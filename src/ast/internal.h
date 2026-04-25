/**
 * @file internal.h
 * @brief Internal AST helpers shared across cxpr modules.
 */

#ifndef CXPR_AST_INTERNAL_H
#define CXPR_AST_INTERNAL_H

#include <cxpr/ast.h>

/** @brief Internal owned AST node representation. */
struct cxpr_ast {
    cxpr_node_type type;

    union {
        struct {
            double value;
        } number;
        struct {
            bool value;
        } boolean;
        struct {
            char* value;
        } string;
        struct {
            char* name;
        } identifier;
        struct {
            char* name;
        } variable;
        struct {
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
            struct cxpr_ast** args;
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
            struct cxpr_ast* left;
            struct cxpr_ast* right;
        } binary_op;
        struct {
            int op;
            struct cxpr_ast* operand;
        } unary_op;
        struct {
            char* name;
            struct cxpr_ast** args;
            char** arg_names;
            size_t argc;
            const struct cxpr_registry* cached_registry;
            unsigned long cached_registry_version;
            size_t cached_entry_index;
            bool cached_entry_found;
            bool cached_lookup_valid;
        } function_call;
        struct {
            struct cxpr_ast* target;
            struct cxpr_ast* index;
        } lookback;
        struct {
            struct cxpr_ast* condition;
            struct cxpr_ast* true_branch;
            struct cxpr_ast* false_branch;
        } ternary;
    } data;
};

cxpr_ast* cxpr_ast_new_number(double value);
cxpr_ast* cxpr_ast_new_bool(bool value);
/**
 * @brief Internal constructor for a string literal node.
 * @param value Borrowed string payload to copy.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_string(const char* value);
cxpr_ast* cxpr_ast_new_identifier(const char* name);
cxpr_ast* cxpr_ast_new_variable(const char* name);
cxpr_ast* cxpr_ast_new_field_access(const char* object, const char* field);
cxpr_ast* cxpr_ast_new_chain_access(const char* const* path, size_t depth);
cxpr_ast* cxpr_ast_new_producer_access(const char* name, cxpr_ast** args, size_t argc,
                                       const char* field);
cxpr_ast* cxpr_ast_new_producer_access_named(const char* name, cxpr_ast** args,
                                             char** arg_names, size_t argc,
                                             const char* field);
cxpr_ast* cxpr_ast_new_binary_op(int op, cxpr_ast* left, cxpr_ast* right);
cxpr_ast* cxpr_ast_new_unary_op(int op, cxpr_ast* operand);
cxpr_ast* cxpr_ast_new_function_call(const char* name, cxpr_ast** args, size_t argc);
cxpr_ast* cxpr_ast_new_function_call_named(const char* name, cxpr_ast** args,
                                           char** arg_names, size_t argc);
cxpr_ast* cxpr_ast_new_lookback(cxpr_ast* target, cxpr_ast* index);
cxpr_ast* cxpr_ast_new_ternary(cxpr_ast* condition, cxpr_ast* true_branch, cxpr_ast* false_branch);
/**
 * @brief Check whether a call AST contains any named arguments.
 * @param ast Function-call or producer-access AST node.
 * @return True when at least one argument carries an explicit name.
 */
bool cxpr_ast_call_uses_named_args(const cxpr_ast* ast);
/**
 * @brief Return the cached flattened runtime reference string for an AST.
 * @param ast Reference-like AST node.
 * @return Borrowed full reference string, or NULL when the node has no flattened reference form.
 */
const char* cxpr_ast_full_reference(const cxpr_ast* ast);

#endif /* CXPR_AST_INTERNAL_H */
