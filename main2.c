#include "main2.h"
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/***********/
/* Globals */
/***********/
ValuePool global_vp = (ValuePool){
    .values = (Value[10000]){}, .used = (bool[10000]){}, .cap = 10000};

/*********/
/* Value */
/*********/
void value_ref(Value* v) {
    switch (v->tag) {
    case CONS:
        value_ref(v->val.cons.car);
        value_ref(v->val.cons.cdr);
    case NUMBER:
    case SYMBOL:
    case POINTER:
        v->rc++;
        break;
    }
}

void value_deref(Value* v) {
    assert(v->rc > 0);
    switch (v->tag) {
    case CONS:
        value_deref(v->val.cons.car);
        value_deref(v->val.cons.cdr);
    case NUMBER:
    case SYMBOL:
    case POINTER:
        v->rc--;
        break;
    }

    if (v->rc == 0) {
        valuepool_free(&global_vp, v);
    }
}

Value* value_clone(const Value* v) {
    if (v->tag == CONS) {
        value_ref((Value*)v);
        return (Value*)v;
    }

    Value* ret = valuepool_alloc(&global_vp);
    ret->tag = v->tag;
    ret->quoted = v->quoted;

    switch (v->tag) {
    case NUMBER:
        ret->val.number = v->val.number;
        break;
    case POINTER:
        ret->val.pointer = v->val.pointer;
        break;
    case SYMBOL:
        ret->val.string = strdup(v->val.string);
        break;
    case CONS:
        assert(false);
    }

    return ret;
}

void _value_print(const Value* v) {
    for (int i = 0; i < v->quoted; i++) {
        printf("'");
    }

    switch (v->tag) {
    case NUMBER:
        printf("%g", v->val.number);
        break;
    case SYMBOL:
        printf("%s", v->val.string);
        break;
    case POINTER:
        printf("%p", v->val.pointer);
        break;
    case CONS:
        printf("(");
        const Value* current = v;
        while (true) {
            _value_print(current->val.cons.car);
            if (current->val.cons.cdr->tag == CONS) {
                // LIST, continue
                printf(" ");
                current = current->val.cons.cdr;
            } else if (value_isnil(current->val.cons.cdr)) {
                break;
            } else {
                // PAIR, stop after
                printf(" ");
                _value_print(current->val.cons.cdr);
                break;
            }
        }
        printf(")");
        break;
    }
}

void value_print(const Value* v) {
    _value_print(v);
    printf("\n");
}

bool value_isnil(const Value* v) {
    assert(v);
    return v->tag == SYMBOL && !strcmp("nil", v->val.string);
}

bool value_truthy(const Value* v) {
    return !((v->tag == NUMBER && v->val.number == 0) ||
             (v->tag == POINTER && v->val.pointer == NULL) ||
             (v->tag == SYMBOL &&
              (!strcmp("#f", v->val.string) || value_isnil(v))) ||
             (v->tag == CONS && value_isnil(v->val.cons.car) &&
              value_isnil(v->val.cons.cdr)));
}

specialform value_isspecialform(const Value* v) {
    assert(v);
    assert(v->tag == SYMBOL);

    const char* specialform_keywords[] = {"define", "progn", "cond"};
    const specialform specialform_ptrs[] = {eval_define, eval_progn, eval_cond};

    for (size_t i = 0;
         i < sizeof(specialform_keywords) / sizeof(*specialform_keywords);
         i++) {
        if (!strcmp(v->val.string, specialform_keywords[i])) {
            return specialform_ptrs[i];
        }
    }

    return NULL;
}

Value* _cons(Value* a, Value* b, bool increase_ref) {
    Value* ret = valuepool_alloc(&global_vp);
    ret->tag = CONS;

    if (increase_ref) {
        value_ref(a);
        value_ref(b);
    }

    ret->val.cons.car = a;
    ret->val.cons.cdr = b;

    return ret;
}

Value* _car(Value* v, Env* _) {
    assert(v->tag == CONS);
    assert(v->quoted == 0);

    value_ref(v->val.cons.car);
    return v->val.cons.car;
}

Value* _cdr(Value* v, Env* env) {
    assert(v->tag == CONS);

    if (value_isnil(v->val.cons.cdr)) {
        return env_get(env, "nil");
    }

    value_ref(v->val.cons.cdr);
    return v->val.cons.cdr;
}

bool _symbol_eq(const Value* a, const Value* b) {
    assert(a);
    assert(b);

    assert(a->tag == SYMBOL);
    assert(b->tag == SYMBOL);

    return !strcmp(a->val.string, b->val.string);
}

// (symbol-eq 'a 'b)
Value* symbol_eq(Value* v, struct Env* env) {
    assert(v->tag == CONS);

    Value* a = _car(v, env);
    Value* rest = _cdr(v, env);

    assert(rest->tag == CONS);
    Value* b = _car(rest, env);

    Value* rest_rest = _cdr(rest, env);

    assert(value_isnil(rest_rest));

    assert(a->tag == SYMBOL);
    assert(b->tag == SYMBOL);

    Value* ret = env_get(env, _symbol_eq(a, b) ? "#t" : "#f");

    value_deref(a);
    value_deref(rest);
    value_deref(b);
    value_deref(rest_rest);

    return ret;
}

/*************/
/* ValuePool */
/*************/
Value* valuepool_alloc(ValuePool* vp) {
    assert(vp->in_use < vp->cap);

    for (size_t i = 0; i < vp->cap; i++) {
        if (!vp->used[i]) {
            vp->used[i] = true;
            vp->values[i] = (Value){.rc = 1};
            vp->in_use++;

            if (vp->in_use > vp->high_water) {
                vp->high_water = vp->in_use;
            }

            return vp->values + i;
        }
    }

    assert(false);
}

void valuepool_free(ValuePool* vp, Value* v) {
    ptrdiff_t offset = v - vp->values;
    assert(offset < vp->cap);
    assert(vp->used[offset]);
    assert(vp->values[offset].rc == 0);

    switch (vp->values[offset].tag) {
    case NUMBER:
        break;
    case POINTER:
        break;
    case SYMBOL:
        free(vp->values[offset].val.string);
    case CONS:
        break;
    }

    vp->used[offset] = false;
    vp->in_use--;
}

void valuepool_deinit(const ValuePool* vp) {
    for (size_t i = 0; i < vp->cap; i++) {
        assert(!vp->used[i]);
    }

    assert(vp->in_use == 0);
}

void valuepool_print(const ValuePool* vp) {
    for (size_t i = 0; i < vp->high_water; i++) {
        printf("%4d | ", vp->values[i].rc);
        _value_print(&vp->values[i]);
        printf("\n");
    }
    fflush(stdout);
}

/*******/
/* ENV */
/*******/
Env env_init(size_t size, Env* parent) {
    return (Env){
        .keys = calloc(size, sizeof(*parent->keys)),
        .vals = calloc(size, sizeof(*parent->vals)),
        .cap = size,
        .parent = parent,
    };
}

void env_deinit(Env* e) {
    for (size_t i = 0; i < e->len; i++) {
        free(e->keys[i]);
        value_deref(e->vals[i]);
    }

    free(e->keys);
    free(e->vals);
}

Value* env_get(const Env* e, const char* key) {
    for (size_t i = 0; i < e->len; i++) {
        if (!strcmp(key, e->keys[i])) {
            value_ref(e->vals[i]);
            return e->vals[i];
        }
    }

    if (e->parent) {
        return env_get(e->parent, key);
    } else {
        return env_get(e, "nil");
    }
}

// Same thing as above, but returns a constant reference, and does not increment
// it's rc
const Value* env_get_const(const Env* e, const char* key) {
    for (size_t i = 0; i < e->len; i++) {
        if (!strcmp(key, e->keys[i])) {
            return e->vals[i];
        }
    }

    if (e->parent) {
        return env_get_const(e->parent, key);
    } else {
        return env_get_const(e, "nil");
    }
}

void env_put(Env* e, const char* key, Value* val, bool increase_ref) {
    if (increase_ref) {
        value_ref(val);
    }

    for (size_t i = 0; i < e->len; i++) {
        if (!strcmp(key, e->keys[i])) {
            value_deref(e->vals[i]);
            e->vals[i] = val;

            return;
        }
    }

    if (e->len >= e->cap) {
        e->cap *= 2;
        e->keys = realloc(e->keys, sizeof(*e->keys) * e->cap);
        e->vals = realloc(e->vals, sizeof(*e->vals) * e->cap);
    }

    assert(e->len < e->cap);
    e->keys[e->len] = strdup(key);
    e->vals[e->len] = val;
    e->len++;

    return;
}

char parser_peek(Parser* p) {
    assert(p->pos < p->len);
    return p->text[p->pos];
}
char parser_get(Parser* p) {
    assert(p->pos < p->len);
    return p->text[p->pos++];
}

void parser_skip_whitespace(Parser* p) {
    while (parser_peek(p) == ' ') {
        parser_get(p);
    }
}

Value* parse(Parser* p, Env* e) {
    parser_skip_whitespace(p);

    if (parser_peek(p) == '\'') {
        parser_get(p);

        Value* v = parse(p, e);
        v->quoted++;

        return v;
    } else if (parser_peek(p) != '(') {
        assert(parser_peek(p) != '\'');
        char buf[256] = {0};

        for (int i = 0;
             parser_peek(p) != ')' && parser_peek(p) != ' ' && p->pos < p->len;
             i++) {
            buf[i] = parser_get(p);
        }

        Value* ret = valuepool_alloc(&global_vp);
        if (buf[0] == '0' && buf[1] == 'x') {
            ret->tag = POINTER;

            ret->val.pointer = (void*)strtoull(buf, NULL, 0);
        } else if (isdigit(buf[0]) || buf[0] == '-') {
            ret->tag = NUMBER;

            ret->val.number = strtod(buf, NULL);
        } else {
            ret->tag = SYMBOL;

            ret->val.string = strdup(buf);
        }

        return ret;
    } else {
        // '('
        parser_get(p);
        parser_skip_whitespace(p);

        // Early out for empty list
        if (parser_peek(p) == ')') {
            parser_get(p);
            return env_get(e, "nil");
        }

        // We're parsing a list
        // I'm cheating and getting the first element to make the follwing loop
        // not hurt my brain quite so much
        Value* v = _cons(parse(p, e), env_get(e, "nil"), false);

        Value* current_cons = v;
        while (parser_peek(p) != ')' && p->pos < p->len) {
            Value* next_cons = _cons(parse(p, e), env_get(e, "nil"), false);

            value_deref(current_cons->val.cons.cdr);
            current_cons->val.cons.cdr = next_cons;

            current_cons = next_cons;
        }

        parser_skip_whitespace(p);
        parser_get(p); // ')'

        return v;
    }
}

/********/
/* Eval */
/********/
// (+ 1 2 3)
Value* _eval(Value* v, Env* env) {
    if (v->quoted) {
        Value* cloned = value_clone(v);
        cloned->quoted--;

        return cloned;
    }

    switch (v->tag) {
    case NUMBER:
        value_ref(v);
        return v;
    case POINTER:
        value_ref(v);
        return v;
    case SYMBOL:
        return env_get(env, v->val.string);
    case CONS: {
        Value* ret = NULL;

        Value* first = _car(v, env);
        assert(first->tag == SYMBOL);

        Value* args = _cdr(v, env);
        assert(args->tag == CONS);

        specialform sf = NULL;
        if ((sf = value_isspecialform(first))) {
            Value* sf_result = sf(args, env);

            value_deref(first);
            value_deref(args);

            return sf_result;
        }

        Value* symbol = env_get(env, first->val.string);
        assert(symbol->tag == CONS);

        Value* symbol_first = _car(symbol, env);
        assert(symbol_first->tag == SYMBOL);

        if (_symbol_eq(symbol_first, env_get_const(env, "builtin")) ||
            _symbol_eq(symbol_first, env_get_const(env, "lambda"))) {
            // (builtin '(arg1 arg2 ... argN) 0xFFFF)
            // (lambda '(arg1 arg2 ... argN) body)
            Value* rest = _cdr(symbol, env); // ((arg1 arg2 ... argN) 0xFFFF)
            Value* arg_names = _car(rest, env);
            Value* body_or_ptr_cons = _cdr(rest, env); // (0xFFFF)
            Value* body_or_ptr = _car(body_or_ptr_cons, env);
            assert(arg_names->tag == CONS);
            assert(body_or_ptr_cons->tag == CONS);

            Env funcall_env = env_init(10, env);

            while (!value_isnil(args) && !value_isnil(arg_names)) {
                Value* arg_name = _car(arg_names, env);
                assert(arg_name->tag == SYMBOL);

                if (_symbol_eq(arg_name, env_get_const(env, "&rest"))) {
                    // Signal to skip to next arg name and bind all remaining
                    // args to it as a list
                    value_deref(arg_name);
                    Value* rest = _cdr(arg_names, env);
                    value_deref(arg_names);
                    arg_names = rest;

                    arg_name = _car(arg_names, env);
                    assert(arg_name->tag == SYMBOL);

                    // Now we need to eval the rest of the args into a list
                    Value* rest_arg_list = NULL;
                    Value* rest_arg_list_first = NULL;

                    while (!value_isnil(args)) {
                        Value* head = _car(args, env);
                        Value* next_cons =
                            _cons(_eval(head, env), env_get(env, "nil"), false);
                        value_deref(head);

                        if (rest_arg_list == NULL) {
                            rest_arg_list = next_cons;
                            rest_arg_list_first = next_cons;
                        } else {
                            value_deref(rest_arg_list->val.cons.cdr);
                            rest_arg_list->val.cons.cdr = next_cons;
                            rest_arg_list = next_cons;
                        }

                        Value* rest_args = _cdr(args, env);
                        value_deref(args);
                        args = rest_args;
                    }

                    env_put(&funcall_env, arg_name->val.string,
                            rest_arg_list_first, false);

                    value_deref(arg_name);

                    break;
                }

                Value* arg_value = _car(args, env);
                env_put(&funcall_env, arg_name->val.string,
                        _eval(arg_value, env), false);
                value_deref(arg_value);
                value_deref(arg_name);

                Value* rest_args = _cdr(args, env);
                Value* rest_names = _cdr(arg_names, env);
                value_deref(args);
                value_deref(arg_names);
                args = rest_args;
                arg_names = rest_names;
            }

            if (_symbol_eq(symbol_first, env_get_const(env, "builtin"))) {
                builtin builtin_ptr = body_or_ptr->val.pointer;
                ret = builtin_ptr(&funcall_env);
            } else if (_symbol_eq(symbol_first, env_get_const(env, "lambda"))) {
                ret = _eval(body_or_ptr, &funcall_env);
            }

            env_deinit(&funcall_env);
            value_deref(arg_names);
            value_deref(body_or_ptr);
            value_deref(body_or_ptr_cons);
            value_deref(rest);
        } else if (_symbol_eq(symbol_first, env_get_const(env, "macro"))) {
            // (macro '(arg1 arg2 ... argN) (body))
            Value* rest = _cdr(symbol, env); // ((arg1 arg2 ... argN) (body))
            Value* arg_names = _car(rest, env);
            Value* body_cons = _cdr(rest, env); // (body)
            Value* body = _car(body_cons, env);
            assert(arg_names->tag == CONS);
            assert(body_cons->tag == CONS);

            Env macro_env = env_init(10, env);

            while (!value_isnil(args) && !value_isnil(arg_names)) {
                Value* arg_name = _car(arg_names, env);
                assert(arg_name->tag == SYMBOL);

                if (_symbol_eq(arg_name, env_get_const(env, "&rest"))) {
                    // Signal to skip to next arg name and bind all remaining
                    // args to it as a list
                    value_deref(arg_name);
                    Value* rest = _cdr(arg_names, env);
                    value_deref(arg_names);
                    arg_names = rest;

                    arg_name = _car(arg_names, env);
                    assert(arg_name->tag == SYMBOL);

                    env_put(&macro_env, arg_name->val.string, rest, false);

                    value_deref(arg_name);

                    break;
                }

                env_put(&macro_env, arg_name->val.string, _car(args, env),
                        false);
                value_deref(arg_name);

                Value* rest_args = _cdr(args, env);
                Value* rest_names = _cdr(arg_names, env);
                value_deref(args);
                value_deref(arg_names);
                args = rest_args;
                arg_names = rest_names;
            }

            Value* expanded_form = _eval(body, &macro_env);
            ret = _eval(expanded_form, env);

            env_deinit(&macro_env);
            value_deref(expanded_form);
            value_deref(arg_names);
            value_deref(body);
            value_deref(body_cons);
            value_deref(rest);
        }

        value_deref(symbol_first);
        value_deref(symbol);
        value_deref(args);
        value_deref(first);

        return ret;
    } break;
    }
}

// (define add (lambda (a b) (+ a b)))
// (add (lambda (a b) (+ a b)))
Value* eval_define(Value* v, Env* env) {
    assert(v);
    assert(v->tag == CONS);

    Value* symbol = _car(v, env);
    assert(symbol->tag == SYMBOL);

    Value* rest = _cdr(v, env);
    Value* expr = _car(rest, env);
    Value* evaluated = _eval(expr, env);

    env_put(env, symbol->val.string, evaluated, true);

    value_deref(symbol);
    value_deref(rest);
    value_deref(expr);

    return evaluated;
}

// (progn (+ 1 2) (+ 3 4))
// ((+ 1 2) (+ 3 4))
Value* eval_progn(Value* v, Env* env) {
    assert(v);
    assert(v->tag == CONS);

    Value* first = _car(v, env);
    Value* ret = _eval(first, env);
    value_deref(first);

    v = _cdr(v, env);
    while (!value_isnil(v)) {
        value_deref(ret);

        Value* head = _car(v, env);
        ret = _eval(head, env);
        value_deref(head);

        Value* ret = _cdr(v, env);
        value_deref(v);
        v = ret;
    }
    value_deref(v);

    return ret;
}

// (cond ((< 4 5) "yes") (t "default"))
// (((< 4 5) "yes") (t "default"))
Value* eval_cond(Value* v, Env* env) {
    assert(v);
    assert(v->tag == CONS);

    Value* ret = NULL;

    bool first = true;
    while (!value_isnil(v)) {
        Value* cond_cell = _car(v, env); // ((< 4 5) "yes")
        assert(cond_cell->tag == CONS);
        Value* condition = _car(cond_cell, env);
        Value* condition_evaled = _eval(condition, env);
        bool truthy = value_truthy(condition_evaled);

        if (truthy) {
            Value* rest = _cdr(cond_cell, env);
            Value* expr = _car(rest, env);
            ret = _eval(expr, env);

            value_deref(rest);
            value_deref(expr);

            value_deref(condition_evaled);
            value_deref(condition);
            value_deref(cond_cell);

            break;
        }

        value_deref(condition_evaled);
        value_deref(condition);
        value_deref(cond_cell);

        if (first) {
            first = false;
            v = _cdr(v, env);
        } else {
            Value* rest = _cdr(v, env);
            value_deref(v);
            v = rest;
        }
    }

    if (!first) {
        value_deref(v);
    }

    if (ret == NULL) {
        ret = env_get(env, "nil");
    }

    assert(ret != NULL);
    return ret;
}

/************/
/* Builtins */
/************/
// (plus &rest numbers)
Value* plus(Env* env) {
    // We take in a list of numbers, add them up
    Value* nums = env_get(env, "numbers");
    assert(nums->tag == CONS);

    double acc = 0;

    while (!value_isnil(nums)) {
        Value* head = _car(nums, env);
        assert(head->tag == NUMBER);
        acc += head->val.number;
        value_deref(head);

        Value* rest = _cdr(nums, env);
        value_deref(nums);
        nums = rest;
    }

    value_deref(nums);

    Value* ret = valuepool_alloc(&global_vp);
    ret->tag = NUMBER;
    ret->val.number = acc;

    return ret;
}

// (eq a b)
// Only for numbers for now
Value* eq(Env* env) {
    const Value* a = env_get_const(env, "a");
    const Value* b = env_get_const(env, "b");

    assert(a->tag == NUMBER);
    assert(b->tag == NUMBER);

    return env_get(env, a->val.number == b->val.number ? "#t" : "#f");
}

// (car list)
Value* car(Env* env) {
    Value* list = env_get(env, "list");
    Value* ret = _car(list, env);

    value_deref(list);
    return ret;
}

// (cdr list)
Value* cdr(Env* env) {
    Value* list = env_get(env, "list");
    Value* ret = _cdr(list, env);

    value_deref(list);
    return ret;
}

// (cons a b)
Value* cons(Env* env) {
    Value* a = env_get(env, "a");
    Value* b = env_get(env, "b");
    Value* ret = _cons(a, b, false);

    return ret;
}

// (eval form)
Value* eval(Env* env) {
    Value* form = env_get(env, "form");

    Value* ret = _eval(form, env);

    value_deref(form);

    return ret;
}

Value* make_symbol(const char* name) {
    Value* v = valuepool_alloc(&global_vp);
    v->tag = SYMBOL;
    v->val.string = strdup(name);

    return v;
}

void setup_symbols(Env* e) {
    const char* symbols[] = {"nil",    "builtin", "lambda", "macro",
                             "string", "#t",      "#f",     "&rest"};

    for (size_t i = 0; i < sizeof(symbols) / sizeof(*symbols); i++) {
        env_put(e, symbols[i], make_symbol(symbols[i]), false);
    }
}

Value* make_builtin(Env* e, char* args[], size_t args_len, builtin func_ptr) {
    char buf[512] = {0};
    char* buf_ptr = buf;

    buf_ptr += sprintf(buf_ptr, "(builtin (");
    for (size_t i = 0; i < args_len; i++) {
        buf_ptr += sprintf(buf_ptr, "%s", args[i]);

        if (i != args_len - 1) {
            buf_ptr += sprintf(buf_ptr, " ");
        }
    }
    buf_ptr += sprintf(buf_ptr, ") %p)", func_ptr);

    Parser parser = (Parser){.text = buf, .len = strlen(buf)};

    return parse(&parser, e);
}

int main() {
    Env env = env_init(10, NULL);
    setup_symbols(&env);
    env_put(&env, "+",
            make_builtin(&env, (char*[]){"&rest", "numbers"}, 2, (void*)plus),
            false);
    env_put(&env, "eq", make_builtin(&env, (char*[]){"a", "b"}, 2, (void*)eq),
            false);
    env_put(&env, "car", make_builtin(&env, (char*[]){"list"}, 1, (void*)car),
            false);
    env_put(&env, "cdr", make_builtin(&env, (char*[]){"list"}, 1, (void*)cdr),
            false);
    env_put(&env, "cons",
            make_builtin(&env, (char*[]){"a", "b"}, 2, (void*)cons), false);
    env_put(&env, "eval", make_builtin(&env, (char*[]){"form"}, 1, (void*)eval),
            false);

    // clang-format off
    char* input = "(progn "
        "(define add '(lambda (a b) (+ a b)))"
        "(define apply '(lambda (func &rest args) (eval (cons func args))))"
        "(define list '(lambda (&rest args) args))"
        "(define if '(macro (condition true-body false-body) (list 'cond (list condition true-body) (list #t false-body))))"
        "(add 5 6)"
        "(cond (#f 68) (nil 54) (#t 42))"
        "(define sum '(lambda (x) (if (eq 1 x) 1 (+ x (sum (+ x -1))))))"
        "(if (eq 5 (+ 1 4)) 1 2)"
        "(sum 5)"
        "(define add1 '(lambda (x) (+ 1 x)))"
        "(define map '(lambda (func l) (if (cdr l) (cons (apply func (car l)) (map func (cdr l))) (apply func (car l)))))"
        "(map 'add1 '(1 2 3))"
        ")";
    // clang-format on
    Parser parser = (Parser){.text = input, .len = strlen(input)};
    Value* parsed = parse(&parser, &env);
    Value* evaled = _eval(parsed, &env);
    value_print(parsed);
    value_print(evaled);
    value_deref(parsed);
    value_deref(evaled);

    env_deinit(&env);

    /* valuepool_print(&global_vp); */
    valuepool_deinit(&global_vp);
}
