/**
 * @file provider.c
 * @brief Generic provider helpers for cxpr.
 */

#include <cxpr/provider.h>

int cxpr_provider_is_valid(const cxpr_provider* provider) {
    return provider != NULL &&
           provider->vtable != NULL &&
           provider->vtable->fn_specs != NULL &&
           provider->vtable->fn_spec_find != NULL &&
           provider->vtable->source_specs != NULL &&
           provider->vtable->source_spec_find != NULL;
}

const cxpr_provider_fn_spec* const* cxpr_provider_fn_specs(
    const cxpr_provider* provider,
    size_t* count) {
    if (count) *count = 0u;
    if (!cxpr_provider_is_valid(provider)) return NULL;
    return provider->vtable->fn_specs(provider->userdata, count);
}

const cxpr_provider_fn_spec* cxpr_provider_fn_spec_find(
    const cxpr_provider* provider,
    const char* name) {
    if (!cxpr_provider_is_valid(provider) || !name) return NULL;
    return provider->vtable->fn_spec_find(provider->userdata, name);
}

const cxpr_provider_source_spec* const* cxpr_provider_source_specs(
    const cxpr_provider* provider,
    size_t* count) {
    if (count) *count = 0u;
    if (!cxpr_provider_is_valid(provider)) return NULL;
    return provider->vtable->source_specs(provider->userdata, count);
}

const cxpr_provider_source_spec* cxpr_provider_source_spec_find(
    const cxpr_provider* provider,
    const char* name) {
    if (!cxpr_provider_is_valid(provider) || !name) return NULL;
    return provider->vtable->source_spec_find(provider->userdata, name);
}

int cxpr_provider_expr_param_spec_for(
    const cxpr_provider* provider,
    const char* name,
    struct cxpr_expr_param_spec* out) {
    const cxpr_provider_source_spec* source_spec;

    if (!cxpr_provider_is_valid(provider) || !name || !out) return 0;
    if (provider->vtable->expr_param_spec_for &&
        provider->vtable->expr_param_spec_for(provider->userdata, name, out)) {
        return 1;
    }

    source_spec = cxpr_provider_source_spec_find(provider, name);
    if (source_spec && source_spec->scope && source_spec->scope->param_name &&
        source_spec->scope->param_name[0] != '\0') {
        static const char* names[1];

        names[0] = source_spec->scope->param_name;
        out->names = names;
        out->defaults = NULL;
        out->kinds = NULL;
        out->count = 1u;
        out->min_count = source_spec->scope->optional ? 0u : 1u;
        out->lookback_sugar_name = NULL;
        out->has_timeframe_param = 0;
        return 1;
    }

    return 0;
}
