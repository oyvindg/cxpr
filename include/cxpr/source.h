/**
 * @file source.h
 * @brief Provider source planning and binding API.
 *
 * Source plans describe how a provider source expression should be materialized
 * by a host. They are useful for expressions such as `close`, `ema(close, 14)`,
 * `ema(close(timeframe="1d"), 14)[2]`, or arbitrary source expressions that must
 * be evaluated bar-by-bar by the host.
 *
 * New host integrations should prefer @ref cxpr_plan_bind_sources: cxpr owns AST
 * traversal, source-plan parsing, bound-argument evaluation, and scoped-source
 * registration; the host only binds parsed nodes to handles and resolves those
 * handles at evaluation time.
 */

#pragma once

#include <cxpr/provider.h>
#include <cxpr/resample.h>
#include <cxpr/source_location.h>
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
    uint64_t requirement_id;    /**< Stable materialization id, excluding lookback. */
    char* name;                 /**< Source or provider function name, when applicable. */
    char* field_name;           /**< Selected record field, when applicable. */
    char* scope_value;          /**< Optional scope value, such as timeframe `1d` or warehouse `warehouse-a`. */
    char* interval_value;       /**< Normalized resample interval, or NULL for non-temporal scopes. */
    int64_t interval_duration_ns; /**< Fixed interval in nanoseconds, or zero when absent. */
    cxpr_value_type value_type; /**< Materialized element type. */
    size_t arg_count;           /**< Number of numeric bound argument slots. */
    size_t* arg_slots;          /**< Slots into @ref cxpr_source_plan_ast::bound_arg_asts. */
    size_t lookback_slot;       /**< Bound lookback slot, or `SIZE_MAX` when absent. */
    struct cxpr_source_plan_node* source; /**< Child source for smoothing/source-input nodes. */
    const cxpr_expr_ast* expression_ast; /**< Borrowed AST for EXPRESSION nodes. */
} cxpr_source_plan_node;

/**
 * @brief Parsed provider source plan with owned metadata.
 */
typedef struct {
    cxpr_source_plan_node root;      /**< Root source-plan node. */
    const cxpr_expr_ast** bound_arg_asts; /**< Borrowed ASTs for runtime numeric arguments. */
    size_t arg_count;                /**< Number of entries in @ref bound_arg_asts. */
    char* canonical;                 /**< Owned canonical rendering of the plan. */
} cxpr_source_plan_ast;

/**
 * @brief Host callback used by @ref cxpr_plan_bind_sources to bind one
 * materializable source-plan leaf to a concrete runtime handle.
 *
 * `node` is a parsed source-plan leaf owned by the temporary plan being walked.
 * Its structured fields (`name`, `field_name`, `scope_value`, interval metadata,
 * `requirement_id`, `node_id`, and kind) are already separated by cxpr.
 * `requirement_id` identifies the pre-bound generated input independently of
 * expression lookbacks. `bound_args` contains this node's numeric
 * arguments after evaluating the node's bound argument ASTs against `ctx` and
 * `reg`; the array is borrowed and valid only for the duration of the call.
 *
 * The host should map the node plus bound arguments to a stable handle for its
 * own data source registry. Return non-zero on success and write the handle to
 * `out_handle`. Return zero when the source cannot be bound.
 */
typedef int (*cxpr_source_plan_bind_fn)(
    const cxpr_source_plan_node* node,
    const double* bound_args,
    size_t arg_count,
    uint64_t* out_handle,
    void* userdata);

/** @brief One owned, normalized provider series requirement. */
typedef struct {
    uint64_t requirement_id;        /**< Stable identity including provider, source, interval, and type. */
    char* provider_name;            /**< Owned provider identity. */
    char* source_name;              /**< Owned expression-visible source identity. */
    cxpr_resample_interval every;   /**< Normalized interval; zeroed for unscoped sources. */
    cxpr_value_type value_type;     /**< Required materialized element type. */
    unsigned source_flags;          /**< Provider source capabilities captured during planning. */
    cxpr_provider_materialization materialization; /**< Provider-declared materialization policy. */
} cxpr_series_requirement;

/* Requirement pointers passed to bind callbacks are borrowed and valid only
 * for that call. Manifest entries returned in cxpr_source_plan_bindings own
 * their strings and are immutable, so separate threads may inspect them after
 * planning completes. Hosts own handle lifetime and synchronization. Provider
 * metadata must remain alive and immutable for the entire planning call. */

/** @brief Bind a normalized requirement during planning, never on the runtime hot path. */
typedef int (*cxpr_series_requirement_bind_fn)(
    const cxpr_series_requirement* requirement,
    uint64_t* out_handle,
    void* userdata);

/**
 * @brief Reference/fallback resolver for one pre-bound materialized series.
 *
 * `evaluation_time_ns` is event time. `evaluation_cursor` identifies the host
 * evaluation row and is provided for hosts with precomputed alignment maps.
 * `lookback` is relative to the selected materialized series, never the host
 * evaluation stream. The callback is intended for tests and reference
 * evaluation; generated C uses pre-bound arrays/slots instead.
 */
typedef int (*cxpr_series_value_resolve_fn)(
    uint64_t handle,
    const cxpr_series_requirement* requirement,
    int64_t evaluation_time_ns,
    size_t evaluation_cursor,
    size_t lookback,
    cxpr_value* out_value,
    void* userdata);

typedef struct {
    cxpr_series_value_resolve_fn resolve; /**< Required reference resolver. */
    void* userdata; /**< Host-owned state; host provides synchronization. */
} cxpr_series_value_resolver;

/**
 * @brief Resolve one scoped source handle to a numeric value during evaluation.
 * @param[in] handle Host-defined source handle returned by planning.
 * @param[in] source_name Registered source name, such as `close`.
 * @param[out] out_value Receives the resolved numeric value.
 * @param[in] userdata Opaque pointer from @ref cxpr_scope_resolver.
 * @return Non-zero on success, zero when the value cannot be resolved.
 */
typedef int (*cxpr_scope_resolver_fn)(
    uint64_t handle,
    const char* source_name,
    double* out_value,
    void* userdata);

/**
 * @brief Host resolver configuration for scoped source functions.
 */
typedef struct {
    cxpr_scope_resolver_fn resolve; /**< Required eval-time resolver callback. */
    void* userdata;                 /**< Host-owned pointer passed to @ref resolve. */
} cxpr_scope_resolver;

/**
 * @brief Plan-time and eval-time callbacks for scoped source integration.
 *
 * `bind` maps parsed source-plan leaves to host handles during planning.
 * `resolve` maps those handles back to current numeric values during
 * evaluation. `userdata` is passed to both callbacks. Passing this config to
 * @ref cxpr_plan_bind_sources also lets cxpr register provider-declared scoped
 * source functions from provider metadata.
 */
typedef struct {
    cxpr_source_plan_bind_fn bind; /**< Required plan-time source binder. */
    cxpr_scope_resolver_fn resolve; /**< Optional eval-time source resolver. */
    void* userdata; /**< Host-owned pointer passed to @ref bind and @ref resolve. */
    cxpr_series_requirement_bind_fn bind_requirement; /**< Preferred normalized binder; optional for compatibility. */
} cxpr_plan_config;

/**
 * @brief Handles produced while binding source-plan leaves.
 *
 * `handles` and `requirement_ids` are owned parallel arrays with `count`
 * deduplicated entries in first-use order. They form the planning manifest used
 * to assign pre-bound generated-C/CUDA input slots; runtime callbacks are not
 * required by that path. Release them with @ref cxpr_free_source_plan_bindings.
 */
typedef struct {
    uint64_t* handles; /**< Owned host handles, one per bound source-plan leaf. */
    size_t count;      /**< Number of entries in @ref handles. */
    uint64_t* requirement_ids; /**< Owned stable requirement ids parallel to handles. */
    cxpr_series_requirement* requirements; /**< Owned inspectable manifest parallel to handles. */
} cxpr_source_plan_bindings;

/** Borrowed state used by the reference AST/IR evaluator for bound resamples. */
typedef struct {
    const cxpr_source_plan_bindings* bindings;
    const cxpr_series_value_resolver* resolver;
    int64_t evaluation_time_ns;
    size_t evaluation_cursor;
} cxpr_bound_series_evaluator;

/**
 * @brief Static mapping entry used by @ref cxpr_plan_bind_sources_from_table.
 */
typedef struct {
    const char* name;        /**< Source or provider function name to match. */
    const char* scope_value; /**< Optional scope value to match; NULL means unscoped/default. */
    uint64_t handle;         /**< Host handle returned for matching nodes. */
} cxpr_source_handle_entry;

/**
 * @brief Parse one provider source expression into a source-plan tree.
 * @param[in] provider Provider metadata used to identify source/function names.
 * @param[in] ast Expression AST to parse. The returned plan borrows AST nodes.
 * @param[out] out Receives the parsed plan on success.
 * @return Non-zero on success, zero when the AST is not a valid provider source plan.
 *
 * This low-level helper is kept for compatibility and advanced tooling. Normal
 * host integrations should use @ref cxpr_plan_bind_sources instead. On success,
 * call @ref cxpr_free_source_plan_ast to release owned plan storage.
 */
int cxpr_parse_provider_source_plan_ast(
    const cxpr_provider* provider,
    const cxpr_expr_ast* ast,
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
 *
 * This low-level helper is kept for compatibility and advanced tooling. Normal
 * host integrations should use @ref cxpr_plan_bind_sources, which evaluates
 * bound args before invoking the bind callback.
 */
int cxpr_eval_source_plan_bound_args(
    const cxpr_source_plan_ast* plan,
    const cxpr_context* ctx,
    const cxpr_registry* reg,
    double* out_values,
    size_t out_capacity,
    cxpr_error* err);

/**
 * @brief Discover and bind source-plan leaves in an expression AST.
 * @param[in] provider Provider metadata used to identify source/function names.
 * @param[in] expr Expression AST to inspect.
 * @param[in] ctx Evaluation context for bound numeric source-plan arguments.
 * @param[in] reg Registry for bound numeric source-plan arguments.
 * @param[in] config Plan/eval integration callbacks.
 * @param[out] out Receives owned handle bindings on success.
 * @param[in,out] err Optional error sink for bound-argument evaluation failures.
 * @return Non-zero on success, zero on parse, evaluation, allocation, or bind failure.
 *
 * This is a planning helper. cxpr owns AST traversal and source-plan parsing;
 * the host owns semantic binding from parsed source-plan leaves to concrete data
 * source handles. If @p config contains a resolver and @p reg is mutable, cxpr
 * also registers scoped source functions declared by provider source metadata.
 * On success, release @p out with
 * @ref cxpr_free_source_plan_bindings. On failure, @p out is cleared.
 */
int cxpr_plan_bind_sources(
    const cxpr_provider* provider,
    const cxpr_expr_ast* expr,
    const cxpr_context* ctx,
    cxpr_registry* reg,
    const cxpr_plan_config* config,
    cxpr_source_plan_bindings* out,
    cxpr_error* err);

/**
 * @brief Bind source-plan leaves by looking them up in a static name/scope table.
 *
 * This convenience wrapper is intended for simple hosts and tests. It matches
 * each leaf by `node->name` and `node->scope_value`; bound numeric arguments are
 * evaluated but ignored by the table lookup. More advanced hosts should use
 * @ref cxpr_plan_bind_sources with a callback.
 */
int cxpr_plan_bind_sources_from_table(
    const cxpr_provider* provider,
    const cxpr_expr_ast* expr,
    const cxpr_context* ctx,
    cxpr_registry* reg,
    const cxpr_source_handle_entry* table,
    size_t table_count,
    cxpr_source_plan_bindings* out,
    cxpr_error* err);

/**
 * @brief Free storage owned by a parsed source plan.
 * @param[in,out] plan Plan to clear. Safe to call on zero-initialized storage.
 */
void cxpr_free_source_plan_ast(cxpr_source_plan_ast* plan);

/**
 * @brief Free storage owned by source-plan bindings.
 * @param[in,out] bindings Bindings to clear. Safe to call on zero-initialized storage.
 */
void cxpr_free_source_plan_bindings(cxpr_source_plan_bindings* bindings);

/**
 * @brief Validate a manifest for the scalar numeric generated-C/CUDA ABI.
 *
 * Version 1 does not transport records or vectors. Hosts must call this before
 * constructing/uploading resample views so unsupported provider types fail at
 * the binding boundary rather than being reinterpreted as doubles.
 */
int cxpr_validate_generated_resample_bindings(
    const cxpr_source_plan_bindings* bindings,
    cxpr_error* err);

/**
 * @brief Resolve one manifest entry for reference evaluation.
 *
 * On missing history, invalid manifest index, or a resolver miss, this returns
 * zero and writes numeric NAN to `out_value`. Successful values retain the
 * provider-declared type supplied by the resolver.
 */
int cxpr_resolve_bound_series_value(
    const cxpr_source_plan_bindings* bindings,
    size_t requirement_index,
    const cxpr_series_value_resolver* resolver,
    int64_t evaluation_time_ns,
    size_t evaluation_cursor,
    size_t lookback,
    cxpr_value* out_value);

/** AST callback for registering the cxpr-owned `resample` reference evaluator. */
cxpr_value cxpr_eval_bound_resample(
    const cxpr_expr_ast* call_ast, const cxpr_context* context,
    const cxpr_registry* registry, void* userdata, cxpr_error* err);

/** Lookback callback preserving target-series-relative indexing. */
bool cxpr_eval_bound_resample_lookback(
    const cxpr_expr_ast* target, const cxpr_expr_ast* index,
    const cxpr_context* context, const cxpr_registry* registry,
    void* userdata, cxpr_value* out_value, cxpr_error* err);

#ifdef __cplusplus
}
#endif
