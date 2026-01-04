#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// (+ 1 2)
// (if 1 2 3)
// (and t f t)
// (or t f f)
// (lambda (x y z) (+ x y z))
// (let ((x 1) (y 2) (z 3)) (+ x y z))

typedef struct Parser {
    char* text;
    size_t pos;
    size_t len;
} Parser;

char parser_peek(const Parser* p) { return p->text[p->pos]; }
char parser_get(Parser* p) { return p->text[p->pos++]; }
void parser_skip_whitespace(Parser* p) {
    while (p->text[p->pos] == ' ') {
        p->pos++;
    }
}

struct Value;

typedef struct List {
    struct Value** values;
    size_t cap;
    size_t len;
} List;

typedef struct Cons {
    struct Value* car;
    struct Value* cdr;
} Cons;

struct Env;
struct Value* env_get(const struct Env* e, const char* symbol);

typedef struct Value* (*builtin_procedure)(const struct Value*, struct Env* e);

typedef enum ValueTag {
    NIL,
    NUMBER,
    STRING,
    BOOLEAN,
    PROCEDURE,
    SPECIAL_FORM,
    BUILTIN,
    SYMBOL,
    LIST,
    MACRO,
    CONS,
} ValueTag;

typedef struct Value {
    ValueTag tag;
    union {
        double number;
        char* string;
        bool boolean;
        List list;
        builtin_procedure builtin;
        Cons cons;
    } val;
    int quoted;
    int rc;
} Value;

struct ValuePool;
struct ValuePool valuepool_init(Value*, bool*, size_t);
Value* valuepool_alloc(struct ValuePool*);
void valuepool_free(struct ValuePool*, Value*);

struct ValuePool global_vp;

// Increase a value's reference count, and all children
void value_ref(Value* v) {
    switch (v->tag) {
    case PROCEDURE:
    case MACRO:
    case LIST:
        for (size_t i = 0; i < v->val.list.len; i++) {
            value_ref(v->val.list.values[i]);
        }
    case NIL:
    case NUMBER:
    case STRING:
    case BOOLEAN:
    case BUILTIN:
    case SYMBOL:
    case SPECIAL_FORM:
        v->rc += 1;
        break;
    case CONS:
        v->rc += 1;
        value_ref(v->val.cons.car);
        value_ref(v->val.cons.cdr);
    }
}

void value_deref(Value* v) {
    assert(v->rc >= 1);

    switch (v->tag) {
    case PROCEDURE:
    case MACRO:
    case LIST:
        for (size_t i = 0; i < v->val.list.len; i++) {
            value_deref(v->val.list.values[i]);
        }
    case NIL:
    case NUMBER:
    case STRING:
    case BOOLEAN:
    case BUILTIN:
    case SYMBOL:
    case SPECIAL_FORM:
        v->rc -= 1;
        break;
    case CONS:
        v->rc -= 1;
        value_deref(v->val.cons.car);
        value_deref(v->val.cons.cdr);
    }

    if (v->rc == 0) {
        switch (v->tag) {
        case BOOLEAN:
        case NIL:
        case NUMBER:
        case BUILTIN:
        case SPECIAL_FORM:
            break;
        case STRING:
        case SYMBOL:
            free(v->val.string);
            break;
        case PROCEDURE:
        case MACRO:
        case LIST:
            free(v->val.list.values);
            break;
        }

        valuepool_free(&global_vp, v);
    }
}

void value_print(const Value* v) {
    if (v->tag != PROCEDURE) {
        for (int i = 0; i < v->quoted; i++) {
            putchar('\'');
        }
    }

    switch (v->tag) {
    case NIL:
        printf("nil");
        break;
    case NUMBER:
        printf("%g", v->val.number);
        break;
    case STRING:
        printf("\"%s\"", v->val.string);
        break;
    case BOOLEAN:
        printf("%s", v->val.boolean ? "t" : "f");
        break;
    case SPECIAL_FORM:
        printf("SPECIAL_FORM: 0x%p", v->val.builtin);
        break;
    case BUILTIN:
        printf("Builtin: 0x%p", v->val.builtin);
        break;
    case SYMBOL:
        printf("%s", v->val.string);
        break;
    case MACRO:
        printf("(macro ");
        for (size_t i = 0; i < v->val.list.len; i++) {
            value_print(v->val.list.values[i]);

            if (i != v->val.list.len - 1) {
                printf(" ");
            }
        }
        printf(")");
        break;
    case PROCEDURE:
        if (v->quoted) {
            printf("%s", v->val.list.values[0]->val.list.values[0]->val.string);
            break;
        } else {
            printf("Procedure: ");
        }
    case LIST:
        printf("(");
        for (size_t i = 0; i < v->val.list.len; i++) {
            value_print(v->val.list.values[i]);

            if (i != v->val.list.len - 1) {
                printf(" ");
            }
        }
        printf(")");
        break;
    case CONS:
        // I'm going to make the probably incorrect assumption that all cons
        // cells are lists
        printf("(");
        const Value* current = v;
        while (current->tag == CONS) {
            value_print(current->val.cons.car);

            current = current->val.cons.cdr;
        }
        printf(")");
        break;
    }
}

bool value_truthy(const Value* v) {
    return !((v->tag == BOOLEAN && v->val.boolean == false) || v->tag == NIL ||
             (v->tag == NUMBER && v->val.number == 0) ||
             (v->tag == CONS && v->val.cons.car->tag == NIL) ||
             (v->tag == STRING && strlen(v->val.string) == 0) ||
             (v->tag == LIST && v->val.list.len == 0) ||
             (v->tag == SYMBOL && !strcmp("f", v->val.string)));
}

List list_init();
void list_add(List* l, Value* v, bool ref);

Value* value_clone(const Value* v) {
    Value* ret = valuepool_alloc(&global_vp);
    ret->tag = v->tag;
    ret->quoted = v->quoted;

    switch (v->tag) {
    case NIL:
        break;
    case NUMBER:
        ret->val.number = v->val.number;
        break;
    case SYMBOL:
    case STRING:
        ret->val.string = strdup(v->val.string);
        break;
    case BOOLEAN:
        ret->val.boolean = v->val.boolean;
        break;
    case SPECIAL_FORM:
    case BUILTIN:
        ret->val.builtin = v->val.builtin;
        break;
    case PROCEDURE:
    case MACRO:
    case LIST:
        ret->val.list = list_init();

        for (size_t i = 0; i < v->val.list.len; i++) {
            list_add(&ret->val.list, value_clone(v->val.list.values[i]), false);
        }
        break;
    case CONS:
        assert(false);
        break;
    }

    return ret;
}

List list_init() {
    return (List){
        .values = calloc(3, sizeof(Value)),
        .cap = 3,
        .len = 0,
    };
}

void list_add(List* l, Value* v, bool ref) {
    if (l->len >= l->cap) {
        l->cap *= 2;
        l->values = realloc(l->values, l->cap * sizeof(*l->values));
    }

    l->values[l->len++] = v;
    if (ref) {
        value_ref(v);
    }
}

Value* internal_car(List l) {
    assert(l.len > 0);

    value_ref(l.values[0]);

    return l.values[0];
}

// We're passed a list that looks like (arg1 arg2 arg3)
// For car this means ((arg1)), where arg1 is a list
// In essence car only accepts 1 argument
Value* car(const Value* v, struct Env* _) {
    assert(v->tag == LIST ||
           v->tag == PROCEDURE); // Maybe need to add procedure?
    assert(v->val.list.len == 1);

    return internal_car(v->val.list.values[0]->val.list);
}

Value* internal_cdr(List l) {
    List out = list_init();

    for (size_t i = 1; i < l.len; i++) {
        list_add(&out, l.values[i], true);
    }

    Value* ret = valuepool_alloc(&global_vp);
    ret->tag = LIST;
    ret->val.list = out;

    return ret;
}

Value* cdr(const Value* v, struct Env* e) {
    assert(v->tag == LIST || v->tag == PROCEDURE);
    assert(v->val.list.len == 1);

    Value* arg1 = v->val.list.values[0];
    assert(arg1->tag == LIST || arg1->tag == PROCEDURE || arg1->tag == MACRO);
    assert(arg1->val.list.len >= 1);

    List arg1_list = arg1->val.list;

    if (arg1_list.len <= 1) {
        return env_get(e, "nil");
    }

    return internal_cdr(arg1_list);
}

typedef struct ValuePool {
    Value* values;
    bool* in_use;
    size_t cap;
    size_t len;
} ValuePool;

ValuePool valuepool_init(Value* value_buf, bool* used_buf, size_t cap) {
    return (ValuePool){
        .values = value_buf,
        .in_use = used_buf,
        .cap = cap,
        .len = 0,
    };
}

void valuepool_deinit(ValuePool* vp) {
    if (vp->len != 0) {
        printf("vp len is %zu\n", vp->len);
    }
    assert(vp->len == 0);
}

Value* valuepool_alloc(ValuePool* vp) {
    assert(vp->len < vp->cap);

    for (size_t i = 0; i < vp->cap; i++) {
        if (!vp->in_use[i]) {
            vp->in_use[i] = true;
            vp->values[i] = (Value){.rc = 1};
            vp->len++;

            return vp->values + i;
        }
    }

    assert(false);
    return NULL;
}

void valuepool_free(ValuePool* vp, Value* v) {
    assert(vp->values <= v && v <= (vp->values + vp->cap));
    assert(v->rc == 0);

    ptrdiff_t offset = v - vp->values;

    vp->in_use[offset] = false;
    vp->len--;
}

Value t = (Value){.tag = BOOLEAN, .val.boolean = true, .rc = 2};
Value f = (Value){.tag = BOOLEAN, .val.boolean = false, .rc = 2};
Value nil = (Value){.tag = NIL, .rc = 2};
Value type_nil = (Value){.tag = SYMBOL, .val.string = "#nil", .rc = 2};
Value type_number = (Value){.tag = SYMBOL, .val.string = "#number", .rc = 2};
Value type_string = (Value){.tag = SYMBOL, .val.string = "#string", .rc = 2};
Value type_boolean = (Value){.tag = SYMBOL, .val.string = "#boolean", .rc = 2};
Value type_procedure =
    (Value){.tag = SYMBOL, .val.string = "#procedure", .rc = 2};
Value type_specialform =
    (Value){.tag = SYMBOL, .val.string = "#special-form", .rc = 2};
Value type_symbol = (Value){.tag = SYMBOL, .val.string = "#symbol", .rc = 2};
Value type_list = (Value){.tag = SYMBOL, .val.string = "#list", .rc = 2};
Value type_macro = (Value){.tag = SYMBOL, .val.string = "#macro", .rc = 2};

typedef struct Env {
    struct Env* parent;

    char** keys;
    Value** vals;
    size_t len;
    size_t cap;
} Env;

Env env_init() {
    return (Env){
        .keys = calloc(3, sizeof(char*)),
        .vals = calloc(3, sizeof(Value*)),
        .len = 0,
        .cap = 3,
    };
}

void env_deinit(Env* e) {
    if (e) {
        for (size_t i = 0; i < e->len; i++) {
            free(e->keys[i]);
            value_deref(e->vals[i]);
        }

        free(e->keys);
        free(e->vals);
    }
}

Value* env_get(const Env* e, const char* symbol) {
    if (!e) {
        return &nil;
    }

    for (size_t i = 0; i < e->len; i++) {
        if (!strcmp(symbol, e->keys[i])) {
            value_ref(e->vals[i]);
            return e->vals[i];
        }
    }

    return env_get(e->parent, symbol);
}

void env_put(Env* e, const char* symbol, Value* v) {
    for (size_t i = 0; i < e->len; i++) {
        if (!strcmp(symbol, e->keys[i])) {
            // If we find a duplicate key, replace the old value
            value_deref(e->vals[i]);
            value_ref(v);
            e->vals[i] = v;

            return;
        }
    }

    // If we don't find it, add a new entry
    if (e->len >= e->cap) {
        e->cap *= 2;
        e->keys = realloc(e->keys, e->cap * sizeof(*e->keys));
        e->vals = realloc(e->vals, e->cap * sizeof(*e->vals));
    }

    e->keys[e->len] = strdup(symbol);
    e->vals[e->len] = v;
    value_ref(v);
    e->len++;
}

void env_print(const Env* e) {
    for (size_t i = 0; i < e->len; i++) {
        printf("%10s --> ", e->keys[i]);
        value_print(e->vals[i]);
        printf("\n");
    }
}

Value* handle_if(const Value*, Env*);
Value* handle_define(const Value*, Env*);
Value* handle_and(const Value*, Env*);
Value* handle_or(const Value*, Env*);
Value* handle_progn(const Value*, Env*);
Value* handle_cond(const Value*, Env*);

Value* parse(Parser* input);
// TODO I think we're going to need a similar internal_eval and eval split as we
// needed with car and cdr
// The issue is I want to call with a list from my C code, but from the lisp
// code it makes more sense to call with a list of args where eval expects a
// single arg
Value* internal_eval(const Value* v, Env* e) {
    if (v->quoted) {
        Value* ret = value_clone(v);
        ret->quoted--;

        return ret;
    } else if (v->tag == LIST) {
        Value* procedure = internal_eval(v->val.list.values[0], e);
        Value* ret_val = NULL;

        if (procedure->tag == SPECIAL_FORM) {
            // Special forms evaluate their own arguments
            return procedure->val.builtin(v, e);
        } else if (procedure->tag == MACRO) {
            Value* arguments = internal_cdr(v->val.list);
            Value* macro_arg_names = procedure->val.list.values[0];
            List macro_arg_names_list = macro_arg_names->val.list;
            Value* macro_body = procedure->val.list.values[1];

            Env macrocall_env = env_init();
            macrocall_env.parent = e;

            for (size_t i = 1; i < macro_arg_names_list.len; i++) {
                env_put(&macrocall_env,
                        macro_arg_names_list.values[i]->val.string,
                        arguments->val.list.values[i - 1]);
            }
            value_deref(arguments);

            Value* macro_eval = internal_eval(macro_body, &macrocall_env);
            env_deinit(&macrocall_env);

            ret_val = internal_eval(macro_eval, e);

            value_deref(macro_eval);
        } else if (procedure->tag == BUILTIN) {
            Value* arguments = internal_cdr(v->val.list);
            // Now evaluate all of the arguments to prepare them for the
            // builtin
            List evaluated_args = list_init();
            for (size_t i = 0; i < arguments->val.list.len; i++) {
                list_add(&evaluated_args,
                         internal_eval(arguments->val.list.values[i], e),
                         false);
            }

            Value* builtin_args = valuepool_alloc(&global_vp);
            builtin_args->tag = LIST;
            builtin_args->val.list = evaluated_args;

            ret_val = procedure->val.builtin(builtin_args, e);

            value_deref(builtin_args);
            value_deref(arguments);
        } else if (procedure->tag == PROCEDURE) {
            assert(procedure->val.list.values[0]->tag == LIST);
            assert(procedure->val.list.values[1]->tag == LIST ||
                   procedure->val.list.values[1]->tag == SYMBOL);

            List name_args = procedure->val.list.values[0]->val.list;
            Value* func_body = procedure->val.list.values[1];

            Env funcall_env = env_init();
            funcall_env.parent = e;
            bool rest = false;

            // Map the provided arguments into the funcall_env
            for (size_t i = 1; i < name_args.len; i++) {
                if (!strcmp("&rest", name_args.values[i]->val.string)) {
                    rest = true;

                    // Map all remaining arguments into a list with name at
                    // [i+1]
                    List rest_args = list_init();

                    for (size_t j = i; j < v->val.list.len; j++) {
                        Value* eval_result =
                            internal_eval(v->val.list.values[j], e);

                        list_add(&rest_args, eval_result, false);
                    }

                    Value* rest = valuepool_alloc(&global_vp);
                    rest->tag = LIST;
                    rest->val.list = rest_args;

                    env_put(&funcall_env, name_args.values[i + 1]->val.string,
                            rest);

                    value_deref(rest);
                    i++;
                } else {
                    Value* eval_result =
                        internal_eval(v->val.list.values[i], e);
                    env_put(&funcall_env, name_args.values[i]->val.string,
                            eval_result);

                    value_deref(eval_result);
                }
            }

            if (!rest && procedure->val.list.values[0]->val.list.len !=
                             v->val.list.len) {
                fprintf(stderr,
                        "error: attempting to call %s with %zu arguments, "
                        "expects %zu\n",
                        v->val.list.values[0]->val.string, v->val.list.len - 1,
                        procedure->val.list.values[0]->val.list.len - 1);
                assert(procedure->val.list.values[0]->val.list.len ==
                       v->val.list.len);
            }

            // All procedure arguments are now bound, recursive call into eval
            // with the new environment
            ret_val = internal_eval(func_body, &funcall_env);
            env_deinit(&funcall_env);
        }

        value_deref(procedure);

        assert(ret_val);
        return ret_val;
    } else if (v->tag == SYMBOL) {
        return env_get(e, v->val.string);
    } else if (v->tag == NUMBER || v->tag == STRING) {
        value_ref((Value*)v);
        return (Value*)v;
    }

    return env_get(e, "nil");
}

// Eval a procedure which takes 1 argument
Value* eval(const Value* v, Env* e) {
    assert(v->tag == LIST);
    List l = v->val.list;

    assert(l.len == 1);

    return internal_eval(l.values[0], e);
}

// if takes 3 (4 including symbol if) arguments
Value* handle_if(const Value* v, Env* e) {
    // (if condition true_expression false_expression)
    assert(v->tag == LIST);
    List l = v->val.list;

    assert(l.len == 4);
    assert(l.values[0]->tag == SYMBOL &&
           !strcmp("if", l.values[0]->val.string));

    // False only if nil, 0, "", false, f
    Value* condition = internal_eval(l.values[1], e);
    bool truthy = value_truthy(condition);

    value_deref(condition);

    if (truthy) {
        return internal_eval(l.values[2], e);
    } else {
        return internal_eval(l.values[3], e);
    }
}

Value* handle_and(const Value* v, Env* e) {
    assert(v->tag == LIST);
    List l = v->val.list;

    assert(l.values[0]->tag == SYMBOL &&
           !strcmp("and", l.values[0]->val.string));
    assert(l.len > 1);

    for (size_t i = 1; i < l.len; i++) {
        Value* result = internal_eval(l.values[i], e);
        bool truthy = value_truthy(result);
        value_deref(result);

        if (!truthy) {
            return env_get(e, "f");
        }
    }

    return env_get(e, "t");
}

Value* handle_or(const Value* v, Env* e) {
    assert(v->tag == LIST);
    List l = v->val.list;

    assert(l.values[0]->tag == SYMBOL &&
           !strcmp("or", l.values[0]->val.string));
    assert(l.len > 1);

    for (size_t i = 1; i < l.len; i++) {
        Value* result = internal_eval(l.values[i], e);
        bool truthy = value_truthy(result);
        value_deref(result);

        if (truthy) {
            return env_get(e, "t");
        }
    }

    return env_get(e, "f");
}

Value handle_lambda(Parser* input) { return (Value){}; }
Value handle_let(Parser* input) { return (Value){}; }
Value* handle_define(const Value* v, Env* e) {
    assert(v->tag == LIST);
    List l = v->val.list;
    assert(l.len == 3);

    // Procedure definition form
    // (define (name [arg1 [arg2 …]]) body …)
    // Regular variable definition form
    // (define name expr)
    assert(l.values[0]->tag == SYMBOL &&
           !strcmp("define", l.values[0]->val.string));

    if (l.values[1]->tag == SYMBOL) {
        // Regular path
        assert(l.values[1]->tag == SYMBOL);

        Value* expr = internal_eval(l.values[2], e);

        env_put(e, l.values[1]->val.string, expr);

        return expr;
    } else if (l.values[1]->tag == LIST) {
        // Procedure path
        assert(l.values[1]->tag == LIST);
        assert(l.values[2]->tag == LIST || l.values[2]->tag == SYMBOL);

        List name_vars = l.values[1]->val.list;
        assert(name_vars.values[0]->tag == SYMBOL);

        // Procedures are stored as `Value`s, with the `List` field being
        // populated as follows
        // ((procedure_name [arg1] [arg2] ... [argN]) (procedure_body...))

        List procedure_list = list_init();
        list_add(&procedure_list, l.values[1], true);
        list_add(&procedure_list, l.values[2], true);

        Value* procedure = valuepool_alloc(&global_vp);
        procedure->tag = PROCEDURE;
        procedure->val.list = procedure_list;

        env_put(e, name_vars.values[0]->val.string, procedure);

        return procedure;
    }

    assert(false);
}

Value* handle_define_macro(const Value* v, Env* e) {
    assert(v->tag == LIST);
    List l = v->val.list;
    assert(l.len == 3);

    assert(l.values[0]->tag == SYMBOL &&
           !strcmp("define-macro", l.values[0]->val.string));
    assert(l.values[1]->tag == LIST);
    assert(l.values[1]->tag == LIST);

    assert(l.values[1]->tag == LIST);
    assert(l.values[2]->tag == LIST || l.values[2]->tag == SYMBOL);

    List name_vars = l.values[1]->val.list;
    assert(name_vars.values[0]->tag == SYMBOL);

    List macro_list = list_init();
    list_add(&macro_list, l.values[1], true);
    list_add(&macro_list, l.values[2], true);

    Value* macro = valuepool_alloc(&global_vp);
    macro->tag = MACRO;
    macro->val.list = macro_list;

    env_put(e, name_vars.values[0]->val.string, macro);

    return macro;
}

Value* handle_progn(const Value* v, Env* e) {
    assert(v->tag == LIST);
    List l = v->val.list;

    assert(l.values[0]->tag == SYMBOL &&
           !strcmp("progn", l.values[0]->val.string));
    assert(l.len > 1);

    for (size_t i = 1; i < l.len - 1; i++) {
        Value* result = internal_eval(l.values[i], e);
        value_deref(result);
    }

    Value* result = internal_eval(l.values[l.len - 1], e);
    return result;
}

// Display takes 1 argument
// (display arg1)
Value* handle_display(const Value* v, Env* _) {
    assert(v->tag == LIST);
    List l = v->val.list;

    assert(l.len == 1);

    value_print(l.values[0]);
    printf("\n");

    value_ref(l.values[0]);
    return l.values[0];
}

Value* handle_cond(const Value* v, Env* e) {
    // (case
    //   ((> x 1) 42)
    //   ((> x -4) 41)
    //   (t default))
    assert(v->tag == LIST);
    List l = v->val.list;

    assert(l.values[0]->tag == SYMBOL &&
           !strcmp("cond", l.values[0]->val.string));
    assert(l.len > 1);

    for (size_t i = 1; i < l.len; i++) {
        assert(l.values[i]->tag == LIST);
        List case_list = l.values[i]->val.list;

        assert(case_list.len == 2);

        Value* boolean_result = internal_eval(case_list.values[0], e);
        bool truthy = value_truthy(boolean_result);
        value_deref(boolean_result);

        if (truthy) {
            return internal_eval(case_list.values[1], e);
        }
    }

    return env_get(e, "nil");
}

typedef enum BinOp {
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
} BinOp;
Value* handle_arithmetic(const Value* v, BinOp op) {
    assert(v->tag == LIST);
    List l = v->val.list;
    assert(l.len >= 1);

    Value* first = l.values[0];
    assert(first->tag == NUMBER);

    Value* accumulator = valuepool_alloc(&global_vp);
    accumulator->tag = NUMBER;
    accumulator->val.number = first->val.number;

    for (size_t i = 1; i < l.len; i++) {
        Value* next = l.values[i];
        assert(next->tag == NUMBER);

        switch (op) {
        case ADD:
            accumulator->val.number += next->val.number;
            break;
        case SUB:
            accumulator->val.number -= next->val.number;
            break;
        case MUL:
            accumulator->val.number *= next->val.number;
            break;
        case DIV:
            accumulator->val.number /= next->val.number;
            break;
        case MOD:
            accumulator->val.number =
                fmod(accumulator->val.number, next->val.number);
            break;
        }
    }

    return accumulator;
}

Value* handle_add(const Value* v, Env* _) { return handle_arithmetic(v, ADD); }
Value* handle_sub(const Value* v, Env* _) { return handle_arithmetic(v, SUB); }
Value* handle_mul(const Value* v, Env* _) { return handle_arithmetic(v, MUL); }
Value* handle_div(const Value* v, Env* _) { return handle_arithmetic(v, DIV); }
Value* handle_mod(const Value* v, Env* _) { return handle_arithmetic(v, MOD); }

typedef enum CompOp {
    LT,
    GT,
    EQ,
    LE,
    GE,
    NE,
} CompOp;
Value* handle_logical(const Value* v, const Env* e, CompOp op) {
    assert(v->tag == LIST);
    List l = v->val.list;

    assert(l.len == 2);

    Value* first = v->val.list.values[0];
    Value* second = v->val.list.values[1];
    if (first->tag == NUMBER && second->tag == NUMBER) {
        assert(first->tag == NUMBER);
        assert(second->tag == NUMBER);

        Value* ret;

        switch (op) {
        case LT:
            ret = env_get(e,
                          (first->val.number < second->val.number) ? "t" : "f");
            break;
        case GT:
            ret = env_get(e,
                          (first->val.number > second->val.number) ? "t" : "f");
            break;
        case EQ:
            ret = env_get(e, (first->val.number == second->val.number) ? "t"
                                                                       : "f");
            break;
        case LE:
            ret = env_get(e, (first->val.number <= second->val.number) ? "t"
                                                                       : "f");
            break;
        case GE:
            ret = env_get(e, (first->val.number >= second->val.number) ? "t"
                                                                       : "f");
            break;
        case NE:
            ret = env_get(e, (first->val.number != second->val.number) ? "t"
                                                                       : "f");
            break;
        }

        return ret;
    } else if (first->tag == BOOLEAN && second->tag == BOOLEAN) {
        assert(op == EQ || op == NE);

        Value* ret;

        switch (op) {
        case EQ:
            ret = env_get(e, (first->val.boolean == second->val.boolean) ? "t"
                                                                         : "f");
            break;
        case NE:
            ret = env_get(e, (first->val.boolean != second->val.boolean) ? "t"
                                                                         : "f");
            break;
        default:
            assert(false);
        }

        return ret;
    }

    assert(false);
}

Value* handle_lt(const Value* v, Env* e) { return handle_logical(v, e, LT); };
Value* handle_gt(const Value* v, Env* e) { return handle_logical(v, e, GT); };
Value* handle_eq(const Value* v, Env* e) { return handle_logical(v, e, EQ); };
Value* handle_le(const Value* v, Env* e) { return handle_logical(v, e, LE); };
Value* handle_ge(const Value* v, Env* e) { return handle_logical(v, e, GE); };
Value* handle_ne(const Value* v, Env* e) { return handle_logical(v, e, NE); };

Value* builtin_tagp(const Value* v, ValueTag tag) {
    assert(v->tag == LIST);
    List l = v->val.list;
    assert(l.len == 1);

    Value* ret = valuepool_alloc(&global_vp);
    ret->tag = BOOLEAN;

    ret->val.boolean = l.values[0]->tag == tag;

    return ret;
}

Value* builtin_nilp(const Value* v, Env* _) { return builtin_tagp(v, NIL); }
Value* builtin_numberp(const Value* v, Env* _) {
    return builtin_tagp(v, NUMBER);
}
Value* builtin_stringp(const Value* v, Env* _) {
    return builtin_tagp(v, STRING);
}
Value* builtin_booleanp(const Value* v, Env* _) {
    return builtin_tagp(v, BOOLEAN);
}
Value* builtin_procedurep(const Value* v, Env* _) {
    return builtin_tagp(v, PROCEDURE);
}
Value* builtin_specialformp(const Value* v, Env* _) {
    return builtin_tagp(v, SPECIAL_FORM);
}
Value* builtin_builtinp(const Value* v, Env* _) {
    return builtin_tagp(v, BUILTIN);
}
Value* builtin_symbolp(const Value* v, Env* _) {
    return builtin_tagp(v, SYMBOL);
}
Value* builtin_listp(const Value* v, Env* _) { return builtin_tagp(v, LIST); }
Value* builtin_macrop(const Value* v, Env* _) { return builtin_tagp(v, MACRO); }

// Takes 1 arg
Value* value_tag(const Value* v, Env* e) {
    assert(v->tag == LIST);
    List l = v->val.list;
    assert(l.len == 1);

    Value* inner = l.values[0];

    switch (inner->tag) {
    case NIL:
        return env_get(e, "#nil");
    case NUMBER:
        return env_get(e, "#number");
    case STRING:
        return env_get(e, "#string");
    case BOOLEAN:
        return env_get(e, "#boolean");
    case PROCEDURE:
        return env_get(e, "#procedure");
    case SPECIAL_FORM:
        return env_get(e, "#special-form");
    case BUILTIN:
        return env_get(e, "#builtin");
    case SYMBOL:
        return env_get(e, "#symbol");
    case LIST:
        return env_get(e, "#list");
    case MACRO:
        return env_get(e, "#macro");
    }
}

Value* parse(Parser* input) {
    parser_skip_whitespace(input);
    if (parser_peek(input) == '\'') {
        // Parse expression into value, return it quoted
        parser_get(input);
        Value* v = parse(input);
        v->quoted += 1;

        return v;
    }
    if (parser_peek(input) != '(') {
        // Entering from a recursive parse call
        // The expression here should be a literal
        // Either a symbol literal, number literal, string literal, char
        // literal, quoted value

        char buf[256] = {0};
        if (parser_peek(input) == '"') {
            // Consume first quote
            parser_get(input);

            for (int i = 0;
                 parser_peek(input) != '"' && input->pos < input->len; i++) {
                if (parser_peek(input) == '\\') {
                    parser_get(input);
                }

                buf[i] = parser_get(input);
            }

            assert(parser_peek(input) == '"');
            parser_get(input);

            Value* ret = valuepool_alloc(&global_vp);
            ret->tag = STRING;
            ret->val.string = strdup(buf);

            return ret;
        } else {
            for (int i = 0;
                 parser_peek(input) != ' ' && parser_peek(input) != ')' &&
                 input->pos < input->len;
                 i++) {
                buf[i] = parser_get(input);
            }

            Value* ret = valuepool_alloc(&global_vp);
            if (isdigit(buf[0])) {
                // Parse number
                ret->tag = NUMBER;
                ret->val.number = strtod(buf, NULL);
            } else {
                // Symbol
                ret->tag = SYMBOL;
                ret->val.string = strdup(buf);
            }

            return ret;
        }
    }

    assert(parser_peek(input) == '(');
    parser_get(input);

    // If we make it here then the first symbol must be a function
    // Start of a list
    // (+ 1 2 (+ 3 4))
    //  ^^^
    List l = list_init();

    Value* v = parse(input);
    list_add(&l, v, false);

    while (parser_peek(input) != ')') {
        v = parse(input);
        list_add(&l, v, false);
    }

    // Consume the closing `)`
    assert(parser_peek(input) == ')');
    parser_get(input);

    Value* ret = valuepool_alloc(&global_vp);
    ret->tag = LIST;
    ret->val.list = l;

    return ret;
}

Value* symbol_eq(const Value* v, Env* _) {
    assert(v->tag == LIST);
    List l = v->val.list;
    assert(l.len == 2);

    Value* lhs = l.values[0];
    Value* rhs = l.values[1];

    assert(lhs->tag == SYMBOL);
    assert(rhs->tag == SYMBOL);

    Value* ret = valuepool_alloc(&global_vp);
    ret->tag = BOOLEAN;
    ret->val.boolean = !strcmp(lhs->val.string, rhs->val.string);

    return ret;
}

Value* string_eq(const Value* v, Env* _) {
    assert(v->tag == LIST);
    List l = v->val.list;
    assert(l.len == 2);

    Value* lhs = l.values[0];
    Value* rhs = l.values[1];

    assert(lhs->tag == STRING);
    assert(rhs->tag == STRING);

    Value* ret = valuepool_alloc(&global_vp);
    ret->tag = BOOLEAN;
    ret->val.boolean = !strcmp(lhs->val.string, rhs->val.string);

    return ret;
}

// (prepend list x)
Value* builtin_list_prepend(const Value* v, Env* _) {
    assert(v->tag == LIST);
    List args = v->val.list;

    assert(args.len == 2);
    assert(args.values[0]->tag == LIST);

    List old = args.values[0]->val.list;
    Value* to_add = args.values[1];
    List new = list_init();

    list_add(&new, to_add, true);

    for (size_t i = 0; i < old.len; i++) {
        list_add(&new, old.values[i], true);
    }

    Value* ret = valuepool_alloc(&global_vp);
    ret->tag = LIST;
    ret->val.list = new;

    return ret;
}

// (append list x)
Value* builtin_list_append(const Value* v, Env* _) {
    assert(v->tag == LIST);
    List args = v->val.list;

    assert(args.len == 2);
    assert(args.values[0]->tag == LIST);

    List old = args.values[0]->val.list;
    Value* to_add = args.values[1];
    List new = list_init();

    for (size_t i = 0; i < old.len; i++) {
        list_add(&new, old.values[i], true);
    }

    list_add(&new, to_add, true);

    Value* ret = valuepool_alloc(&global_vp);
    ret->tag = LIST;
    ret->val.list = new;

    return ret;
}

// (list arg1 arg2 ... argN)
Value* builtin_list(const Value* v, Env* _) {
    assert(v->tag == LIST);
    List args = v->val.list;

    assert(args.len >= 1);

    List new = list_init();

    for (size_t i = 0; i < args.len; i++) {
        list_add(&new, args.values[i], true);
    }

    Value* ret = valuepool_alloc(&global_vp);
    ret->tag = LIST;
    ret->val.list = new;

    return ret;
}

typedef struct Test {
    char* input;
    char* output;
} Test;

#define VP_SIZE 1000

int main(int argc, char* argv[]) {
    /* char buf[4096] = {0}; */
    /* while (true) { */
    /*     printf("jisp> "); */
    /*     fgets(buf, sizeof(buf), stdin); */

    /*     if (buf[0] != '(') { */
    /*         continue; */
    /*     } */

    /*     Parser p = (Parser){.text = buf, .pos = 0}; */

    /*     Value v = parse(&p); */
    /* } */

    global_vp = valuepool_init((Value[VP_SIZE]){}, (bool[VP_SIZE]){}, VP_SIZE);

    Env global_env = env_init();
    env_put(&global_env, "+",
            &(Value){.tag = BUILTIN, .val.builtin = handle_add, .rc = 1});
    env_put(&global_env, "-",
            &(Value){.tag = BUILTIN, .val.builtin = handle_sub, .rc = 1});
    env_put(&global_env, "*",
            &(Value){.tag = BUILTIN, .val.builtin = handle_mul, .rc = 1});
    env_put(&global_env, "/",
            &(Value){.tag = BUILTIN, .val.builtin = handle_div, .rc = 1});
    env_put(&global_env, "%",
            &(Value){.tag = BUILTIN, .val.builtin = handle_mod, .rc = 1});
    env_put(&global_env, "<",
            &(Value){.tag = BUILTIN, .val.builtin = handle_lt, .rc = 1});
    env_put(&global_env, ">",
            &(Value){.tag = BUILTIN, .val.builtin = handle_gt, .rc = 1});
    env_put(&global_env, "=",
            &(Value){.tag = BUILTIN, .val.builtin = handle_eq, .rc = 1});
    env_put(&global_env,
            "<=", &(Value){.tag = BUILTIN, .val.builtin = handle_le, .rc = 1});
    env_put(&global_env,
            ">=", &(Value){.tag = BUILTIN, .val.builtin = handle_ge, .rc = 1});
    env_put(&global_env,
            "!=", &(Value){.tag = BUILTIN, .val.builtin = handle_ne, .rc = 1});
    env_put(&global_env, "symbol-eq",
            &(Value){.tag = BUILTIN, .val.builtin = symbol_eq, .rc = 1});
    env_put(&global_env, "string-eq",
            &(Value){.tag = BUILTIN, .val.builtin = string_eq, .rc = 1});
    env_put(&global_env, "display",
            &(Value){.tag = BUILTIN, .val.builtin = handle_display, .rc = 1});
    env_put(&global_env, "eval",
            &(Value){.tag = BUILTIN, .val.builtin = eval, .rc = 1});
    env_put(&global_env, "car",
            &(Value){.tag = BUILTIN, .val.builtin = car, .rc = 1});
    env_put(&global_env, "cdr",
            &(Value){.tag = BUILTIN, .val.builtin = cdr, .rc = 1});
    env_put(&global_env, "if",
            &(Value){.tag = SPECIAL_FORM, .val.builtin = handle_if, .rc = 1});
    env_put(
        &global_env, "define",
        &(Value){.tag = SPECIAL_FORM, .val.builtin = handle_define, .rc = 1});
    env_put(&global_env, "define-macro",
            &(Value){.tag = SPECIAL_FORM,
                     .val.builtin = handle_define_macro,
                     .rc = 1});
    env_put(&global_env, "and",
            &(Value){.tag = SPECIAL_FORM, .val.builtin = handle_and, .rc = 1});
    env_put(&global_env, "or",
            &(Value){.tag = SPECIAL_FORM, .val.builtin = handle_or, .rc = 1});
    env_put(
        &global_env, "progn",
        &(Value){.tag = SPECIAL_FORM, .val.builtin = handle_progn, .rc = 1});
    env_put(&global_env, "cond",
            &(Value){.tag = SPECIAL_FORM, .val.builtin = handle_cond, .rc = 1});
    env_put(&global_env, "t", &t);
    env_put(&global_env, "f", &f);
    env_put(&global_env, "nil", &nil);
    env_put(&global_env, "#nil", &type_nil);
    env_put(&global_env, "#number", &type_number);
    env_put(&global_env, "#string", &type_string);
    env_put(&global_env, "#boolean", &type_boolean);
    env_put(&global_env, "#procedure", &type_procedure);
    env_put(&global_env, "#special-form", &type_specialform);
    env_put(&global_env, "#symbol", &type_symbol);
    env_put(&global_env, "#list", &type_list);
    env_put(&global_env, "#macro", &type_macro);
    env_put(&global_env, "nil?",
            &(Value){.tag = BUILTIN, .val.builtin = builtin_nilp, .rc = 1});
    env_put(&global_env, "number?",
            &(Value){.tag = BUILTIN, .val.builtin = builtin_numberp, .rc = 1});
    env_put(&global_env, "string?",
            &(Value){.tag = BUILTIN, .val.builtin = builtin_stringp, .rc = 1});
    env_put(&global_env, "boolean?",
            &(Value){.tag = BUILTIN, .val.builtin = builtin_booleanp, .rc = 1});
    env_put(
        &global_env, "procedure?",
        &(Value){.tag = BUILTIN, .val.builtin = builtin_procedurep, .rc = 1});
    env_put(
        &global_env, "special-form?",
        &(Value){.tag = BUILTIN, .val.builtin = builtin_specialformp, .rc = 1});
    env_put(&global_env, "builtin?",
            &(Value){.tag = BUILTIN, .val.builtin = builtin_builtinp, .rc = 1});
    env_put(&global_env, "symbol?",
            &(Value){.tag = BUILTIN, .val.builtin = builtin_symbolp, .rc = 1});
    env_put(&global_env, "list?",
            &(Value){.tag = BUILTIN, .val.builtin = builtin_listp, .rc = 1});
    env_put(&global_env, "macro?",
            &(Value){.tag = BUILTIN, .val.builtin = builtin_macrop, .rc = 1});
    env_put(&global_env, "tag",
            &(Value){.tag = BUILTIN, .val.builtin = value_tag, .rc = 1});
    env_put(
        &global_env, "prepend",
        &(Value){.tag = BUILTIN, .val.builtin = builtin_list_prepend, .rc = 1});
    env_put(
        &global_env, "append",
        &(Value){.tag = BUILTIN, .val.builtin = builtin_list_append, .rc = 1});
    env_put(&global_env, "list",
            &(Value){.tag = BUILTIN, .val.builtin = builtin_list, .rc = 1});

    char* eq = "(define (eq a b)"
               "    (and"
               "     (symbol-eq (tag a) (tag b))"
               "     (cond"
               "       ((nil? a) t)"
               "       ((or (number? a) (boolean? a)) (= a b))"
               "       ((string? a) (string-eq a b))"
               "       ((or (list? a) (procedure? a) (macro? a)) (and (eq (car "
               "a) (car "
               "          b)) (eq (cdr a) (cdr b))))"
               "       ((symbol? a) (symbol-eq a b))"
               "       ((special-form? a) (nil))"
               "       (t f))))";

    {
        Parser input = (Parser){.text = eq, .pos = 0, .len = strlen(eq)};
        Value* parsed = parse(&input);
        Value* evaled = internal_eval(parsed, &global_env);

        value_deref(parsed);
        value_deref(evaled);
    }

    // TODO develop a value equals function
    Test tests[] = {
        (Test){.input = "(car '(1 2 3))", .output = "1"},
        (Test){.input = "(cdr '(1 2 3))", .output = "'(2 3)"},
        (Test){.input = "(eval '(+ 1 3))", .output = "4"},
        (Test){.input = "(display '(1 2 3))", .output = "'(1 2 3)"},
        (Test){.input =
                   "(+ 1 2 (+ 3 4) (/ 1 2) 5 (% 15.5 0.2690) (+ (+ 1 2)1))",
               .output = "19.667"},
        (Test){.input = "(if nil 1 2)", .output = "2"},
        (Test){.input = "(define x 42)", .output = "42"},
        (Test){.input = "(define (add1 x) (+ 1 x))", .output = "add1"},
        (Test){.input = "(add1 70)", .output = "71"},
        (Test){.input = "(define (sub1 x) (- x 1))", .output = "sub1"},
        (Test){.input = "(define (factorial x) (if (> x 1) (* x (factorial "
                        "(sub1 x))) 1))",
               .output = "factorial"},
        (Test){.input = "(factorial 5)", .output = "120"},
        (Test){.input = "(define (add a b) (+ a b))", .output = "add"},
        (Test){.input = "(add 1 2)", .output = "3"},
        (Test){.input = "(define (factorial-iter acc x) (if (> x 1) "
                        "(factorial-iter (* acc x) (sub1 x)) acc))",
               .output = "factorial-iter"},
        (Test){.input = "(factorial-iter 1 5)", .output = "120"},
        (Test){.input = "(and t t t)", .output = "t"},
        (Test){.input = "(and t t f)", .output = "f"},
        (Test){.input = "(define (not boolean) (if boolean f t))",
               .output = "not"},
        (Test){.input = "(or t t t)", .output = "t"},
        (Test){.input = "(or t t (+ nil nil))", .output = "t"},
        (Test){.input = "(progn (define y 45) (+ y 2))", .output = "47"},
        (Test){.input = "(not t)", .output = "f"},
        (Test){.input = "(not f)", .output = "t"},
        (Test){.input = "(cond (t 15) (f 42))", .output = "15"},
        (Test){.input = "(cond (f 15) (f 42))", .output = "nil"},
        (Test){.input = "(cond (f 15) (t 42))", .output = "42"},
        (Test){.input = "(cond (f 15) ((> 15 2) (add 1 y)) (t 42))",
               .output = "46"},
        (Test){.input = "(nil? nil)", .output = "t"},
        (Test){.input = "(nil? 5)", .output = "f"},
        (Test){.input = "(number? 5)", .output = "t"},
        (Test){.input = "(number? thing)", .output = "f"},
        (Test){.input = "(list? 5)", .output = "f"},
        (Test){.input = "(list? '(1 2 3))", .output = "t"},
        (Test){.input = "(tag 5)", .output = "#number"},
        (Test){.input = "(tag '(1 2 3))", .output = "#list"},
        (Test){.input = "(define symb 'a)", .output = "'a"},
        (Test){.input = "(symbol-eq symb 'a)", .output = "t"},
        (Test){.input = "(symbol-eq symb 'b)", .output = "f"},
        (Test){.input = "(eq 5 5)", .output = "t"},
        (Test){.input = "(eq '(1 2) '(1 2))", .output = "t"},
        (Test){.input = "(eq nil nil)", .output = "t"},
        (Test){.input = "(eq (= 1 1) (= 1 1))", .output = "t"},
        (Test){.input = "(boolean? t)", .output = "t"},
        (Test){.input = "(eq t t)", .output = "t"},
        (Test){.input = "(eq t f)", .output = "f"},
        (Test){.input = "(procedure? add1)", .output = "t"},
        (Test){.input = "(eq 'add1 'add1)", .output = "t"},
        (Test){.input = "(eq add1 add1)", .output = "t"},
        (Test){.input = "(eq (eq add1 add1) t)", .output = "t"},
        (Test){.input = "\"hi mom\"", .output = "\"hi mom\""},
        (Test){.input = "(define (reverse a) (if (cdr a) (append (reverse (cdr "
                        "a)) (car a)) a))",
               .output = "reverse"},
        (Test){.input = "(reverse '(1 2 3))", .output = "'(3 2 1)"},
        (Test){.input = "(list 3 2 1)", .output = "'(3 2 1)"},
        (Test){.input = "(list 3)", .output = "'(3)"},
        (Test){.input = "(if (cdr '(1)) t f)", .output = "f"},
        (Test){.input = "(if (cdr '(1 2)) t f)", .output = "t"},
        (Test){.input = "(define (apply func args) (eval (prepend args func)))",
               .output = "apply"},
        (Test){.input = "(apply '+ '(1 2 3))", .output = "'6"},
        (Test){.input = "(apply 'add1 '(1))", .output = "2"},
        (Test){.input = "(define (test-rest &rest args) args)",
               .output = "test-rest"},
        (Test){.input = "(test-rest 1 2 3)", .output = "'(1 2 3)"},
        (Test){.input = "(define (funcall func &rest args) (apply func args))",
               .output = "funcall"},
        (Test){.input = "(funcall '+ 1 2 3)", .output = "'6"},
        (Test){.input = "(funcall 'add1 1)", .output = "2"},
        (Test){.input = "(define (map func l) (if (cdr l) (prepend (map "
                        "func (cdr l)) (funcall func (car l))) (list "
                        "(apply func l))))",
               .output = "map"},
        (Test){.input = "(map 'add1 '(3 6 9))", .output = "'(4 7 10)"},
        (Test){
            .input =
                "(define (prepend-not-nil l x) (if (eq '(nil) (display x)) l "
                "(prepend l x)))",
            .output = "prepend-not-nil"},
        (Test){.input =
                   "(define (filter predicate l) (if (cdr l) (if (funcall "
                   "predicate (car l)) (prepend-not-nil (filter predicate (cdr "
                   "l)) (car l)) (filter predicate (cdr l))) (if (funcall "
                   "predicate (car l)) l '(nil))))",
               .output = "filter"},
        (Test){.input = "(filter 'number? '(3 \"hi\" 9))", .output = "'(3 9)"},
        (Test){.input = "(filter 'number? '(3 6 \"hi\"))", .output = "'(3 6)"},
        (Test){.input = "(define-macro (test a b) (list 'eq a b))",
               .output = "test"},
        (Test){.input = "(test (+ 5 2) (+ 6 1))", .output = "t"},
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(*tests); i++) {
        Parser input = (Parser){
            .text = tests[i].input, .pos = 0, .len = strlen(tests[i].input)};
        Parser output = (Parser){
            .text = tests[i].output, .pos = 0, .len = strlen(tests[i].output)};

        Value* parse_input = parse(&input);
        Value* parse_output = parse(&output);

        Value* eq_symbol = env_get(&global_env, "eq");
        Value* eq_proc = value_clone(eq_symbol);
        eq_proc->quoted++;
        value_deref(eq_symbol);

        List l = list_init();
        list_add(&l, eq_proc, false);
        list_add(&l, parse_input, false);
        list_add(&l, parse_output, false);
        Value* to_eval = valuepool_alloc(&global_vp);
        to_eval->tag = LIST;
        to_eval->val.list = l;

        Value* result = internal_eval(to_eval, &global_env);
        assert(result->tag == BOOLEAN);

        if (!result->val.boolean) {
            printf("Test %zu failed:\n", i);
            printf("\tInput:    %s\n", input.text);
            printf("\tExpected: %s\n", output.text);
            printf("\tActual:   ");
            Value* actual_output = internal_eval(parse_input, &global_env);
            value_print(actual_output);
            value_deref(actual_output);
            printf("\n");
            fflush(stdout);

            exit(1);
        } else {
            printf("Test %zu passed\n", i);
            fflush(stdout);
        }

        value_deref(to_eval);
    }

    /* env_print(&global_env); */
    env_deinit(&global_env);

    valuepool_deinit(&global_vp);
}
