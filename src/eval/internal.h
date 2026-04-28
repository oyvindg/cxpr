/**
 * @file internal.h
 * @brief Internal helpers for the split cxpr evaluator.
 */

#ifndef CXPR_EVAL_INTERNAL_H
#define CXPR_EVAL_INTERNAL_H

#include "../ast/internal.h"
#include "../registry/internal.h"
#include <cxpr/context.h>

/** @brief Populate an error object and return a zero-like failure value. */
cxpr_value cxpr_eval_error(cxpr_error* err, cxpr_error_code code, const char* message);
/** @brief Require one typed value to match an expected runtime type. */
bool cxpr_require_type(cxpr_value value, cxpr_value_type type,
                       cxpr_error* err, const char* message);
/** @brief Look up one named field from a struct value. */
cxpr_value cxpr_struct_get_field(const cxpr_struct_value* value, const char* field, bool* found);
/** @brief Look up one field by positional index from a struct value. */
cxpr_value cxpr_struct_get_field_by_index(const cxpr_struct_value* value, size_t index,
                                          bool* found);
/** @brief Try to fold an AST subtree to a constant double. */
bool cxpr_eval_constant_double(const cxpr_ast* ast, double* out);
/** @brief Deep-clone an AST subtree for internal rewriting/evaluation flows. */
cxpr_ast* cxpr_eval_clone_ast(const cxpr_ast* ast);
/** @brief Build or reuse a constant-key string for one producer call AST. */
const char* cxpr_eval_prepare_const_key_for_producer(const cxpr_ast* ast,
                                                     const cxpr_ast* const* ordered_args,
                                                     size_t argc,
                                                     const cxpr_context* ctx,
                                                     const cxpr_registry* reg,
                                                     char* local_buf,
                                                     size_t local_cap,
                                                     char** heap_buf,
                                                     cxpr_error* err);
/** @brief Format a struct-cache key from scalar producer arguments. */
const char* cxpr_build_struct_cache_key(const char* name, const double* args, size_t argc,
                                        char* local_buf, size_t local_cap, char** heap_buf);
/** @brief Evaluate or fetch one cached struct-producing function result. */
const cxpr_struct_value* cxpr_eval_struct_result(cxpr_func_entry* entry,
                                                 const char* name,
                                                 const cxpr_ast* const* arg_nodes,
                                                 size_t argc,
                                                 const char* cache_key_hint,
                                                 const cxpr_context* ctx,
                                                 const cxpr_registry* reg,
                                                 cxpr_error* err);
/** @brief Resolve and cache the registry entry used by a function-call AST. */
cxpr_func_entry* cxpr_eval_cached_function_entry(const cxpr_ast* ast, const cxpr_registry* reg);
/** @brief Resolve and cache the registry entry used by a producer-access AST. */
cxpr_func_entry* cxpr_eval_cached_producer_entry(const cxpr_ast* ast, const cxpr_registry* reg);
/** @brief Check whether an AST subtree contains any string literals. */
bool cxpr_eval_ast_contains_string_literal(const cxpr_ast* ast);

/** @brief Evaluate a struct producer and optionally select one field from the result. */
cxpr_value cxpr_eval_struct_producer(cxpr_func_entry* entry, const char* name,
                                     const char* field,
                                     const cxpr_ast* const* arg_nodes,
                                     size_t argc,
                                     const cxpr_context* ctx,
                                     const cxpr_registry* reg,
                                     cxpr_error* err);
/** @brief Evaluate one AST argument and coerce it to a scalar double. */
double cxpr_eval_scalar_arg(const cxpr_ast* ast, const cxpr_context* ctx,
                            const cxpr_registry* reg, cxpr_error* err);
/** @brief Populate an error for invalid named arguments and return a failure value. */
cxpr_value cxpr_eval_named_arg_error(cxpr_error* err, cxpr_error_code code,
                                     const char* message);
/** @brief Bind a call AST's arguments into canonical positional order. */
bool cxpr_eval_bind_call_args(const cxpr_ast* call_ast,
                              const cxpr_func_entry* entry,
                              const cxpr_ast** out_args,
                              cxpr_error* err);
/** @brief Evaluate one expression-defined function call. */
cxpr_value cxpr_eval_defined_function(cxpr_func_entry* entry,
                                      const cxpr_ast* call_ast,
                                      const cxpr_context* ctx,
                                      const cxpr_registry* reg,
                                      cxpr_error* err);
/** @brief Copy scalar bindings from one prefix namespace into another. */
bool cxpr_context_copy_prefixed_scalars(cxpr_context* dst, const cxpr_context* src,
                                        const char* src_prefix, const char* dst_prefix);
/** @brief Evaluate a defined function while honoring any AST overlay interception. */
cxpr_value cxpr_eval_defined_with_overlay(cxpr_func_entry* entry,
                                          const cxpr_ast* call_ast,
                                          const cxpr_context* ctx,
                                          const cxpr_registry* reg,
                                          cxpr_error* err);
/** @brief Evaluate a cached producer-field access node. */
cxpr_value cxpr_eval_cached_producer_access(const cxpr_ast* ast,
                                            const cxpr_context* ctx,
                                            const cxpr_registry* reg,
                                            cxpr_error* err);
/** @brief Return a structural hash for one AST subtree. */
unsigned long cxpr_eval_ast_hash(const cxpr_ast* ast);
/** @brief Check structural AST equality for evaluation memoization. */
bool cxpr_eval_ast_equal(const cxpr_ast* lhs, const cxpr_ast* rhs);
/** @brief Return true when an AST can be memoized without bypassing runtime-aware hooks. */
bool cxpr_eval_ast_memoable(const cxpr_ast* ast, const cxpr_registry* reg);
/** @brief Look up one memoized number/bool AST result from the current eval pass. */
bool cxpr_eval_memo_get(const cxpr_context* ctx,
                        const cxpr_ast* ast,
                        unsigned long hash,
                        cxpr_value* out_value);
/** @brief Store one number/bool AST result in the current eval-pass memo. */
bool cxpr_eval_memo_set(const cxpr_context* ctx,
                        const cxpr_ast* ast,
                        unsigned long hash,
                        cxpr_value value);
/** @brief Clear memoized AST values without touching producer struct cache. */
void cxpr_eval_memo_clear(cxpr_context* ctx);
/** @brief Begin one top-level memoized evaluation scope. */
void cxpr_eval_memo_enter(cxpr_context* ctx);
/** @brief End one top-level memoized evaluation scope. */
void cxpr_eval_memo_leave(cxpr_context* ctx);

/** @brief Evaluate a dotted field-access node against the current context. */
cxpr_value cxpr_eval_field_access(const cxpr_ast* ast, const cxpr_context* ctx,
                                  const cxpr_registry* reg, cxpr_error* err);
/** @brief Evaluate a multi-segment chain-access node against the current context. */
cxpr_value cxpr_eval_chain_access(const cxpr_ast* ast, const cxpr_context* ctx,
                                  cxpr_error* err);
/** @brief Evaluate one AST node without additional result coercion. */
cxpr_value cxpr_eval_node(const cxpr_ast* ast, const cxpr_context* ctx,
                          const cxpr_registry* reg, cxpr_error* err);
/** @brief Evaluate one AST subtree to a typed runtime value. */
cxpr_value cxpr_eval_ast_value(const cxpr_ast* ast, const cxpr_context* ctx,
                               const cxpr_registry* reg, cxpr_error* err);

#endif /* CXPR_EVAL_INTERNAL_H */
