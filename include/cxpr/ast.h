/**
 * @file ast.h
 * @brief Public AST API for cxpr.
 */

#ifndef CXPR_AST_H
#define CXPR_AST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cxpr_parser cxpr_parser;
typedef struct cxpr_ast cxpr_ast;
typedef struct cxpr_context cxpr_context;
typedef struct cxpr_registry cxpr_registry;
typedef struct cxpr_program cxpr_program;
typedef struct cxpr_error cxpr_error;
typedef struct cxpr_field_value cxpr_field_value;

/* ═══════════════════════════════════════════════════════════════════════════
 * Parser API
 * ═══════════════════════════════════════════════════════════════════════════ */

cxpr_parser* cxpr_parser_new(void);
void cxpr_parser_free(cxpr_parser* p);
cxpr_ast* cxpr_parse(cxpr_parser* p, const char* expression, cxpr_error* err);

/* ═══════════════════════════════════════════════════════════════════════════
 * AST Construction API
 * ═══════════════════════════════════════════════════════════════════════════ */

void cxpr_ast_free(cxpr_ast* ast);
cxpr_ast* cxpr_ast_new_number(double value);
cxpr_ast* cxpr_ast_new_bool(bool value);
cxpr_ast* cxpr_ast_new_identifier(const char* name);
cxpr_ast* cxpr_ast_new_variable(const char* name);
cxpr_ast* cxpr_ast_new_field_access(const char* object, const char* field);
cxpr_ast* cxpr_ast_new_producer_access(const char* name, cxpr_ast** args,
                                       size_t argc, const char* field);
cxpr_ast* cxpr_ast_new_binary_op(int op, cxpr_ast* left, cxpr_ast* right);
cxpr_ast* cxpr_ast_new_unary_op(int op, cxpr_ast* operand);
cxpr_ast* cxpr_ast_new_function_call(const char* name, cxpr_ast** args, size_t argc);
cxpr_ast* cxpr_ast_new_ternary(cxpr_ast* condition, cxpr_ast* true_branch,
                               cxpr_ast* false_branch);

/* ═══════════════════════════════════════════════════════════════════════════
 * AST Inspection API
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    CXPR_NODE_NUMBER,
    CXPR_NODE_BOOL,
    CXPR_NODE_IDENTIFIER,
    CXPR_NODE_VARIABLE,
    CXPR_NODE_FIELD_ACCESS,
    CXPR_NODE_CHAIN_ACCESS,
    CXPR_NODE_PRODUCER_ACCESS,
    CXPR_NODE_BINARY_OP,
    CXPR_NODE_UNARY_OP,
    CXPR_NODE_FUNCTION_CALL,
    CXPR_NODE_TERNARY
} cxpr_node_type;

cxpr_node_type cxpr_ast_type(const cxpr_ast* ast);
double cxpr_ast_number_value(const cxpr_ast* ast);
bool cxpr_ast_bool_value(const cxpr_ast* ast);
const char* cxpr_ast_identifier_name(const cxpr_ast* ast);
const char* cxpr_ast_variable_name(const cxpr_ast* ast);
const char* cxpr_ast_field_object(const cxpr_ast* ast);
const char* cxpr_ast_field_name(const cxpr_ast* ast);
size_t cxpr_ast_chain_depth(const cxpr_ast* ast);
const char* cxpr_ast_chain_segment(const cxpr_ast* ast, size_t index);
int cxpr_ast_operator(const cxpr_ast* ast);
const cxpr_ast* cxpr_ast_left(const cxpr_ast* ast);
const cxpr_ast* cxpr_ast_right(const cxpr_ast* ast);
const cxpr_ast* cxpr_ast_operand(const cxpr_ast* ast);
const char* cxpr_ast_function_name(const cxpr_ast* ast);
size_t cxpr_ast_function_argc(const cxpr_ast* ast);
const cxpr_ast* cxpr_ast_function_arg(const cxpr_ast* ast, size_t index);
const char* cxpr_ast_producer_name(const cxpr_ast* ast);
const char* cxpr_ast_producer_field(const cxpr_ast* ast);
size_t cxpr_ast_producer_argc(const cxpr_ast* ast);
const cxpr_ast* cxpr_ast_producer_arg(const cxpr_ast* ast, size_t index);
const cxpr_ast* cxpr_ast_ternary_condition(const cxpr_ast* ast);
const cxpr_ast* cxpr_ast_ternary_true_branch(const cxpr_ast* ast);
const cxpr_ast* cxpr_ast_ternary_false_branch(const cxpr_ast* ast);

/* ═══════════════════════════════════════════════════════════════════════════
 * AST Reference Extraction API
 * ═══════════════════════════════════════════════════════════════════════════ */

size_t cxpr_ast_references(const cxpr_ast* ast, const char** names, size_t max_names);
size_t cxpr_ast_functions_used(const cxpr_ast* ast, const char** names, size_t max_names);
size_t cxpr_ast_variables_used(const cxpr_ast* ast, const char** names, size_t max_names);

/* ═══════════════════════════════════════════════════════════════════════════
 * Evaluator API
 * ═══════════════════════════════════════════════════════════════════════════ */

cxpr_field_value cxpr_ast_eval(const cxpr_ast* ast, const cxpr_context* ctx,
                               const cxpr_registry* reg, cxpr_error* err);
double cxpr_ast_eval_double(const cxpr_ast* ast, const cxpr_context* ctx,
                            const cxpr_registry* reg, cxpr_error* err);
bool cxpr_ast_eval_bool(const cxpr_ast* ast, const cxpr_context* ctx,
                        const cxpr_registry* reg, cxpr_error* err);

/* ═══════════════════════════════════════════════════════════════════════════
 * Compiled Program API
 * ═══════════════════════════════════════════════════════════════════════════ */

cxpr_program* cxpr_compile(const cxpr_ast* ast, const cxpr_registry* reg, cxpr_error* err);
cxpr_field_value cxpr_ir_eval(const cxpr_program* prog, const cxpr_context* ctx,
                              const cxpr_registry* reg, cxpr_error* err);
double cxpr_ir_eval_double(const cxpr_program* prog, const cxpr_context* ctx,
                           const cxpr_registry* reg, cxpr_error* err);
bool cxpr_ir_eval_bool(const cxpr_program* prog, const cxpr_context* ctx,
                       const cxpr_registry* reg, cxpr_error* err);
void cxpr_program_free(cxpr_program* prog);
void cxpr_program_dump(const cxpr_program* prog, FILE* out);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_AST_H */
