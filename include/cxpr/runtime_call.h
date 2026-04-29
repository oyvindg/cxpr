/**
 * @file runtime_call.h
 * @brief Runtime call inspection helpers for cxpr providers.
 *
 * These helpers decode cxpr AST call nodes into a small host-neutral view.
 * Hosts use the decoded view to route function calls, producer field access,
 * numeric arguments, and optional scope selectors to their runtime backends.
 */

#pragma once

#include <cxpr/provider.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Kind of runtime call represented by a cxpr AST node.
 */
typedef enum {
    CXPR_RUNTIME_CALL_INVALID = 0,  /**< Unsupported or uninitialized call. */
    CXPR_RUNTIME_CALL_FUNCTION = 1, /**< Plain function call, such as `ema(14)`. */
    CXPR_RUNTIME_CALL_PRODUCER = 2, /**< Producer field access, such as `macd(...).signal`. */
} cxpr_runtime_call_kind;

/**
 * @brief Decoded view of one function-call or producer-access AST node.
 *
 * String pointers are borrowed from the original AST and provider metadata.
 * Keep both alive while using this structure.
 */
typedef struct {
    cxpr_runtime_call_kind kind; /**< Decoded call kind. */
    const char* name;            /**< Function or source name. */
    const char* field_name;      /**< Producer field name, or NULL. */
    const char* timeframe;       /**< Backward-compatible alias for @ref scope_value. */
    const char* scope_value;     /**< Optional selector, such as `1d` or `warehouse-a`. */
    size_t arg_count;            /**< Raw AST argument count. */
    size_t value_arg_count;      /**< Numeric/value argument count excluding selector args. */
} cxpr_runtime_call;

/**
 * @brief Provider-resolved scope metadata found in an expression tree.
 */
typedef struct {
    const char* scope_name;  /**< Provider scope parameter name, such as `selector`. */
    const char* scope_value; /**< Borrowed selector value. */
    const cxpr_ast* origin;  /**< Borrowed call AST that contributed the scope. */
} cxpr_resolved_scope;

/**
 * @brief Decode one runtime call AST without provider metadata.
 * @param[in] ast Function-call or producer-access AST node.
 * @param[out] out Receives the decoded call view on success.
 * @return Non-zero when @p ast is a supported runtime call node.
 *
 * This recognizes trailing string selector arguments positionally, for example
 * `close("1d")` or `macd(12, 26, 9, "1d").signal`.
 */
int cxpr_parse_runtime_call(
    const cxpr_ast* ast,
    cxpr_runtime_call* out);

/**
 * @brief Decode one runtime call AST using provider scope metadata.
 * @param[in] provider Optional provider used to resolve named selector args.
 * @param[in] ast Function-call or producer-access AST node.
 * @param[out] out Receives the decoded call view on success.
 * @return Non-zero when @p ast is a supported runtime call node.
 *
 * With provider metadata, named selector arguments such as
 * `close(selector="1d")` are exposed through @ref cxpr_runtime_call::scope_value
 * and excluded from @ref cxpr_runtime_call::value_arg_count.
 */
int cxpr_parse_runtime_call_provider(
    const cxpr_provider* provider,
    const cxpr_ast* ast,
    cxpr_runtime_call* out);

/**
 * @brief Find the first provider-declared scoped call in an expression tree.
 * @param[in] provider Provider metadata used to identify scoped calls.
 * @param[in] root Expression AST root to inspect.
 * @param[out] out Receives borrowed scope metadata on success.
 * @return Non-zero when a provider-declared scope selector was found.
 */
int cxpr_resolve_expression_scope(
    const cxpr_provider* provider,
    const cxpr_ast* root,
    cxpr_resolved_scope* out);

/**
 * @brief Return one numeric/value argument AST from a runtime call.
 * @param[in] provider Optional provider used for named-arg binding.
 * @param[in] ast Original call AST.
 * @param[in] index Zero-based value argument index, excluding selector args.
 * @return Borrowed argument AST, or NULL when unsupported/out of range.
 */
const cxpr_ast* cxpr_provider_runtime_call_arg(
    const cxpr_provider* provider,
    const cxpr_ast* ast,
    size_t index);

/**
 * @brief Evaluate leading runtime-call value arguments as numbers.
 * @param[in] provider Optional provider used for named-arg binding.
 * @param[in] ast Original call AST.
 * @param[in] count Number of value arguments to evaluate.
 * @param[in] ctx Evaluation context.
 * @param[in] reg Registry used for nested evaluation.
 * @param[out] out_values Receives numeric values.
 * @param[in] out_capacity Capacity of @p out_values.
 * @param[in,out] err Optional error sink.
 * @return Non-zero on success, zero on failure or insufficient capacity.
 */
int cxpr_provider_eval_runtime_call_number_args(
    const cxpr_provider* provider,
    const cxpr_ast* ast,
    size_t count,
    const cxpr_context* ctx,
    const cxpr_registry* reg,
    double* out_values,
    size_t out_capacity,
    cxpr_error* err);

#ifdef __cplusplus
}
#endif
