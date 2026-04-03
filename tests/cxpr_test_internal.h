/**
 * @file cxpr_test_internal.h
 * @brief Narrow internal surface used by cxpr's white-box tests.
 *
 * This header exists to keep tests from depending on the entire src/internal.h
 * implementation header. It exposes only the internal lexer, IR, and formula
 * engine details currently exercised by tests.
 */

#ifndef CXPR_TEST_INTERNAL_H
#define CXPR_TEST_INTERNAL_H

#include <cxpr/cxpr.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CXPR_TOK_NUMBER,
    CXPR_TOK_IDENTIFIER,
    CXPR_TOK_VARIABLE,
    CXPR_TOK_TRUE,
    CXPR_TOK_FALSE,
    CXPR_TOK_STRING,
    CXPR_TOK_PLUS,
    CXPR_TOK_MINUS,
    CXPR_TOK_STAR,
    CXPR_TOK_SLASH,
    CXPR_TOK_PERCENT,
    CXPR_TOK_POWER,
    CXPR_TOK_EQ,
    CXPR_TOK_NEQ,
    CXPR_TOK_LT,
    CXPR_TOK_GT,
    CXPR_TOK_LTE,
    CXPR_TOK_GTE,
    CXPR_TOK_AND,
    CXPR_TOK_OR,
    CXPR_TOK_NOT,
    CXPR_TOK_LPAREN,
    CXPR_TOK_RPAREN,
    CXPR_TOK_COMMA,
    CXPR_TOK_DOT,
    CXPR_TOK_QUESTION,
    CXPR_TOK_COLON,
    CXPR_TOK_EOF,
    CXPR_TOK_ERROR
} cxpr_token_type;

typedef struct {
    cxpr_token_type type;
    const char* start;
    size_t length;
    double number_value;
    size_t position;
    size_t line;
    size_t column;
} cxpr_token;

typedef struct {
    const char* source;
    const char* current;
    size_t line;
    size_t column;
    size_t position;
} cxpr_lexer;

void cxpr_lexer_init(cxpr_lexer* lexer, const char* source);
cxpr_token cxpr_lexer_next(cxpr_lexer* lexer);
cxpr_token cxpr_lexer_peek(cxpr_lexer* lexer);

typedef enum {
    CXPR_OP_PUSH_CONST,
    CXPR_OP_PUSH_BOOL,
    CXPR_OP_LOAD_LOCAL,
    CXPR_OP_LOAD_LOCAL_SQUARE,
    CXPR_OP_LOAD_VAR,
    CXPR_OP_LOAD_VAR_SQUARE,
    CXPR_OP_LOAD_PARAM,
    CXPR_OP_LOAD_PARAM_SQUARE,
    CXPR_OP_LOAD_FIELD,
    CXPR_OP_LOAD_FIELD_SQUARE,
    CXPR_OP_LOAD_CHAIN,
    CXPR_OP_ADD,
    CXPR_OP_SUB,
    CXPR_OP_MUL,
    CXPR_OP_SQUARE,
    CXPR_OP_DIV,
    CXPR_OP_MOD,
    CXPR_OP_CMP_EQ,
    CXPR_OP_CMP_NEQ,
    CXPR_OP_CMP_LT,
    CXPR_OP_CMP_LTE,
    CXPR_OP_CMP_GT,
    CXPR_OP_CMP_GTE,
    CXPR_OP_NOT,
    CXPR_OP_NEG,
    CXPR_OP_SIGN,
    CXPR_OP_SQRT,
    CXPR_OP_ABS,
    CXPR_OP_FLOOR,
    CXPR_OP_CEIL,
    CXPR_OP_ROUND,
    CXPR_OP_POW,
    CXPR_OP_CLAMP,
    CXPR_OP_CALL_PRODUCER,
    CXPR_OP_CALL_PRODUCER_CONST,
    CXPR_OP_CALL_PRODUCER_CONST_FIELD,
    CXPR_OP_GET_FIELD,
    CXPR_OP_CALL_UNARY,
    CXPR_OP_CALL_BINARY,
    CXPR_OP_CALL_TERNARY,
    CXPR_OP_CALL_FUNC,
    CXPR_OP_CALL_DEFINED,
    CXPR_OP_CALL_AST,
    CXPR_OP_JUMP,
    CXPR_OP_JUMP_IF_FALSE,
    CXPR_OP_JUMP_IF_TRUE,
    CXPR_OP_RETURN
} cxpr_opcode;

struct cxpr_func_entry;

typedef struct {
    cxpr_opcode op;
    const char* name;
    const char* aux_name;
    const void* payload;
    const struct cxpr_func_entry* func;
    union {
        double value;
        unsigned long hash;
        size_t index;
        const cxpr_ast* ast;
    };
} cxpr_ir_instr;

typedef struct {
    const cxpr_context* request_ctx;
    const cxpr_context* owner_ctx;
    void* entries_base;
    size_t slot;
    unsigned long shadow_version;
} cxpr_ir_lookup_cache;

typedef struct {
    cxpr_ir_instr* code;
    size_t count;
    size_t capacity;
    const cxpr_ast* ast;
    cxpr_ir_lookup_cache* lookup_cache;
    unsigned char fast_result_kind;
} cxpr_ir_program;

_Static_assert(sizeof(cxpr_ir_instr) <= 48, "cxpr_ir_instr too large");

typedef struct {
    char* name;
    cxpr_func_ptr sync_func;
    cxpr_struct_producer_ptr struct_producer;
    enum {
        CXPR_NATIVE_KIND_NONE = 0,
        CXPR_NATIVE_KIND_NULLARY,
        CXPR_NATIVE_KIND_UNARY,
        CXPR_NATIVE_KIND_BINARY,
        CXPR_NATIVE_KIND_TERNARY
    } native_kind;
    union {
        double (*nullary)(void);
        double (*unary)(double);
        double (*binary)(double, double);
        double (*ternary)(double, double, double);
    } native_scalar;
    size_t min_args;
    size_t max_args;
    void* userdata;
    cxpr_userdata_free_fn userdata_free;
    char** struct_fields;
    size_t fields_per_arg;
    size_t struct_argc;
    cxpr_ast* defined_body;
    cxpr_program* defined_program;
    bool defined_program_failed;
    char** defined_param_names;
    size_t defined_param_count;
    char*** defined_param_fields;
    size_t* defined_param_field_counts;
} cxpr_func_entry;

cxpr_func_entry* cxpr_registry_find(const cxpr_registry* reg, const char* name);

bool cxpr_ir_compile(const cxpr_ast* ast, const cxpr_registry* reg,
                     cxpr_ir_program* program, cxpr_error* err);
double cxpr_ir_exec(const cxpr_ir_program* program, const cxpr_context* ctx,
                    const cxpr_registry* reg, cxpr_error* err);
void cxpr_ir_program_reset(cxpr_ir_program* program);

typedef struct {
    char* name;
    char* expression;
    cxpr_ast* ast;
    cxpr_program* program;
    cxpr_field_value result;
    bool evaluated;
} cxpr_formula_entry;

struct cxpr_formula_engine {
    cxpr_formula_entry* formulas;
    size_t capacity;
    size_t count;
    size_t* eval_order;
    size_t eval_order_count;
    bool compiled;
    const cxpr_registry* registry;
    cxpr_parser* parser;
};

#ifdef __cplusplus
}
#endif

#endif
