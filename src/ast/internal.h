/**
 * @file internal.h
 * @brief Internal AST helpers shared across cxpr modules.
 */

#ifndef CXPR_AST_INTERNAL_H
#define CXPR_AST_INTERNAL_H

#include <cxpr/ast/expression.h>

/** @brief Internal owned AST node representation. */
struct cxpr_expr_ast {
    cxpr_expr_ast_kind type;
    cxpr_expr_compiled* compiled_cache;
    const struct cxpr_registry* compiled_registry;
    unsigned long compiled_registry_version;
    bool compiled_cache_failed;
    const struct cxpr_registry* typecheck_registry;
    unsigned long typecheck_registry_version;
    bool typecheck_cache_valid;
    bool typecheck_cache_ok;

    union {
        struct {
            double value;
        } number;
        struct {
            bool value;
        } boolean;
        struct {
            struct cxpr_expr_ast** elements;
            size_t count;
        } array;
        struct {
            char** field_names;
            struct cxpr_expr_ast** field_values;
            size_t field_count;
        } record;
        struct {
            char* value;
        } string;
        struct {
            char* name;
            unsigned long hash;
            const struct cxpr_context* cached_ctx;
            void* cached_entries_base;
            size_t cached_slot;
            unsigned long cached_version;
        } identifier;
        struct {
            char* name;
            unsigned long hash;
            const struct cxpr_context* cached_ctx;
            void* cached_entries_base;
            size_t cached_slot;
            unsigned long cached_version;
        } variable;
        struct {
            struct cxpr_expr_ast* base;
            char* object;
            char* field;
            char* full_key;
        } field_access;
        struct {
            char** path;
            size_t depth;
            char* full_key;
        } chain_access;
        struct {
            char* name;
            struct cxpr_expr_ast** args;
            char** arg_names;
            size_t argc;
            char* field;
            char* full_key;
            const struct cxpr_registry* cached_registry;
            unsigned long cached_registry_version;
            size_t cached_entry_index;
            bool cached_entry_found;
            bool cached_lookup_valid;
            char* cached_const_key;
            bool cached_const_key_ready;
            size_t cached_field_index;
            bool cached_field_index_valid;
        } producer_access;
        struct {
            int op;
            struct cxpr_expr_ast* left;
            struct cxpr_expr_ast* right;
        } binary_op;
        struct {
            int op;
            struct cxpr_expr_ast* operand;
        } unary_op;
        struct {
            char* name;
            struct cxpr_expr_ast** args;
            char** arg_names;
            size_t argc;
            const struct cxpr_registry* cached_registry;
            unsigned long cached_registry_version;
            size_t cached_entry_index;
            bool cached_entry_found;
            bool cached_lookup_valid;
            const struct cxpr_registry* cached_memoable_registry;
            unsigned long cached_memoable_registry_version;
            unsigned long cached_hash;
            bool cached_memoable;
            bool cached_memoable_valid;
            bool cached_hash_valid;
            char* cached_const_key;
            bool cached_const_key_ready;
        } function_call;
        struct {
            struct cxpr_expr_ast* target;
            struct cxpr_expr_ast* index;
        } lookback;
        struct {
            struct cxpr_expr_ast* condition;
            struct cxpr_expr_ast* true_branch;
            struct cxpr_expr_ast* false_branch;
        } ternary;
    } data;
};

/**
 * @brief Internal constructor for a numeric literal node.
 * @param value Literal numeric payload.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_number_new(double value);
/**
 * @brief Internal constructor for a boolean literal node.
 * @param value Literal boolean payload.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_bool_new(bool value);
/**
 * @brief Internal constructor for an array literal node.
 * @param elements Owned element array.
 * @param count Number of elements.
 * @return Newly allocated AST node taking ownership of `elements`, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_array_new(cxpr_expr_ast** elements, size_t count);
/**
 * @brief Internal constructor for a record literal node.
 * @param field_names Borrowed field names copied by the node.
 * @param field_values Owned field expression array.
 * @param field_count Number of fields.
 * @return Newly allocated AST node taking ownership of `field_values`, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_record_new(const char* const* field_names,
                              cxpr_expr_ast** field_values,
                              size_t field_count);
/**
 * @brief Internal constructor for a string literal node.
 * @param value Borrowed string payload to copy.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_new_string(const char* value);
/**
 * @brief Internal constructor for an identifier node.
 * @param name Borrowed identifier string to copy.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_identifier_new(const char* name);
/**
 * @brief Internal constructor for a `$param` variable node.
 * @param name Borrowed parameter name string to copy.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_param_new(const char* name);
/**
 * @brief Internal constructor for a dotted field-access node.
 * @param object Borrowed object/prefix name to copy.
 * @param field Borrowed field name to copy.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_field_new(const char* object, const char* field);
cxpr_expr_ast* cxpr_expr_ast_field_expr_new(cxpr_expr_ast* base, const char* field);
/**
 * @brief Internal constructor for a multi-segment chain-access node.
 * @param path Borrowed segment array to copy.
 * @param depth Number of path segments.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_new_chain_access(const char* const* path, size_t depth);
/**
 * @brief Internal constructor for a producer-field access node.
 * @param name Producer name.
 * @param args Owned argument array.
 * @param argc Number of arguments.
 * @param field Selected produced field name.
 * @return Newly allocated AST node taking ownership of `args`, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_producer_field_new(const char* name, cxpr_expr_ast** args, size_t argc,
                                       const char* field);
/**
 * @brief Internal constructor for a producer-field access node with named arguments.
 * @param name Producer name.
 * @param args Owned argument array.
 * @param arg_names Optional owned argument-name array.
 * @param argc Number of arguments.
 * @param field Selected produced field name.
 * @return Newly allocated AST node taking ownership of `args` and `arg_names`, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_producer_field_named_new(const char* name, cxpr_expr_ast** args,
                                             char** arg_names, size_t argc,
                                             const char* field);
/**
 * @brief Internal constructor for a binary operator node.
 * @param op Internal token/operator tag.
 * @param left Owned left child.
 * @param right Owned right child.
 * @return Newly allocated AST node taking ownership of both children, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_binary_new(int op, cxpr_expr_ast* left, cxpr_expr_ast* right);
/**
 * @brief Internal constructor for a unary operator node.
 * @param op Internal token/operator tag.
 * @param operand Owned operand child.
 * @return Newly allocated AST node taking ownership of `operand`, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_unary_new(int op, cxpr_expr_ast* operand);
/**
 * @brief Internal constructor for a function-call node.
 * @param name Function name.
 * @param args Owned argument array.
 * @param argc Number of arguments.
 * @return Newly allocated AST node taking ownership of `args`, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_call_new(const char* name, cxpr_expr_ast** args, size_t argc);
/**
 * @brief Internal constructor for a function-call node with named arguments.
 * @param name Function name.
 * @param args Owned argument array.
 * @param arg_names Optional owned argument-name array.
 * @param argc Number of arguments.
 * @return Newly allocated AST node taking ownership of `args` and `arg_names`, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_call_named_new(const char* name, cxpr_expr_ast** args,
                                           char** arg_names, size_t argc);
/**
 * @brief Internal constructor for a lookback node.
 * @param target Owned target child.
 * @param index Owned index child.
 * @return Newly allocated AST node taking ownership of both children, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_lookback_new(cxpr_expr_ast* target, cxpr_expr_ast* index);
/**
 * @brief Internal constructor for a ternary node.
 * @param condition Owned condition child.
 * @param true_branch Owned true-branch child.
 * @param false_branch Owned false-branch child.
 * @return Newly allocated AST node taking ownership of all three children, or NULL on allocation failure.
 */
cxpr_expr_ast* cxpr_expr_ast_ternary_new(cxpr_expr_ast* condition, cxpr_expr_ast* true_branch, cxpr_expr_ast* false_branch);
/**
 * @brief Check whether a call AST contains any named arguments.
 * @param ast Function-call or producer-access AST node.
 * @return True when at least one argument carries an explicit name.
 */
bool cxpr_expr_ast_call_uses_named_args(const cxpr_expr_ast* ast);
/**
 * @brief Return the cached flattened runtime reference string for an AST.
 * @param ast Reference-like AST node.
 * @return Borrowed full reference string, or NULL when the node has no flattened reference form.
 */
const char* cxpr_expr_ast_full_reference(const cxpr_expr_ast* ast);

#endif /* CXPR_AST_INTERNAL_H */
