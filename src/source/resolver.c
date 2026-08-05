/** @file resolver.c Reference resolution for pre-bound series requirements. */

#include <cxpr/source.h>
#include "lookback.h"

#include <math.h>
#include <string.h>

int cxpr_validate_generated_resample_bindings(
    const cxpr_source_plan_bindings* bindings,
    cxpr_error* err) {
    if (err) memset(err, 0, sizeof(*err));
    if (!bindings || (bindings->count > 0u && !bindings->requirements)) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Generated resample validation requires an owned requirement manifest";
        }
        return 0;
    }
    for (size_t i = 0u; i < bindings->count; ++i) {
        if (bindings->requirements[i].value_type != CXPR_VALUE_NUMBER) {
            if (err) {
                err->code = CXPR_ERR_TYPE_MISMATCH;
                err->message = "Generated C/CUDA resample ABI v1 supports only scalar numeric series; record/vector series are unsupported";
            }
            return 0;
        }
    }
    return 1;
}

static int cxpr_bound_resample_slot(const cxpr_source_plan_bindings* bindings,
                                    const cxpr_expr_ast* ast, size_t* out_slot,
                                    cxpr_error* err) {
    cxpr_resample_call call = {0};
    const char* source;
    if (!ast || !out_slot) return 0;
    if (!bindings || (bindings->count > 0u &&
                      (!bindings->requirements || !bindings->handles))) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Bound resample evaluation requires requirements and handles";
        }
        return 0;
    }
    if (!cxpr_resample_call_parse(ast, &call, err) || !call.source ||
        cxpr_expr_ast_kind_of(call.source) != CXPR_NODE_IDENTIFIER) return 0;
    source = cxpr_expr_ast_identifier_name(call.source);
    for (size_t i = 0u; i < bindings->count; ++i) {
        const cxpr_series_requirement* requirement = &bindings->requirements[i];
        if (requirement->source_name && strcmp(requirement->source_name, source) == 0 &&
            requirement->every.duration_ns == call.every.duration_ns) {
            *out_slot = i;
            return 1;
        }
    }
    if (err) {
        err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
        err->message = "No bound requirement matches the resample source and interval";
    }
    return 0;
}

int cxpr_resolve_bound_series_value(
    const cxpr_source_plan_bindings* bindings,
    size_t requirement_index,
    const cxpr_series_value_resolver* resolver,
    int64_t evaluation_time_ns,
    size_t evaluation_cursor,
    size_t lookback,
    cxpr_value* out_value) {
    cxpr_value value = cxpr_num(NAN);

    if (out_value) *out_value = value;
    if (!bindings || !resolver || !resolver->resolve || !out_value ||
        requirement_index >= bindings->count || !bindings->handles ||
        !bindings->requirements) {
        return 0;
    }
    if (!resolver->resolve(
            bindings->handles[requirement_index],
            &bindings->requirements[requirement_index],
            evaluation_time_ns,
            evaluation_cursor,
            lookback,
            &value,
            resolver->userdata)) {
        return 0;
    }
    *out_value = value;
    return 1;
}

cxpr_value cxpr_eval_bound_resample(
    const cxpr_expr_ast* call_ast, const cxpr_context* context,
    const cxpr_registry* registry, void* userdata, cxpr_error* err) {
    cxpr_bound_series_evaluator* evaluator = (cxpr_bound_series_evaluator*)userdata;
    cxpr_value value = cxpr_num(NAN);
    size_t slot;
    (void)context;
    (void)registry;
    if (!evaluator || !cxpr_bound_resample_slot(evaluator->bindings, call_ast, &slot, err)) {
        if (err && err->code == CXPR_OK) {
            err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
            err->message = "Unbound resample requirement";
        }
        return value;
    }
    (void)cxpr_resolve_bound_series_value(evaluator->bindings, slot, evaluator->resolver,
                                          evaluator->evaluation_time_ns,
                                          evaluator->evaluation_cursor, 0u, &value);
    return value;
}

bool cxpr_eval_bound_resample_lookback(
    const cxpr_expr_ast* target, const cxpr_expr_ast* index,
    const cxpr_context* context, const cxpr_registry* registry,
    void* userdata, cxpr_value* out_value, cxpr_error* err) {
    cxpr_bound_series_evaluator* evaluator = (cxpr_bound_series_evaluator*)userdata;
    unsigned lookback;
    size_t slot;
    (void)context;
    (void)registry;
    if (!evaluator || !out_value) return false;
    if (!cxpr_bound_resample_slot(evaluator->bindings, target, &slot, err)) {
        if (target && cxpr_expr_ast_kind_of(target) == CXPR_NODE_FUNCTION_CALL &&
            cxpr_expr_ast_call_name(target) &&
            strcmp(cxpr_expr_ast_call_name(target), "resample") == 0) {
            *out_value = cxpr_num(NAN);
            return true;
        }
        return false;
    }
    if (!cxpr_lookback_literal_offset(index, &lookback, err,
                                      "resample lookback must be a non-negative integer")) {
        if (err) err->code = CXPR_ERR_INVALID_INDEX;
        return true;
    }
    (void)cxpr_resolve_bound_series_value(evaluator->bindings, slot, evaluator->resolver,
                                          evaluator->evaluation_time_ns,
                                          evaluator->evaluation_cursor,
                                          (size_t)lookback, out_value);
    return true;
}
