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
    char *text;
    size_t pos;
} Parser;

char parser_peek(const Parser *p) { return p->text[p->pos]; }
char parser_get(Parser *p) { return p->text[p->pos++]; }
void parser_skip_whitespace(Parser *p) {
    while (p->text[p->pos] == ' ') {
        p->pos++;
    }
}

struct Value;

typedef struct List {
    struct Value *values;
    size_t cap;
    size_t len;
} List;

typedef struct Value {
    enum {
        NUMBER,
        STRING,
        BOOLEAN,
        PROCEDURE,
        SYMBOL,
        QUOTED_SYMBOL,
        LIST
    } tag;
    union {
        double number;
        char *string;
        bool boolean;
        List list;
    } val;
} Value;

void value_print(Value v) {
    switch (v.tag) {
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
        assert(false);
        break;
    case SYMBOL:
        printf("%s", v.val.string);
        break;
    case QUOTED_SYMBOL:
        printf("'%s", v.val.string);
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

void value_dump(Value v) {
    switch (v.tag) {
    case NUMBER:
        printf("{tag: number, val: %g}", v.val.number);
        break;
    case STRING:
        printf("{tag: string, val: \"%s\"}", v.val.string);
        break;
    case BOOLEAN:
        printf("{tag: boolean, val: %s}", v.val.boolean ? "t" : "f");
        break;
    case PROCEDURE:
        assert(false);
        break;
    case SYMBOL:
        printf("{tag: symbol, val: %s}", v.val.string);
        break;
    case QUOTED_SYMBOL:
        printf("{tag: quoted_symbol, val: '%s}", v.val.string);
        break;
    case LIST:
        printf("{tag: list, val: (");
        for (size_t i = 0; i < v.val.list.len; i++) {
            value_dump(v.val.list.values[i]);

            if (i != v.val.list.len - 1) {
                printf(" ");
            }
        }
        printf(")}");
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

void list_add(List *l, Value v) {
    if (l->len >= l->cap) {
        l->cap *= 2;
        l->values = realloc(l->values, l->cap * sizeof(*l->values));
    }

    l->values[l->len++] = v;
}

void list_deinit(List *l) {
    if (l && l->values) {
        for (size_t i = 0; i < l->len; i++) {
            if (l->values[i].tag == LIST) {
                list_deinit(&l->values[i].val.list);
            } else if (l->values[i].tag == STRING ||
                       l->values[i].tag == SYMBOL ||
                       l->values[i].tag == QUOTED_SYMBOL) {
                free(l->values[i].val.string);
            }
        }
        free(l->values);
    }
}

Value t = (Value){.tag = BOOLEAN, .val.boolean = true};
Value f = (Value){.tag = BOOLEAN, .val.boolean = false};

Value parse(Parser *input);

Value handle_if(Parser *input) { return (Value){}; }
Value handle_and(Parser *input) { return (Value){}; }
Value handle_or(Parser *input) { return (Value){}; }
Value handle_lambda(Parser *input) { return (Value){}; }
Value handle_let(Parser *input) { return (Value){}; }

typedef enum BinaryOps {
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
} BinaryOps;

Value handle_arithmetic(Parser *input, BinaryOps op) {
    parser_skip_whitespace(input);

    Value first = parse(input);
    assert(first.tag == NUMBER);

    while (parser_peek(input) != ')') {
        Value next = parse(input);
        assert(first.tag == NUMBER);

        switch (op) {
        case ADD:
            first.val.number += next.val.number;
            break;
        case SUB:
            first.val.number -= next.val.number;
            break;
        case MUL:
            first.val.number *= next.val.number;
            break;
        case DIV:
            first.val.number /= next.val.number;
            break;
        case MOD:
            first.val.number = fmod(first.val.number, next.val.number);
            break;
        }
    }

    return first;
}

Value handle_add(Parser *input) { return handle_arithmetic(input, ADD); }
Value handle_sub(Parser *input) { return handle_arithmetic(input, SUB); }
Value handle_mul(Parser *input) { return handle_arithmetic(input, MUL); }
Value handle_div(Parser *input) { return handle_arithmetic(input, DIV); }
Value handle_mod(Parser *input) { return handle_arithmetic(input, MOD); }

Value parse(Parser *input) {
    parser_skip_whitespace(input);
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

int main(int argc, char *argv[]) {
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

    {
        Parser p =
            (Parser){.text = "(+ 1 2 (+ 3 4 (if t 4 '(5 6 7))))", .pos = 0};

        Value v = parse(&p);
        value_print(v);
        printf("\n");
        value_dump(v);
        printf("\n");
        if (v.tag == LIST) {
            list_deinit(&v.val.list);
        }
        /* assert(v.tag == NUMBER && v.val.number == 3.0); */
    }
}
