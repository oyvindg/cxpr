/**
 * @file scope.h
 * @brief Runtime registration API for host-resolved scoped source functions.
 *
 * This is the low-level runtime side of provider sources. It registers source
 * names such as `close` or `temperature` in a cxpr registry and delegates value
 * lookup to a host resolver. Higher-level code may parse expressions such as
 * `close(selector="1d")[7]` into a host-specific handle before evaluation.
 */

#pragma once

#include <cxpr/provider.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cxpr_host_config;

/**
 * @brief Resolve one scoped source handle to a numeric value.
 * @param[in] handle Host-defined source handle or lookback/index value.
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
    cxpr_scope_resolver_fn resolve; /**< Required resolver callback. */
    void* userdata;                 /**< Host-owned pointer passed to @ref resolve. */
} cxpr_scope_resolver;

/**
 * @brief Runtime registration metadata for one scoped source family.
 */
typedef struct {
    const char* name;                         /**< Expression-visible source name. */
    size_t min_args;                          /**< Minimum runtime argument count. */
    size_t max_args;                          /**< Maximum runtime argument count. */
    const cxpr_provider_scope_spec* scope;    /**< Optional provider scope metadata. */
} cxpr_scoped_source_spec;

/**
 * @brief Register runtime scoped-source functions into a cxpr registry.
 * @param[out] reg Registry receiving runtime scoped-source functions.
 * @param[in] specs Provider/source family metadata to expose.
 * @param[in] spec_count Number of entries in @p specs.
 * @param[in] resolver Runtime resolver used by scoped-source adapters.
 * @param[in] host Optional host callback for propagating resolver failures.
 *
 * Each spec registers a scalar function under `spec.name`. The generated
 * function forwards its first numeric argument as the resolver handle, or `0`
 * when called without arguments.
 */
void cxpr_scoped_source_functions_register(
    cxpr_registry* reg,
    const cxpr_scoped_source_spec* specs,
    size_t spec_count,
    const cxpr_scope_resolver* resolver,
    const struct cxpr_host_config* host);

#ifdef __cplusplus
}
#endif
