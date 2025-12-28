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
    struct Value* values;
    size_t cap;
    size_t len;
} List;

struct Env;

typedef struct Value (*builtin_procedure)(const List*, struct Env* e);

typedef struct Value {
    enum {
        NIL,
        NUMBER,
        STRING,
        BOOLEAN,
        PROCEDURE,
        BUILTIN,
        SYMBOL,
        LIST,
    } tag;
    union {
        double number;
        char* string;
        bool boolean;
        List list;
        builtin_procedure builtin;
    } val;
    int quoted;
} Value;

void list_deinit(List*);

void value_deinit(Value* v) {
    if (v->tag == STRING || v->tag == SYMBOL) {
        free(v->val.string);
    } else if (v->tag == LIST || v->tag == PROCEDURE) {
        list_deinit(&v->val.list);
    }
}

void value_print(Value v) {
    for (int i = 0; i < v.quoted; i++) {
        putchar('\'');
    }

    switch (v.tag) {
    case NIL:
        printf("nil");
        break;
    case NUMBER:
        printf("%g", v.val.number);
        break;
    case STRING:
        printf("\"%s\"", v.val.string);
        break;
    case BOOLEAN:
        printf("%s", v.val.boolean ? "t" : "f");
        break;
    case PROCEDURE:
    case BUILTIN:
        assert(false);
        break;
    case SYMBOL:
        printf("%s", v.val.string);
        break;
    case LIST:
        printf("(");
        for (size_t i = 0; i < v.val.list.len; i++) {
            value_print(v.val.list.values[i]);

            if (i != v.val.list.len - 1) {
                printf(" ");
            }
        }
        printf(")");
        break;
    }
}

List list_init() {
    return (List){
        .values = calloc(3, sizeof(Value)),
        .cap = 3,
        .len = 0,
    };
}

void list_add(List* l, Value v) {
    if (l->len >= l->cap) {
        l->cap *= 2;
        l->values = realloc(l->values, l->cap * sizeof(*l->values));
    }

    l->values[l->len++] = v;
}

void list_deinit(List* l) {
    if (l && l->values) {
        for (size_t i = 0; i < l->len; i++) {
            if (l->values[i].tag == LIST) {
                list_deinit(&l->values[i].val.list);
            } else if (l->values[i].tag == STRING ||
                       l->values[i].tag == SYMBOL) {
                free(l->values[i].val.string);
            }
        }
        free(l->values);
    }
}

const Value t = (Value){.tag = BOOLEAN, .val.boolean = true};
const Value f = (Value){.tag = BOOLEAN, .val.boolean = false};
const Value nil = (Value){.tag = NIL};

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
            value_deinit(e->vals[i]);
        }
    }
}

const Value* env_get(const Env* e, const char* symbol) {
    if (!e) {
        return &nil;
    }

    for (size_t i = 0; i < e->len; i++) {
        if (!strcmp(symbol, e->keys[i])) {
            return e->vals[i];
        }
    }

    return env_get(e->parent, symbol);
}

void env_put(Env* e, const char* symbol, Value* v) {
    for (size_t i = 0; i < e->len; i++) {
        if (!strcmp(symbol, e->keys[i])) {
            // If we find a duplicate key, replace the old value
            e->vals[i] = v;
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
    e->len++;
}

void env_print(const Env* e) {
    for (size_t i = 0; i < e->len; i++) {
        printf("%10s --> ", e->keys[i]);
        value_print(*e->vals[i]);
        printf("\n");
    }
}

Value parse(Parser* input);
Value eval(const Value* v, Env* e) {
    if (v->quoted) {
        // TODO we need a value_copy function
        Value ret = *v;
        ret.quoted--;

        return ret;
    } else if (v->tag == LIST) {
        List l = v->val.list;
        Value procedure = eval(&l.values[0], e);

        if (procedure.tag == BUILTIN) {
            return procedure.val.builtin(&l, e);
        }
    } else if (v->tag == SYMBOL) {
        return *env_get(e, v->val.string);
    } else if (v->tag == NUMBER) {
        return *v;
    }

    return nil;
}

Value handle_if(Parser* input) { return (Value){}; }
Value handle_and(Parser* input) { return (Value){}; }
Value handle_or(Parser* input) { return (Value){}; }
Value handle_lambda(Parser* input) { return (Value){}; }
Value handle_let(Parser* input) { return (Value){}; }

Value handle_arithmetic(const List* l, Env* e) {
    if (l->values[0].tag != SYMBOL) {
        assert(false);
    }

    Value op = l->values[0];
    if (op.val.string[0] != '+' && op.val.string[0] != '-' &&
        op.val.string[0] != '*' && op.val.string[0] != '/' &&
        op.val.string[0] != '%') {
        assert(false);
    }

    Value first = eval(l->values + 1, e);
    assert(first.tag == NUMBER);

    for (size_t i = 2; i < l->len; i++) {
        Value next = eval(l->values + i, e);
        assert(first.tag == NUMBER);

        switch (op.val.string[0]) {
        case '+':
            first.val.number += next.val.number;
            break;
        case '-':
            first.val.number -= next.val.number;
            break;
        case '*':
            first.val.number *= next.val.number;
            break;
        case '/':
            first.val.number /= next.val.number;
            break;
        case '%':
            first.val.number = fmod(first.val.number, next.val.number);
            break;
        }
    }

    return first;
}

Value parse(Parser* input) {
    parser_skip_whitespace(input);
    if (parser_peek(input) == '\'') {
        // Parse expression into value, return it quoted
        parser_get(input);
        Value v = parse(input);
        v.quoted += 1;

        return v;
    }
    if (parser_peek(input) != '(') {
        // Entering from a recursive parse call
        // The expression here should be a literal
        // Either a symbol literal, number literal, string literal, char
        // literal, quoted value

        char buf[256] = {0};
        for (int i = 0; parser_peek(input) != ' ' && parser_peek(input) != ')';
             i++) {
            buf[i] = parser_get(input);
        }

        if (isdigit(buf[0])) {
            // Parse number
            return (Value){
                .tag = NUMBER,
                .val.number = strtod(buf, NULL),
            };
        } else {
            // Symbol
            return (Value){
                .tag = SYMBOL,
                .val.string = strdup(buf),
            };
        }
    }

    assert(parser_peek(input) == '(');
    parser_get(input);

    // If we make it here then the first symbol must be a function
    // Start of a list
    // (+ 1 2 (+ 3 4))
    //  ^^^
    List l = list_init();

    Value v = parse(input);
    list_add(&l, v);

    while (parser_peek(input) != ')') {
        v = parse(input);
        list_add(&l, v);
    }

    return (Value){.tag = LIST, .val.list = l};
}

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

    Env global_env = env_init();
    env_put(&global_env, "+",
            &(Value){.tag = BUILTIN, .val.builtin = handle_arithmetic});
    env_put(&global_env, "-",
            &(Value){.tag = BUILTIN, .val.builtin = handle_arithmetic});
    env_put(&global_env, "*",
            &(Value){.tag = BUILTIN, .val.builtin = handle_arithmetic});
    env_put(&global_env, "/",
            &(Value){.tag = BUILTIN, .val.builtin = handle_arithmetic});
    env_put(&global_env, "%",
            &(Value){.tag = BUILTIN, .val.builtin = handle_arithmetic});

    {
        Parser p = (Parser){.text = "(+ 1 2 (+ 3 4))", .pos = 0};

        Value v = parse(&p);
        printf("Parse results: ");
        value_print(v);
        printf("\n");

        Value result = eval(&v, &global_env);
        printf("Eval results: ");
        value_print(result);

        if (v.tag == LIST) {
            list_deinit(&v.val.list);
        }

        /* assert(result.tag == NUMBER && result.val.number == 10.0); */
    }
}
