/**
 * @file source_plan.h
 * @brief Provider source-plan parsing API.
 *
 * Source plans describe how a provider source expression should be materialized
 * by a host. They are useful for expressions such as `close`, `ema(close, 14)`,
 * `ema(close(selector="1d"), 14)[2]`, or arbitrary source expressions that must
 * be evaluated bar-by-bar by the host.
 */

#pragma once

#include <cxpr/provider.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Kind of source-plan node parsed from an expression AST.
 */
typedef enum {
    CXPR_SOURCE_PLAN_INVALID = 0,    /**< Uninitialized or invalid node. */
    CXPR_SOURCE_PLAN_FIELD = 1,      /**< Direct source field, such as `close`. */
    CXPR_SOURCE_PLAN_INDICATOR = 2,  /**< Provider function without source input. */
    CXPR_SOURCE_PLAN_SMOOTHING = 3,  /**< Provider function applied to another source. */
    CXPR_SOURCE_PLAN_EXPRESSION = 4, /**< Arbitrary expression AST materialized by host logic. */
} cxpr_source_plan_kind;

/**
 * @brief One node in a parsed provider source-plan tree.
 *
 * String fields and arrays are owned by the containing @ref cxpr_source_plan_ast
 * and released by @ref cxpr_free_source_plan_ast. `expression_ast` is borrowed
 * from the original parsed cxpr AST and must not be freed through the plan.
 */
typedef struct cxpr_source_plan_node {
    cxpr_source_plan_kind kind; /**< Node kind. */
    uint64_t node_id;           /**< Stable hash derived from canonical node content. */
    char* name;                 /**< Source or provider function name, when applicable. */
    char* field_name;           /**< Selected record field, when applicable. */
    char* timeframe;            /**< Backward-compatible alias for @ref scope_value. */
    char* scope_value;          /**< Optional scope selector, such as `1d` or `warehouse-a`. */
    size_t arg_count;           /**< Number of numeric bound argument slots. */
    size_t* arg_slots;          /**< Slots into @ref cxpr_source_plan_ast::bound_arg_asts. */
    size_t lookback_slot;       /**< Bound lookback slot, or `SIZE_MAX` when absent. */
    struct cxpr_source_plan_node* source; /**< Child source for smoothing/source-input nodes. */
    const cxpr_ast* expression_ast; /**< Borrowed AST for EXPRESSION nodes. */
} cxpr_source_plan_node;

/**
 * @brief Parsed provider source plan with owned metadata.
 */
typedef struct {
    cxpr_source_plan_node root;      /**< Root source-plan node. */
    const cxpr_ast** bound_arg_asts; /**< Borrowed ASTs for runtime numeric arguments. */
    size_t arg_count;                /**< Number of entries in @ref bound_arg_asts. */
    char* canonical;                 /**< Owned canonical rendering of the plan. */
} cxpr_source_plan_ast;

/**
 * @brief Parse one provider source expression into a source-plan tree.
 * @param[in] provider Provider metadata used to identify source/function names.
 * @param[in] ast Expression AST to parse. The returned plan borrows AST nodes.
 * @param[out] out Receives the parsed plan on success.
 * @return Non-zero on success, zero when the AST is not a valid provider source plan.
 *
 * On success, call @ref cxpr_free_source_plan_ast to release owned plan storage.
 */
int cxpr_parse_provider_source_plan_ast(
    const cxpr_provider* provider,
    const cxpr_ast* ast,
    cxpr_source_plan_ast* out);

/**
 * @brief Evaluate bound numeric AST arguments for a parsed source plan.
 * @param[in] plan Parsed source plan.
 * @param[in] ctx Evaluation context used for nested expressions.
 * @param[in] reg Registry used for nested evaluation.
 * @param[out] out_values Receives evaluated numeric values.
 * @param[in] out_capacity Capacity of @p out_values.
 * @param[in,out] err Optional error sink.
 * @return Non-zero on success, zero on evaluation failure or insufficient capacity.
 */
int cxpr_eval_source_plan_bound_args(
    const cxpr_source_plan_ast* plan,
    const cxpr_context* ctx,
    const cxpr_registry* reg,
    double* out_values,
    size_t out_capacity,
    cxpr_error* err);

/**
 * @brief Free storage owned by a parsed source plan.
 * @param[in,out] plan Plan to clear. Safe to call on zero-initialized storage.
 */
void cxpr_free_source_plan_ast(cxpr_source_plan_ast* plan);

#ifdef __cplusplus
}
#endif
