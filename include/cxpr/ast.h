/**
 * @file ast.h
 * @brief Public AST API for cxpr.
 */

#ifndef CXPR_AST_H
#define CXPR_AST_H

#include <cxpr/types.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Parser API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Create a parser instance.
 * @return Newly allocated parser, or NULL on allocation failure.
 */
cxpr_parser* cxpr_parser_new(void);
/**
 * @brief Free a parser instance.
 * @param p Parser to free. May be NULL.
 */
void cxpr_parser_free(cxpr_parser* p);
/**
 * @brief Parse an expression string into an AST.
 * @param p Parser instance to use.
 * @param expression NUL-terminated expression source.
 * @param err Optional error output.
 * @return Newly allocated AST on success, or NULL on parse failure.
 */
cxpr_ast* cxpr_parse(cxpr_parser* p, const char* expression, cxpr_error* err);

/* ═══════════════════════════════════════════════════════════════════════════
 * AST Construction API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Free an AST and all owned descendants.
 * @param ast AST to free. May be NULL.
 */
void cxpr_ast_free(cxpr_ast* ast);
/**
 * @brief Construct a numeric literal node.
 * @param value Literal numeric value.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_number(double value);
/**
 * @brief Construct a boolean literal node.
 * @param value Literal boolean value.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_bool(bool value);
/**
 * @brief Construct a plain identifier node.
 * @param name Identifier name.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_identifier(const char* name);
/**
 * @brief Construct a `$param` variable node.
 * @param name Parameter name without `$`.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_variable(const char* name);
/**
 * @brief Construct a dotted field-access node.
 * @param object Root object or prefix name.
 * @param field Field name to access.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_field_access(const char* object, const char* field);
/**
 * @brief Construct a producer-field access node.
 * @param name Producer name.
 * @param args Producer argument array.
 * @param argc Number of producer arguments.
 * @param field Field name selected from the produced struct.
 * @return Newly allocated AST node taking ownership of `args`, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_producer_access(const char* name, cxpr_ast** args,
                                       size_t argc, const char* field);
/**
 * @brief Construct a binary operator node.
 * @param op Internal operator token.
 * @param left Left operand.
 * @param right Right operand.
 * @return Newly allocated AST node taking ownership of `left` and `right`, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_binary_op(int op, cxpr_ast* left, cxpr_ast* right);
/**
 * @brief Construct a unary operator node.
 * @param op Internal operator token.
 * @param operand Operand expression.
 * @return Newly allocated AST node taking ownership of `operand`, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_unary_op(int op, cxpr_ast* operand);
/**
 * @brief Construct a function-call node.
 * @param name Function name.
 * @param args Argument array.
 * @param argc Number of arguments.
 * @return Newly allocated AST node taking ownership of `args`, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_function_call(const char* name, cxpr_ast** args, size_t argc);
/**
 * @brief Construct a postfix lookback node.
 * @param target Expression being indexed.
 * @param index Lookback/index expression.
 * @return Newly allocated AST node taking ownership of both children, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_lookback(cxpr_ast* target, cxpr_ast* index);
/**
 * @brief Construct a ternary conditional node.
 * @param condition Condition expression.
 * @param true_branch Branch used when `condition` is true.
 * @param false_branch Branch used when `condition` is false.
 * @return Newly allocated AST node taking ownership of all children, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_ternary(cxpr_ast* condition, cxpr_ast* true_branch,
                               cxpr_ast* false_branch);

/* ═══════════════════════════════════════════════════════════════════════════
 * AST Inspection API
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    CXPR_NODE_NUMBER,
    CXPR_NODE_BOOL,
    CXPR_NODE_STRING,
    CXPR_NODE_IDENTIFIER,
    CXPR_NODE_VARIABLE,
    CXPR_NODE_FIELD_ACCESS,
    CXPR_NODE_CHAIN_ACCESS,
    CXPR_NODE_PRODUCER_ACCESS,
    CXPR_NODE_BINARY_OP,
    CXPR_NODE_UNARY_OP,
    CXPR_NODE_FUNCTION_CALL,
    CXPR_NODE_LOOKBACK,
    CXPR_NODE_TERNARY
} cxpr_node_type;

typedef enum {
    CXPR_EXPR_UNKNOWN = 0,
    CXPR_EXPR_BOOL,
    CXPR_EXPR_NUMBER,
    CXPR_EXPR_STRUCT
} cxpr_expr_type;

typedef struct {
    cxpr_expr_type result_type;          /**< Best-effort root result type of the expression. */
    bool is_constant;                    /**< True if the expression depends on no runtime inputs or parameters. */
    bool is_predicate;                   /**< True if the root expression evaluates to a boolean predicate. */
    bool uses_variables;                 /**< True if plain identifier/context lookups such as `rsi` are used. */
    bool uses_parameters;                /**< True if `$param` lookups are used. */
    bool uses_functions;                 /**< True if function or producer calls appear in the AST. */
    bool uses_expressions;                  /**< True if semantic analysis resolved at least one registry-defined expression. */
    bool uses_field_access;              /**< True if dotted or producer-style field access appears in the AST. */
    bool can_short_circuit;              /**< True if evaluation may short-circuit (`and`, `or`, ternary). */
    unsigned node_count;                 /**< Total number of AST nodes in the expression tree. */
    unsigned max_depth;                  /**< Maximum AST depth, with the root counted as depth 1. */
    size_t reference_count;              /**< Unique runtime references used by the AST: plain identifiers and full field paths. */
    size_t function_count;               /**< Unique function or producer names referenced by the AST. */
    size_t parameter_count;              /**< Unique `$param` names referenced by the AST. */
    size_t field_path_count;             /**< Unique dotted or field-style reference paths. */
    bool has_unknown_functions;          /**< True if registry-backed analysis found unresolved calls. */
    const char* first_unknown_function;  /**< First unresolved function/producer name, or NULL if none. */
} cxpr_analysis;

/**
 * @brief Return the node kind for an AST node.
 * @param ast AST node to inspect.
 * @return Node tag for `ast`.
 */
cxpr_node_type cxpr_ast_type(const cxpr_ast* ast);
/**
 * @brief Return the numeric payload of a number literal node.
 * @param ast Number node to inspect.
 * @return Literal numeric value.
 */
double cxpr_ast_number_value(const cxpr_ast* ast);
/**
 * @brief Return the boolean payload of a boolean literal node.
 * @param ast Boolean node to inspect.
 * @return Literal boolean value.
 */
bool cxpr_ast_bool_value(const cxpr_ast* ast);
/**
 * @brief Return the string payload of a string literal node.
 * @param ast String node to inspect.
 * @return Borrowed NUL-terminated string value, or NULL if `ast` is not a string node.
 */
const char* cxpr_ast_string_value(const cxpr_ast* ast);
/**
 * @brief Return the identifier name for an identifier node.
 * @param ast Identifier node to inspect.
 * @return Borrowed identifier name.
 */
const char* cxpr_ast_identifier_name(const cxpr_ast* ast);
/**
 * @brief Return the parameter name for a variable node.
 * @param ast Variable node to inspect.
 * @return Borrowed parameter name without `$`.
 */
const char* cxpr_ast_variable_name(const cxpr_ast* ast);
/**
 * @brief Return the object name for a field-access node.
 * @param ast Field-access node to inspect.
 * @return Borrowed object or prefix name.
 */
const char* cxpr_ast_field_object(const cxpr_ast* ast);
/**
 * @brief Return the leaf field name for a field-access node.
 * @param ast Field-access node to inspect.
 * @return Borrowed field name.
 */
const char* cxpr_ast_field_name(const cxpr_ast* ast);
/**
 * @brief Return the number of segments in a chain-access node.
 * @param ast Chain-access node to inspect.
 * @return Segment count.
 */
size_t cxpr_ast_chain_depth(const cxpr_ast* ast);
/**
 * @brief Return one segment from a chain-access node.
 * @param ast Chain-access node to inspect.
 * @param index Zero-based segment index.
 * @return Borrowed segment name, or NULL if `index` is out of range.
 */
const char* cxpr_ast_chain_segment(const cxpr_ast* ast, size_t index);
/**
 * @brief Return the internal operator token for an operator node.
 * @param ast Unary or binary operator node.
 * @return Internal operator token.
 */
int cxpr_ast_operator(const cxpr_ast* ast);
/**
 * @brief Return the left child of a binary operator node.
 * @param ast Binary operator node.
 * @return Borrowed left child, or NULL when not applicable.
 */
const cxpr_ast* cxpr_ast_left(const cxpr_ast* ast);
/**
 * @brief Return the right child of a binary operator node.
 * @param ast Binary operator node.
 * @return Borrowed right child, or NULL when not applicable.
 */
const cxpr_ast* cxpr_ast_right(const cxpr_ast* ast);
/**
 * @brief Return the operand of a unary operator node.
 * @param ast Unary operator node.
 * @return Borrowed operand, or NULL when not applicable.
 */
const cxpr_ast* cxpr_ast_operand(const cxpr_ast* ast);
/**
 * @brief Return the function name for a function-call node.
 * @param ast Function-call node.
 * @return Borrowed function name.
 */
const char* cxpr_ast_function_name(const cxpr_ast* ast);
/**
 * @brief Return the argument count for a function-call node.
 * @param ast Function-call node.
 * @return Number of arguments.
 */
size_t cxpr_ast_function_argc(const cxpr_ast* ast);
/**
 * @brief Return one argument from a function-call node.
 * @param ast Function-call node.
 * @param index Zero-based argument index.
 * @return Borrowed argument node, or NULL if `index` is out of range.
 */
const cxpr_ast* cxpr_ast_function_arg(const cxpr_ast* ast, size_t index);
/**
 * @brief Return the target child of a lookback node.
 * @param ast Lookback node.
 * @return Borrowed target child, or NULL when not applicable.
 */
const cxpr_ast* cxpr_ast_lookback_target(const cxpr_ast* ast);
/**
 * @brief Return the index child of a lookback node.
 * @param ast Lookback node.
 * @return Borrowed index child, or NULL when not applicable.
 */
const cxpr_ast* cxpr_ast_lookback_index(const cxpr_ast* ast);
/**
 * @brief Return the producer name for a producer-access node.
 * @param ast Producer-access node.
 * @return Borrowed producer name.
 */
const char* cxpr_ast_producer_name(const cxpr_ast* ast);
/**
 * @brief Return the selected field name for a producer-access node.
 * @param ast Producer-access node.
 * @return Borrowed field name.
 */
const char* cxpr_ast_producer_field(const cxpr_ast* ast);
/**
 * @brief Return the argument count for a producer-access node.
 * @param ast Producer-access node.
 * @return Number of arguments.
 */
size_t cxpr_ast_producer_argc(const cxpr_ast* ast);
/**
 * @brief Return one argument from a producer-access node.
 * @param ast Producer-access node.
 * @param index Zero-based argument index.
 * @return Borrowed argument node, or NULL if `index` is out of range.
 */
const cxpr_ast* cxpr_ast_producer_arg(const cxpr_ast* ast, size_t index);
/**
 * @brief Return the condition child of a ternary node.
 * @param ast Ternary node.
 * @return Borrowed condition child, or NULL when not applicable.
 */
const cxpr_ast* cxpr_ast_ternary_condition(const cxpr_ast* ast);
/**
 * @brief Return the true-branch child of a ternary node.
 * @param ast Ternary node.
 * @return Borrowed true-branch child, or NULL when not applicable.
 */
const cxpr_ast* cxpr_ast_ternary_true_branch(const cxpr_ast* ast);
/**
 * @brief Return the false-branch child of a ternary node.
 * @param ast Ternary node.
 * @return Borrowed false-branch child, or NULL when not applicable.
 */
const cxpr_ast* cxpr_ast_ternary_false_branch(const cxpr_ast* ast);

/* ═══════════════════════════════════════════════════════════════════════════
 * AST Reference Extraction API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Collect unique runtime references used by an AST.
 * @param ast AST to inspect.
 * @param names Output array for borrowed reference names.
 * @param max_names Maximum number of names to write to `names`.
 * @return Number of unique references written or available.
 */
size_t cxpr_ast_references(const cxpr_ast* ast, const char** names, size_t max_names);
/**
 * @brief Collect unique function or producer names used by an AST.
 * @param ast AST to inspect.
 * @param names Output array for borrowed function names.
 * @param max_names Maximum number of names to write to `names`.
 * @return Number of unique names written or available.
 */
size_t cxpr_ast_functions_used(const cxpr_ast* ast, const char** names, size_t max_names);
/**
 * @brief Collect unique `$param` names used by an AST.
 * @param ast AST to inspect.
 * @param names Output array for borrowed parameter names.
 * @param max_names Maximum number of names to write to `names`.
 * @return Number of unique parameter names written or available.
 */
size_t cxpr_ast_variables_used(const cxpr_ast* ast, const char** names, size_t max_names);
/**
 * @brief Perform structural and registry-backed semantic analysis on an AST.
 * @param ast AST to inspect.
 * @param reg Optional registry used to resolve functions and expressions.
 * @param out_analysis Output analysis struct to fill.
 * @param err Optional error output.
 * @return True on success, false on semantic-analysis failure.
 */
bool cxpr_analyze(const cxpr_ast* ast, const cxpr_registry* reg,
                  cxpr_analysis* out_analysis, cxpr_error* err);
/**
 * @brief Parse and analyze one expression string in a single call.
 * @param expression NUL-terminated expression source.
 * @param reg Optional registry used to resolve functions and expressions.
 * @param out_analysis Output analysis struct to fill.
 * @param err Optional error output.
 * @return True on success, false on parse or semantic-analysis failure.
 */
bool cxpr_analyze_expr(const char* expression, const cxpr_registry* reg,
                       cxpr_analysis* out_analysis, cxpr_error* err);

/* ═══════════════════════════════════════════════════════════════════════════
 * Evaluator API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Evaluate an AST to a typed runtime value.
 * @param ast AST to evaluate.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output value on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure.
 */
bool cxpr_eval_ast(const cxpr_ast* ast, const cxpr_context* ctx,
                   const cxpr_registry* reg, cxpr_value* out_value, cxpr_error* err);
/**
 * @brief Evaluate an AST and require a numeric result.
 * @param ast AST to evaluate.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output number on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure or type mismatch.
 */
bool cxpr_eval_ast_number(const cxpr_ast* ast, const cxpr_context* ctx,
                          const cxpr_registry* reg, double* out_value, cxpr_error* err);
/**
 * @brief Evaluate an AST and require a boolean result.
 * @param ast AST to evaluate.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output boolean on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure or type mismatch.
 */
bool cxpr_eval_ast_bool(const cxpr_ast* ast, const cxpr_context* ctx,
                        const cxpr_registry* reg, bool* out_value, cxpr_error* err);
/**
 * @brief Evaluate an AST at a lookback expression (`ast[index_ast]`).
 * @param ast Target AST to evaluate.
 * @param index_ast AST that evaluates to the desired lookback index.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output value on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure.
 */
bool cxpr_eval_ast_at_lookback(const cxpr_ast* ast,
                               const cxpr_ast* index_ast,
                               const cxpr_context* ctx,
                               const cxpr_registry* reg,
                               cxpr_value* out_value,
                               cxpr_error* err);
/**
 * @brief Evaluate an AST at one numeric lookback offset (`ast[offset]`).
 * @param ast Target AST to evaluate.
 * @param lookback Non-negative lookback offset.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output value on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure.
 */
bool cxpr_eval_ast_at_offset(const cxpr_ast* ast,
                             double lookback,
                             const cxpr_context* ctx,
                             const cxpr_registry* reg,
                             cxpr_value* out_value,
                             cxpr_error* err);
/**
 * @brief Evaluate an AST to a number at one numeric lookback offset.
 * @param ast Target AST to evaluate.
 * @param lookback Non-negative lookback offset.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output number on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure or type mismatch.
 */
bool cxpr_eval_ast_number_at_offset(const cxpr_ast* ast,
                                    double lookback,
                                    const cxpr_context* ctx,
                                    const cxpr_registry* reg,
                                    double* out_value,
                                    cxpr_error* err);
/**
 * @brief Evaluate an AST to a bool at one numeric lookback offset.
 * @param ast Target AST to evaluate.
 * @param lookback Non-negative lookback offset.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output bool on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure or type mismatch.
 */
bool cxpr_eval_ast_bool_at_offset(const cxpr_ast* ast,
                                  double lookback,
                                  const cxpr_context* ctx,
                                  const cxpr_registry* reg,
                                  bool* out_value,
                                  cxpr_error* err);

/* ═══════════════════════════════════════════════════════════════════════════
 * Compiled Program API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Compile an AST into an executable program.
 * @param ast AST to compile.
 * @param reg Function registry used for resolution and codegen.
 * @param err Optional error output.
 * @return Newly allocated program on success, or NULL on failure.
 */
cxpr_program* cxpr_compile(const cxpr_ast* ast, const cxpr_registry* reg, cxpr_error* err);
/**
 * @brief Evaluate a compiled program to a typed runtime value.
 * @param prog Program to evaluate.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output value on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure.
 */
bool cxpr_eval_program(const cxpr_program* prog, const cxpr_context* ctx,
                       const cxpr_registry* reg, cxpr_value* out_value, cxpr_error* err);
/**
 * @brief Evaluate a compiled program and require a numeric result.
 * @param prog Program to evaluate.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output number on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure or type mismatch.
 */
bool cxpr_eval_program_number(const cxpr_program* prog, const cxpr_context* ctx,
                              const cxpr_registry* reg, double* out_value, cxpr_error* err);
/**
 * @brief Evaluate a compiled program and require a boolean result.
 * @param prog Program to evaluate.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output boolean on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure or type mismatch.
 */
bool cxpr_eval_program_bool(const cxpr_program* prog, const cxpr_context* ctx,
                            const cxpr_registry* reg, bool* out_value, cxpr_error* err);
/**
 * @brief Free a compiled program.
 * @param prog Program to free. May be NULL.
 */
void cxpr_program_free(cxpr_program* prog);
/**
 * @brief Dump a human-readable representation of a compiled program.
 * @param prog Program to dump.
 * @param out Output stream to write to.
 */
void cxpr_program_dump(const cxpr_program* prog, FILE* out);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_AST_H */
