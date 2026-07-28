#include "model/internal.h"
#include <stdlib.h>
#include <string.h>

static bool cxpr_model_source_bindings_append(cxpr_source_plan_bindings* out,
                                              const cxpr_source_plan_bindings* part,
                                              cxpr_error* err) {
    uint64_t* grown;
    if (!out || !part || part->count == 0u) return true;
    grown = (uint64_t*)realloc(out->handles,
                               (out->count + part->count) * sizeof(uint64_t));
    if (!grown) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    out->handles = grown;
    memcpy(out->handles + out->count, part->handles, part->count * sizeof(uint64_t));
    out->count += part->count;
    return true;
}

static bool cxpr_model_plan_bind_ast_sources(const cxpr_provider* provider,
                                             const cxpr_expr_ast* expr,
                                             const cxpr_context* ctx,
                                             cxpr_registry* reg,
                                             const cxpr_plan_config* config,
                                             cxpr_source_plan_bindings* out,
                                             cxpr_error* err) {
    cxpr_source_plan_bindings part = {0};
    bool ok;
    if (!expr) return true;
    if (!cxpr_plan_bind_sources(provider, expr, ctx, reg, config, &part, err)) {
        return false;
    }
    ok = cxpr_model_source_bindings_append(out, &part, err);
    cxpr_free_source_plan_bindings(&part);
    return ok;
}

bool cxpr_model_plan_bind_sources(const cxpr_model* model,
                                  const cxpr_provider* provider,
                                  const cxpr_context* ctx,
                                  cxpr_registry* reg,
                                  const cxpr_plan_config* config,
                                  cxpr_source_plan_bindings* out,
                                  cxpr_error* err) {
    cxpr_source_plan_bindings tmp = {0};

    if (err) *err = (cxpr_error){0};
    if (out) memset(out, 0, sizeof(*out));
    if (!model || !provider || !ctx || !config || !config->bind || !out) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid model source plan arguments", 0, 0);
        return false;
    }

    for (size_t i = 0; i < model->constant_count; ++i) {
        if (!cxpr_model_plan_bind_ast_sources(provider, model->constants[i].expr,
                                              ctx, reg, config, &tmp, err)) {
            cxpr_free_source_plan_bindings(&tmp);
            return false;
        }
    }
    for (size_t i = 0; i < model->binding_count; ++i) {
        if (!cxpr_model_plan_bind_ast_sources(provider, model->bindings[i].expr,
                                              ctx, reg, config, &tmp, err)) {
            cxpr_free_source_plan_bindings(&tmp);
            return false;
        }
    }
    for (size_t i = 0; i < model->record_function_count; ++i) {
        for (size_t f = 0; f < model->record_functions[i].field_count; ++f) {
            if (!cxpr_model_plan_bind_ast_sources(
                    provider,
                    model->record_functions[i].fields[f].expr,
                    ctx,
                    reg,
                    config,
                    &tmp,
                    err)) {
                cxpr_free_source_plan_bindings(&tmp);
                return false;
            }
        }
    }

    *out = tmp;
    if (err) err->code = CXPR_OK;
    return true;
}
