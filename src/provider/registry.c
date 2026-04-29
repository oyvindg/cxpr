/**
 * @file registry.c
 * @brief Bridge signature registration implementation.
 */

#include <cxpr/provider.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CXPR_ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))
#define CXPR_NAME_BUFFER_SIZE 256u

typedef struct {
    cxpr_runtime_required_scalar_fn callback;
    void* host_userdata;
    char* name;
} cxpr_provider_runtime_required_context;

typedef struct {
    cxpr_runtime_required_scalar_fn callback;
    void* host_userdata;
    char* name;
    char** field_names;
    size_t field_count;
} cxpr_provider_runtime_required_struct_context;

static double cxpr_provider_runtime_required_default(const char* name,
                                                        const double* args,
                                                        size_t argc,
                                                        void* userdata) {
    (void)name;
    (void)args;
    (void)argc;
    (void)userdata;
    return NAN;
}

static double cxpr_provider_runtime_required_adapter(const double* args,
                                                        size_t argc,
                                                        void* userdata) {
    cxpr_provider_runtime_required_context* ctx = userdata;
    const cxpr_runtime_required_scalar_fn callback =
        (ctx && ctx->callback)
            ? ctx->callback
            : cxpr_provider_runtime_required_default;
    return callback(ctx ? ctx->name : NULL, args, argc, ctx ? ctx->host_userdata : NULL);
}

static void cxpr_provider_runtime_required_context_free(void* userdata) {
    cxpr_provider_runtime_required_context* ctx = userdata;
    if (!ctx) return;
    free(ctx->name);
    free(ctx);
}

static void cxpr_provider_runtime_required_struct_adapter(
    const double* args,
    size_t argc,
    cxpr_value* out,
    size_t field_count,
    void* userdata) {
    cxpr_provider_runtime_required_struct_context* ctx = userdata;
    const cxpr_runtime_required_scalar_fn callback =
        (ctx && ctx->callback)
            ? ctx->callback
            : cxpr_provider_runtime_required_default;
    size_t i;

    if (!out) return;
    for (i = 0u; i < field_count; ++i) {
        char full_name[CXPR_NAME_BUFFER_SIZE];
        int written;
        const char* field_name =
            (ctx && i < ctx->field_count && ctx->field_names) ? ctx->field_names[i] : NULL;
        if (!ctx || !ctx->name || !field_name || field_name[0] == '\0') {
            out[i] = cxpr_fv_double(NAN);
            continue;
        }
        written = snprintf(full_name, sizeof(full_name), "%s.%s", ctx->name, field_name);
        if (written <= 0 || (size_t)written >= sizeof(full_name)) {
            out[i] = cxpr_fv_double(NAN);
            continue;
        }
        out[i] = cxpr_fv_double(callback(full_name, args, argc, ctx->host_userdata));
    }
}

static void cxpr_provider_runtime_required_struct_context_free(void* userdata) {
    cxpr_provider_runtime_required_struct_context* ctx = userdata;
    size_t i;
    if (!ctx) return;
    if (ctx->field_names) {
        for (i = 0u; i < ctx->field_count; ++i) free(ctx->field_names[i]);
        free(ctx->field_names);
    }
    free(ctx->name);
    free(ctx);
}

static void cxpr_provider_runtime_required_struct_register(
    cxpr_registry* reg,
    const cxpr_provider_fn_spec* spec,
    size_t min_args,
    size_t max_args,
    const cxpr_host_config* host) {
    cxpr_provider_runtime_required_struct_context* ctx;
    const cxpr_runtime_required_scalar_fn callback =
        (host && host->runtime_required_scalar)
            ? host->runtime_required_scalar
            : cxpr_provider_runtime_required_default;
    size_t i;
    size_t len;

    if (!reg || !spec || !spec->name || spec->name[0] == '\0') return;
    if (!spec->fields || spec->field_count == 0u) return;

    len = strlen(spec->name);
    ctx = calloc(1u, sizeof(*ctx));
    if (!ctx) return;
    ctx->callback = callback;
    ctx->host_userdata = host ? host->userdata : NULL;
    ctx->field_count = spec->field_count;
    ctx->name = malloc(len + 1u);
    ctx->field_names = calloc(spec->field_count, sizeof(*ctx->field_names));
    if (!ctx->name || !ctx->field_names) {
        cxpr_provider_runtime_required_struct_context_free(ctx);
        return;
    }
    memcpy(ctx->name, spec->name, len + 1u);
    for (i = 0u; i < spec->field_count; ++i) {
        size_t field_len;
        if (!spec->fields[i].name || spec->fields[i].name[0] == '\0') {
            cxpr_provider_runtime_required_struct_context_free(ctx);
            return;
        }
        field_len = strlen(spec->fields[i].name);
        ctx->field_names[i] = malloc(field_len + 1u);
        if (!ctx->field_names[i]) {
            cxpr_provider_runtime_required_struct_context_free(ctx);
            return;
        }
        memcpy(ctx->field_names[i], spec->fields[i].name, field_len + 1u);
    }

    cxpr_registry_add(
        reg,
        spec->name,
        cxpr_provider_runtime_required_adapter,
        min_args,
        max_args,
        ctx,
        cxpr_provider_runtime_required_struct_context_free);

    cxpr_registry_add_struct(
        reg,
        spec->name,
        cxpr_provider_runtime_required_struct_adapter,
        min_args,
        max_args,
        (const char* const*)ctx->field_names,
        ctx->field_count,
        ctx,
        NULL);
}

static void cxpr_provider_runtime_required_register(cxpr_registry* reg,
                                                       const char* name,
                                                       size_t min_args,
                                                       size_t max_args,
                                                       const cxpr_host_config* host) {
    cxpr_provider_runtime_required_context* ctx;
    const cxpr_runtime_required_scalar_fn callback =
        (host && host->runtime_required_scalar)
            ? host->runtime_required_scalar
            : cxpr_provider_runtime_required_default;
    const size_t len = name ? strlen(name) : 0u;

    if (!reg || !name) return;
    if (len == 0u) return;

    ctx = calloc(1u, sizeof(*ctx));
    if (!ctx) return;
    ctx->callback = callback;
    ctx->host_userdata = host ? host->userdata : NULL;
    ctx->name = malloc(len + 1u);
    if (!ctx->name) {
        free(ctx);
        return;
    }
    memcpy(ctx->name, name, len + 1u);

    cxpr_registry_add(
        reg,
        name,
        cxpr_provider_runtime_required_adapter,
        min_args,
        max_args,
        ctx,
        cxpr_provider_runtime_required_context_free);
}

static void cxpr_provider_signature_family_add(cxpr_registry* reg,
                                             const char* name,
                                             size_t min_args,
                                             size_t max_args,
                                             const cxpr_host_config* host) {
    cxpr_provider_runtime_required_register(reg, name, min_args, max_args, host);
}

int cxpr_register_provider_fn_spec(
    cxpr_registry* reg,
    const cxpr_provider_fn_spec* spec,
    const cxpr_host_config* host) {
    const char* param_names[64];
    size_t min_args = 0u;
    size_t max_args = 0u;
    size_t total_param_count = 0u;

    if (!reg || !spec || !spec->name || spec->name[0] == '\0') return 0;
    cxpr_provider_host_visible_arg_range(spec, host, &min_args, &max_args);
    if (max_args < min_args) return 0;

    if (spec->field_count > 0u && spec->fields) {
        cxpr_provider_runtime_required_struct_register(reg, spec, min_args, max_args, host);
    } else {
        cxpr_provider_runtime_required_register(
            reg, spec->name, min_args, max_args, host);
    }

    if (!spec->params || spec->param_count == 0u) return 1;

    if (spec->param_count > CXPR_ARRAY_COUNT(param_names)) return 0;
    if ((spec->flags & CXPR_PROVIDER_FN_SOURCE_INPUT) != 0u) {
        param_names[total_param_count++] = "source";
    }
    for (size_t i = 0u; i < spec->param_count; ++i) {
        if (!spec->params[i].name || spec->params[i].name[0] == '\0') return 0;
        param_names[total_param_count++] = spec->params[i].name;
    }
    if (spec->scope && spec->scope->param_name && spec->scope->param_name[0] != '\0') {
        if (total_param_count >= CXPR_ARRAY_COUNT(param_names)) return 0;
        param_names[total_param_count++] = spec->scope->param_name;
    }
    if (total_param_count < max_args) return 1;

    return cxpr_registry_set_param_names(reg, spec->name, param_names, total_param_count) ? 1 : 0;
}

static int cxpr_provider_should_skip_source_descriptor(
    const cxpr_provider_fn_spec* spec,
    const cxpr_host_config* host) {
    if (!spec || !host || !host->skip_source_descriptor) return 0;
    return host->skip_source_descriptor(spec, host->userdata) != 0;
}

void cxpr_provider_host_visible_arg_range(
    const cxpr_provider_fn_spec* spec,
    const cxpr_host_config* host,
    size_t* min_args,
    size_t* max_args) {
    if (!spec || !min_args || !max_args) return;

    if (host && host->override_arg_range &&
        host->override_arg_range(spec, min_args, max_args, host->userdata) != 0) {
        return;
    }

    *min_args = spec->min_args;
    *max_args = spec->max_args;
    if (spec->scope && spec->scope->param_name && spec->scope->param_name[0] != '\0') {
        *max_args += 1u;
        if (!spec->scope->optional) {
            *min_args += 1u;
        }
    }
    if ((spec->flags & CXPR_PROVIDER_FN_SOURCE_INPUT) != 0u) {
        const size_t source_min_args = 1u + spec->source_min_args;
        const size_t source_max_args = 1u + spec->source_max_args;
        if (source_min_args < *min_args) *min_args = source_min_args;
        if (source_max_args > *max_args) *max_args = source_max_args;
    }
}

void cxpr_register_provider_signatures(
    cxpr_registry* reg,
    const cxpr_provider* provider,
    const cxpr_host_config* host) {
    const cxpr_provider_fn_spec* const* fn_specs;
    const cxpr_provider_source_spec* const* source_specs;
    size_t fn_count = 0u;
    size_t source_count = 0u;
    size_t source_index;

    if (!reg || !cxpr_provider_is_valid(provider)) return;

    fn_specs = cxpr_provider_fn_specs(provider, &fn_count);
    source_specs = cxpr_provider_source_specs(provider, &source_count);
    if (!fn_specs) return;

    for (source_index = 0u; source_index < fn_count; ++source_index) {
        const cxpr_provider_fn_spec* spec = fn_specs[source_index];
        size_t min_args = 0u;
        size_t max_args = 0u;
        size_t field_min_args = 0u;
        size_t field_max_args = 0u;
        size_t field_index;

        if (!spec) continue;
        cxpr_provider_host_visible_arg_range(spec, host, &min_args, &max_args);
        field_min_args = spec->min_args;
        field_max_args = spec->max_args;
        if (host && host->override_arg_range) {
            (void)host->override_arg_range(
                spec, &field_min_args, &field_max_args, host->userdata);
        }
        (void)cxpr_register_provider_fn_spec(reg, spec, host);

        if (!spec->fields && spec->field_count > 0u) continue;
        for (field_index = 0u; field_index < spec->field_count; ++field_index) {
            const cxpr_provider_field_descriptor* field = &spec->fields[field_index];
            char field_name[CXPR_NAME_BUFFER_SIZE];
            int written;

            if (!field->name || field->name[0] == '\0') continue;
            written = snprintf(field_name, sizeof(field_name), "%s.%s", spec->name, field->name);
            if (written <= 0 || (size_t)written >= sizeof(field_name)) continue;
            cxpr_provider_signature_family_add(reg, field_name, field_min_args, field_max_args, host);
        }
    }

    for (source_index = 0u; source_index < source_count; ++source_index) {
        const cxpr_provider_source_spec* source = source_specs ? source_specs[source_index] : NULL;
        if (!source || !source->name || source->name[0] == '\0') continue;
        cxpr_provider_runtime_required_register(
            reg, source->name, source->min_args, source->max_args, host);
    }
}
