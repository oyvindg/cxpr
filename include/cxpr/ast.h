/**
 * @file ast.h
 * @brief Public AST API for cxpr.
 */

#ifndef CXPR_AST_H
#define CXPR_AST_H

#include <cxpr/token.h>
#include <cxpr/types.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Free an AST and all owned descendants.
 * @param ast AST to free. May be NULL.
 */
void cxpr_ast_free(cxpr_ast* ast);
/**
 * @brief Deep-clone an AST and all owned descendants.
 * @param ast AST to clone. May be NULL.
 * @return Newly allocated AST clone, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_clone(const cxpr_ast* ast);
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
 * @brief Construct an array literal node.
 * @param elements Element AST array.
 * @param count Number of elements.
 * @return Newly allocated AST node taking ownership of `elements`, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_array(cxpr_ast** elements, size_t count);
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
 * @brief Construct a producer-field access node with optional named arguments.
 * @param name Producer name.
 * @param args Producer argument array.
 * @param arg_names Optional owned per-argument name array; NULL entry means positional.
 * @param argc Number of producer arguments.
 * @param field Field name selected from the produced struct.
 * @return Newly allocated AST node taking ownership of `args` and `arg_names`, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_producer_access_named(const char* name, cxpr_ast** args,
                                             char** arg_names, size_t argc,
                                             const char* field);
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
 * @brief Construct a function-call node with optional named arguments.
 * @param name Function name.
 * @param args Argument array.
 * @param arg_names Optional owned per-argument name array; NULL entry means positional.
 * @param argc Number of arguments.
 * @return Newly allocated AST node taking ownership of `args` and `arg_names`, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_function_call_named(const char* name, cxpr_ast** args,
                                           char** arg_names, size_t argc);
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

typedef enum {
    CXPR_NODE_NUMBER,
    CXPR_NODE_BOOL,
    CXPR_NODE_ARRAY,
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

typedef struct {
    const char* producer_name;           /**< Producer/function name, e.g. `ichimoku`. */
    const char* field_name;              /**< Selected field name, e.g. `senkouA`. */
} cxpr_producer_field_ref;

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
 * @brief Return the argument name for a function-call argument.
 * @param ast Function-call node.
 * @param index Zero-based argument index.
 * @return Borrowed argument name, or NULL when the argument is positional or `index` is out of range.
 */
const char* cxpr_ast_function_arg_name(const cxpr_ast* ast, size_t index);
/**
 * @brief Return true when a function-call node contains at least one named argument.
 * @param ast Function-call node.
 * @return True when at least one argument is named.
 */
bool cxpr_ast_function_has_named_args(const cxpr_ast* ast);
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
 * @brief Return the argument name for a producer-call argument.
 * @param ast Producer-access node.
 * @param index Zero-based argument index.
 * @return Borrowed argument name, or NULL when the argument is positional or `index` is out of range.
 */
const char* cxpr_ast_producer_arg_name(const cxpr_ast* ast, size_t index);
/**
 * @brief Return true when a producer-access node contains at least one named argument.
 * @param ast Producer-access node.
 * @return True when at least one argument is named.
 */
bool cxpr_ast_producer_has_named_args(const cxpr_ast* ast);
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

/**
 * @brief Return whether the AST root is a boolean-valued predicate expression.
 * @param ast Root AST to inspect.
 * @return `true` for comparisons, logical expressions, boolean literals,
 *         boolean unary operators, ternaries with boolean branches, and
 *         boolean-style calls such as `cross_above(...)`.
 */
bool cxpr_ast_is_boolean_expression(const cxpr_ast* ast);

/**
 * @brief Render an AST to an allocated expression string.
 * @param ast AST to render.
 * @return Newly allocated NUL-terminated string, or NULL on allocation failure.
 *         Caller must free the returned string.
 *
 * The output is valid cxpr source that can be parsed back into an equivalent
 * AST. Parentheses are inserted only where needed to preserve semantics.
 */
char* cxpr_ast_to_string(const cxpr_ast* ast);
/**
 * @brief Write an AST rendering to a FILE stream.
 * @param ast AST to render.
 * @param out Output stream.
 */
void cxpr_ast_dump(const cxpr_ast* ast, FILE* out);

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
 * @brief Collect unique producer field accesses used by an AST.
 * @param ast AST to inspect.
 * @param refs Output array for borrowed `(producer, field)` pairs.
 * @param max_refs Maximum number of pairs to write to `refs`.
 * @return Number of unique producer-field pairs written or available.
 */
size_t cxpr_ast_producer_fields_used(const cxpr_ast* ast,
                                     cxpr_producer_field_ref* refs,
                                     size_t max_refs);
/**
 * @brief Collect unique `$param` names used by an AST.
 * @param ast AST to inspect.
 * @param names Output array for borrowed parameter names.
 * @param max_names Maximum number of names to write to `names`.
 * @return Number of unique parameter names written or available.
 */
size_t cxpr_ast_variables_used(const cxpr_ast* ast, const char** names, size_t max_names);
/**
 * @brief Return whether an AST contains a runtime reference.
 * @param ast AST to inspect.
 * @param name Reference name, e.g. `close` or `macd.line`.
 * @return True when @p name appears as a runtime reference in @p ast.
 */
bool cxpr_ast_contains_reference(const cxpr_ast* ast, const char* name);
/**
 * @brief Return whether an AST contains a `$param` variable.
 * @param ast AST to inspect.
 * @param name Parameter name without `$`.
 * @return True when @p name appears as a parameter variable in @p ast.
 */
bool cxpr_ast_contains_variable(const cxpr_ast* ast, const char* name);
/**
 * @brief Collect function/producer names whose argument subtree contains a reference.
 * @param ast AST to inspect.
 * @param reference Reference name to trace, e.g. `atr_baseline_pct`.
 * @param names Output array for borrowed function or producer names.
 * @param max_names Maximum number of names to write to `names`.
 * @return Number of unique function/producer contexts written or available.
 */
size_t cxpr_ast_call_arg_contexts_for_reference(const cxpr_ast* ast,
                                                const char* reference,
                                                const char** names,
                                                size_t max_names);
/**
 * @brief Collect function/producer names whose argument subtree contains a `$param`.
 * @param ast AST to inspect.
 * @param variable Parameter name without `$`.
 * @param names Output array for borrowed function or producer names.
 * @param max_names Maximum number of names to write to `names`.
 * @return Number of unique function/producer contexts written or available.
 */
size_t cxpr_ast_call_arg_contexts_for_variable(const cxpr_ast* ast,
                                               const char* variable,
                                               const char** names,
                                               size_t max_names);

#ifdef __cplusplus
}
#endif

#include <cxpr/analysis.h>
#include <cxpr/eval.h>
#include <cxpr/parser.h>
#include <cxpr/program.h>

#endif /* CXPR_AST_H */
