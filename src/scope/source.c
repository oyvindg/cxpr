/**
 * @file source.c
 * @brief Runtime registration for host-resolved scoped source functions.
 */

#include <cxpr/scope.h>
#include <cxpr/provider.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* source_name;
    cxpr_scope_resolver resolver;
    cxpr_scope_error_fn raise_error;
    void* host_userdata;
} cxpr_scoped_source_context;

static void cxpr_scoped_source_context_free(void* userdata) {
    cxpr_scoped_source_context* ctx = userdata;
    if (!ctx) return;
    free(ctx->source_name);
    free(ctx);
}

static double cxpr_scoped_source_scalar_adapter(const double* args,
                                                size_t argc,
                                                void* userdata) {
    cxpr_scoped_source_context* ctx = userdata;
    const uint64_t handle = (!args || argc == 0u) ? 0u : (uint64_t)args[0];
    double value = 0.0;

    if (!ctx || !ctx->resolver.resolve || !ctx->source_name) return 0.0;
    if (!ctx->resolver.resolve(handle, ctx->source_name, &value, ctx->resolver.userdata)) {
        if (ctx->raise_error) {
            ctx->raise_error(ctx->resolver.userdata, ctx->host_userdata);
        }
        return 0.0;
    }
    return value;
}

static void cxpr_scoped_source_binding_register(
    cxpr_registry* reg,
    const char* name,
    size_t min_args,
    size_t max_args,
    const char* source_name,
    const cxpr_scope_resolver* resolver,
    const cxpr_host_config* host) {
    cxpr_scoped_source_context* ctx;
    size_t source_name_len;

    if (!reg || !name || !source_name || !resolver || !resolver->resolve) return;

    ctx = malloc(sizeof(*ctx));
    if (!ctx) return;
    source_name_len = strlen(source_name);
    ctx->source_name = malloc(source_name_len + 1u);
    if (!ctx->source_name) {
        free(ctx);
        return;
    }

    memcpy(ctx->source_name, source_name, source_name_len + 1u);
    ctx->resolver = *resolver;
    ctx->raise_error = host ? host->raise_scope_resolver_error : NULL;
    ctx->host_userdata = host ? host->userdata : NULL;

    cxpr_registry_add(
        reg,
        name,
        cxpr_scoped_source_scalar_adapter,
        min_args,
        max_args,
        ctx,
        cxpr_scoped_source_context_free);
}

static void cxpr_scoped_source_family_register(
    cxpr_registry* reg,
    const cxpr_scoped_source_spec* spec,
    const cxpr_scope_resolver* resolver,
    const cxpr_host_config* host) {
    if (!reg || !spec || !spec->name || !resolver) return;

    cxpr_scoped_source_binding_register(
        reg,
        spec->name,
        spec->min_args,
        spec->max_args,
        spec->name,
        resolver,
        host);
}

void cxpr_scoped_source_functions_register(
    cxpr_registry* reg,
    const cxpr_scoped_source_spec* specs,
    size_t spec_count,
    const cxpr_scope_resolver* resolver,
    const cxpr_host_config* host) {
    size_t i;

    if (!reg || !specs || spec_count == 0u || !resolver || !resolver->resolve) return;
    for (i = 0u; i < spec_count; ++i) {
        cxpr_scoped_source_family_register(reg, &specs[i], resolver, host);
    }
}
