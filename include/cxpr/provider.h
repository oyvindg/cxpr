/**
 * @file provider.h
 * @brief Provider metadata and registration API for host-backed cxpr functions.
 *
 * Providers describe functions and direct sources that are implemented by a
 * host library rather than by cxpr itself. The metadata in this header lets
 * cxpr register parse-time signatures, preserve named-argument shape, expose
 * record fields, and hand runtime values back to the host through callbacks.
 *
 * All strings and arrays referenced by provider specs are borrowed from the
 * provider. They must remain valid for as long as the provider is registered
 * or inspected.
 */

#pragma once

#include <cxpr/ast.h>
#include <cxpr/context.h>
#include <cxpr/registry.h>

#include <stdbool.h>
#include <stddef.h>

struct cxpr_expr_param_spec;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Named parameter descriptor for one provider-visible argument.
 */
typedef struct {
    const char* name; /**< Stable expression-visible parameter name. */
} cxpr_provider_param_descriptor;

/**
 * @brief Metadata for one provider-visible output field.
 */
typedef struct {
    const char* name; /**< Stable field name used in `fn(...).field` access. */
} cxpr_provider_field_descriptor;

/**
 * @brief Optional scope metadata for scoped series variants.
 *
 * A scope is intentionally more general than a trading timeframe. Examples:
 * `timeframe`, `resolution`, `sampling`, `bucket`, `region`, or any other
 * provider-specific series partition key.
 */
typedef struct {
    const char* param_name; /**< Named scope argument, such as `timeframe`, `warehouse`, or `region`. */
    bool optional;          /**< True when calls may omit the scope argument. */
} cxpr_provider_scope_spec;

/**
 * @brief Capability flags for one provider function.
 */
typedef enum {
    CXPR_PROVIDER_FN_RECORD_OUTPUT = 1u << 0, /**< Function exposes named fields via `fn(...).field`. */
    CXPR_PROVIDER_FN_SOURCE_INPUT  = 1u << 1, /**< Function accepts another scalar series as input. */
    CXPR_PROVIDER_FN_REWRITES_HISTORY = 1u << 2, /**< Function may revise previously emitted values. */
} cxpr_provider_fn_flags;

/**
 * @brief Generic function metadata exported by one provider.
 *
 * `min_args` and `max_args` describe the normal numeric/positional call shape.
 * When `CXPR_PROVIDER_FN_SOURCE_INPUT` is set, `source_min_args` and
 * `source_max_args` describe the additional numeric arguments accepted after
 * the leading source expression in source-aware calls such as
 * `ema(close, 14)`.
 */
typedef struct {
    const char* name;                                  /**< Stable expression-visible function name. */
    size_t min_args;                                   /**< Minimum positional/numeric argument count. */
    size_t max_args;                                   /**< Maximum positional/numeric argument count. */
    size_t source_min_args;                            /**< Minimum additional arg count for `name_src*` families. */
    size_t source_max_args;                            /**< Maximum additional arg count for `name_src*` families. */
    const cxpr_provider_param_descriptor* params;      /**< Ordered argument metadata, or NULL. */
    size_t param_count;                                /**< Number of entries in @p params. */
    const cxpr_provider_field_descriptor* fields;      /**< Record output field metadata, or NULL. */
    size_t field_count;                                /**< Number of entries in @p fields. */
    int primary_field_index;                           /**< Preferred default field index, or -1. */
    unsigned flags;                                    /**< Bitwise OR of `cxpr_provider_fn_flags`. */
    const cxpr_provider_scope_spec* scope;             /**< Optional scoped-series scope metadata. */
} cxpr_provider_fn_spec;

/**
 * @brief Generic metadata for one direct source family exported by a provider.
 *
 * Direct sources are base series entrypoints such as `close`, `temperature`,
 * `requests`, or any other runtime-required scalar series.
 */
typedef struct {
    const char* name;                             /**< Stable expression-visible source name. */
    size_t min_args;                              /**< Minimum accepted argument count. */
    size_t max_args;                              /**< Maximum accepted argument count. */
    const cxpr_provider_scope_spec* scope;        /**< Optional scoped-series scope metadata. */
} cxpr_provider_source_spec;

/**
 * @brief Provider callbacks used by the generic cxpr bridge engine.
 *
 * The inventory callbacks return provider-owned arrays. cxpr never frees the
 * returned specs. `expr_param_spec_for` is optional and is used by higher-level
 * expression rewriting/introspection code for richer named-argument metadata.
 */
typedef struct {
    const cxpr_provider_fn_spec* const* (*fn_specs)(const void* userdata, size_t* count);
    const cxpr_provider_fn_spec* (*fn_spec_find)(const void* userdata, const char* name);
    const cxpr_provider_source_spec* const* (*source_specs)(const void* userdata, size_t* count);
    const cxpr_provider_source_spec* (*source_spec_find)(const void* userdata, const char* name);
    int (*expr_param_spec_for)(const void* userdata,
                               const char* name,
                               struct cxpr_expr_param_spec* out);
} cxpr_provider_vtable;

/**
 * @brief One configured provider instance.
 */
typedef struct {
    const char* name;                       /**< Human-readable provider name, or NULL. */
    const void* userdata;                   /**< Provider-owned adapter context, or NULL. */
    const cxpr_provider_vtable* vtable;     /**< Required provider callback table. */
} cxpr_provider;

/**
 * @brief Return whether a provider instance is structurally valid.
 * @param[in] provider Provider instance to inspect.
 * @return Non-zero when the provider has the required callbacks.
 */
int cxpr_provider_is_valid(const cxpr_provider* provider);

/**
 * @brief Return the provider function inventory.
 * @param[in] provider Provider instance.
 * @param[out] count Optional function count output.
 * @return Pointer to the provider-owned function-spec pointer array, or NULL.
 */
const cxpr_provider_fn_spec* const* cxpr_provider_fn_specs(
    const cxpr_provider* provider,
    size_t* count);

/**
 * @brief Find one provider function spec by stable name.
 * @param[in] provider Provider instance.
 * @param[in] name Stable expression-visible function name.
 * @return Matching function spec, or NULL when not found.
 */
const cxpr_provider_fn_spec* cxpr_provider_fn_spec_find(
    const cxpr_provider* provider,
    const char* name);

/**
 * @brief Return the provider direct-source inventory.
 * @param[in] provider Provider instance.
 * @param[out] count Optional source count output.
 * @return Pointer to the provider-owned source-spec pointer array, or NULL.
 */
const cxpr_provider_source_spec* const* cxpr_provider_source_specs(
    const cxpr_provider* provider,
    size_t* count);

/**
 * @brief Find one provider direct-source spec by stable name.
 * @param[in] provider Provider instance.
 * @param[in] name Stable expression-visible source name.
 * @return Matching source spec, or NULL when not found.
 */
const cxpr_provider_source_spec* cxpr_provider_source_spec_find(
    const cxpr_provider* provider,
    const char* name);

int cxpr_provider_expr_param_spec_for(
    const cxpr_provider* provider,
    const char* name,
    struct cxpr_expr_param_spec* out);

/**
 * @brief Resolve one host-provided scalar at runtime.
 * @param[in] name Provider-visible scalar or field name being requested.
 * @param[in] args Evaluated numeric call arguments.
 * @param[in] argc Number of entries in @p args.
 * @param[in] userdata Opaque pointer from @ref cxpr_host_config.
 * @return Resolved scalar value. Return `NAN` when the host cannot resolve it.
 */
typedef double (*cxpr_runtime_required_scalar_fn)(
    const char* name,
    const double* args,
    size_t argc,
    void* userdata);

/**
 * @brief Report a scoped-source resolver failure to the host.
 * @param[in] resolver_userdata Opaque pointer from @ref cxpr_scope_resolver.
 * @param[in] userdata Opaque pointer from @ref cxpr_host_config.
 */
typedef void (*cxpr_scope_error_fn)(
    void* resolver_userdata,
    void* userdata);

/**
 * @brief Override the host-visible arity for one provider function.
 * @param[in] fn_spec Provider function metadata being registered.
 * @param[out] min_args Receives the overridden minimum arity.
 * @param[out] max_args Receives the overridden maximum arity.
 * @param[in] userdata Opaque pointer from @ref cxpr_host_config.
 * @return Non-zero when an override was written, zero to use default rules.
 */
typedef int (*cxpr_arg_range_override_fn)(
    const cxpr_provider_fn_spec* fn_spec,
    size_t* min_args,
    size_t* max_args,
    void* userdata);

/**
 * @brief Decide whether host metadata should omit one source descriptor.
 * @param[in] fn_spec Provider function metadata being inspected.
 * @param[in] userdata Opaque pointer from @ref cxpr_host_config.
 * @return Non-zero to skip descriptor emission, zero to include it.
 */
typedef int (*cxpr_skip_source_descriptor_fn)(
    const cxpr_provider_fn_spec* fn_spec,
    void* userdata);

/**
 * @brief Host callbacks and policy hooks used during provider registration.
 */
typedef struct cxpr_host_config {
    cxpr_runtime_required_scalar_fn runtime_required_scalar; /**< Runtime scalar resolver. */
    cxpr_scope_error_fn raise_scope_resolver_error; /**< Optional scoped-source error hook. */
    cxpr_arg_range_override_fn override_arg_range; /**< Optional arity override hook. */
    cxpr_skip_source_descriptor_fn skip_source_descriptor; /**< Optional metadata filter hook. */
    void* userdata; /**< Host-owned pointer passed to callbacks. */
} cxpr_host_config;

/**
 * @brief Expression-level argument kind for rich provider named-arg metadata.
 */
typedef enum cxpr_expr_arg_kind {
    CXPR_EXPR_ARG_NUMERIC = 0,       /**< Argument evaluates to a numeric scalar. */
    CXPR_EXPR_ARG_SCALAR_SOURCE = 1, /**< Argument is a scalar source expression. */
} cxpr_expr_arg_kind;

/**
 * @brief Rich named-argument metadata for one expression-visible function.
 *
 * Arrays are provider-owned and borrowed by the caller. `defaults` may be NULL,
 * or may contain NULL entries for parameters without defaults. `kinds` may be
 * NULL when all arguments are numeric. `has_timeframe_param` is retained for
 * compatibility with cxta-style timeframe selectors; generic providers should
 * prefer @ref cxpr_provider_scope_spec for scope metadata.
 */
typedef struct cxpr_expr_param_spec {
    const char* const* names;        /**< Ordered parameter names, or NULL when count is 0. */
    const char* const* defaults;     /**< Parallel default strings, or NULL. */
    const cxpr_expr_arg_kind* kinds; /**< Parallel argument kinds, or NULL for all-numeric. */
    size_t count;                    /**< Number of named parameters. */
    size_t min_count;                /**< Minimum required named parameters. */
    const char* lookback_sugar_name; /**< Optional subscript sugar target, such as `obv()[n]`. */
    int has_timeframe_param;         /**< Non-zero when a trailing timeframe selector is supported. */
} cxpr_expr_param_spec;

/**
 * @brief Register one provider function signature in a cxpr registry.
 * @param[out] reg Registry receiving the function signature.
 * @param[in] spec Provider-owned function metadata.
 * @param[in] host Optional host callbacks and policy hooks.
 * @return Non-zero on success, zero on validation or registration failure.
 */
int cxpr_register_provider_fn_spec(
    cxpr_registry* reg,
    const cxpr_provider_fn_spec* spec,
    const cxpr_host_config* host);

/**
 * @brief Compute the arity exposed to cxpr for one provider function.
 * @param[in] spec Provider function metadata.
 * @param[in] host Optional host overrides.
 * @param[out] min_args Receives minimum accepted argument count.
 * @param[out] max_args Receives maximum accepted argument count.
 *
 * The result includes provider scope arguments and source-input forms. Host
 * overrides, when supplied, take precedence.
 */
void cxpr_provider_host_visible_arg_range(
    const cxpr_provider_fn_spec* spec,
    const cxpr_host_config* host,
    size_t* min_args,
    size_t* max_args);

/**
 * @brief Register all function and direct-source signatures from one provider.
 * @param[out] reg Registry receiving provider signatures.
 * @param[in] provider Provider inventory and callbacks.
 * @param[in] host Optional host callbacks and policy hooks.
 */
void cxpr_register_provider_signatures(
    cxpr_registry* reg,
    const cxpr_provider* provider,
    const cxpr_host_config* host);

#ifdef __cplusplus
}
#endif
