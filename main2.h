#pragma once

#include <stdbool.h>
#include <stddef.h>
struct Value;
struct Env;

typedef struct Value* (*specialform)(struct Value*, struct Env*);

typedef struct Cons {
    struct Value* car;
    struct Value* cdr;
} Cons;

typedef enum Tag {
    NUMBER,
    POINTER,
    SYMBOL,
    CONS,
} Tag;

typedef struct Value {
    Tag tag;
    union {
        double number;
        char* string;
        Cons cons;
        void* pointer;
    } val;
    int rc;
    int quoted;
} Value;

void value_ref(Value* v);
void value_deref(Value* v);
Value* value_clone(const Value* v);
void _value_print(const Value* v);
void value_print(const Value* v);
bool value_isnil(const Value* v);
bool value_truthy(const Value* v);
specialform value_isspecialform(const Value* v);

Value* _cons(Value*, Value*, bool);
Value* _car(Value* v, struct Env*);
Value* _cdr(Value* v, struct Env*);

bool _symbol_eq(const Value*, const Value*);
Value* symbol_eq(Value* v, struct Env*);

typedef struct ValuePool {
    Value* values;
    bool* used;
    size_t in_use;
    size_t cap;
    size_t high_water;
} ValuePool;

Value* valuepool_alloc(ValuePool* vp);
void valuepool_free(ValuePool* vp, Value* v);
void valuepool_deinit(const ValuePool* vp);
void valuepool_print(const ValuePool* vp);

typedef struct Env {
    char** keys;
    Value** vals;

    size_t len;
    size_t cap;

    struct Env* parent;
} Env;

Env env_init(size_t size, Env* parent);
void env_deinit(Env* e);
Value* env_get(const Env* e, const char* key);
const Value* env_get_const(const Env* e, const char* key);
void env_put(Env* e, const char* key, Value* val, bool increase_ref);

typedef struct Parser {
    const char* text;
    size_t pos;
    size_t len;
} Parser;

Value* parse(Parser* p, Env* e);
char parser_peek(Parser* p);
char parser_get(Parser* p);
void parser_skip_whitespace(Parser* p);

/********/
/* Eval */
/********/
Value* _eval(Value* v, Env* env);
Value* eval_define(Value* v, Env* env);
Value* eval_progn(Value* v, Env* env);
Value* eval_cond(Value* v, Env* env);

/************/
/* Builtins */
/************/
typedef Value* (*builtin)(Env*);

Value* plus(Env*);
Value* eq(Env*);

Value* car(Env*);
Value* cdr(Env*);
Value* cons(Env*);

Value* eval(Env*);
