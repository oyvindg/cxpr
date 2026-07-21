#include "model/internal.h"
#include "registry/internal.h"
#include <stdlib.h>
#include <string.h>

static bool cxpr_model_reference_matches_symbol(const char* reference, const char* symbol) {
    size_t len;
    if (!reference || !symbol) return false;
    if (strcmp(reference, symbol) == 0) return true;
    len = strlen(symbol);
    return strncmp(reference, symbol, len) == 0 && reference[len] == '.';
}

static bool cxpr_model_string_exists(char* const* values, size_t count, const char* name) {
    for (size_t i = 0; i < count; ++i) {
        if (cxpr_model_names_match(values[i], name)) return true;
    }
    return false;
}

static bool cxpr_model_constant_exists(const cxpr_model* model, const char* name) {
    if (!model) return false;
    for (size_t i = 0; i < model->constant_count; ++i) {
        if (cxpr_model_reference_matches_symbol(name, model->constants[i].name)) return true;
    }
    return false;
}

static bool cxpr_model_state_exists(const cxpr_model* model, const char* name) {
    if (!model) return false;
    for (size_t i = 0; i < model->binding_count; ++i) {
        if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE &&
            cxpr_model_names_match(model->bindings[i].name, name)) {
            return true;
        }
    }
    return false;
}

static bool cxpr_model_reference_exists(const cxpr_model* model, const char* reference) {
    if (!model || !reference) return false;
    for (size_t i = 0; i < model->input_count; ++i) {
        if (cxpr_model_reference_matches_symbol(reference, model->inputs[i])) return true;
    }
    for (size_t i = 0; i < model->binding_count; ++i) {
        if (cxpr_model_reference_matches_symbol(reference, model->bindings[i].name)) return true;
    }
    return false;
}

static bool cxpr_model_external_reference_exists(char* const* external_refs,
                                                 size_t external_ref_count,
                                                 const char* reference) {
    if (!reference) return false;
    for (size_t i = 0; i < external_ref_count; ++i) {
        if (cxpr_model_reference_matches_symbol(reference, external_refs[i])) return true;
    }
    return false;
}

static bool cxpr_model_local_reference_exists(const char* const* local_refs,
                                              size_t local_ref_count,
                                              const char* reference) {
    if (!reference) return false;
    for (size_t i = 0; i < local_ref_count; ++i) {
        if (cxpr_model_reference_matches_symbol(reference, local_refs[i])) return true;
    }
    return false;
}

static bool cxpr_model_record_root_field_exists(const cxpr_model* model,
                                                char* const* external_refs,
                                                size_t external_ref_count,
                                                const char* root,
                                                const char* field) {
    char key[256];
    int n;
    if (!root || !root[0] || !field || !field[0]) return false;
    n = snprintf(key, sizeof(key), "%s.%s", root, field);
    if (n < 0 || (size_t)n >= sizeof(key)) return false;
    return cxpr_model_reference_exists(model, key) ||
           cxpr_model_external_reference_exists(external_refs, external_ref_count, key);
}

static bool cxpr_model_function_call_accepts_record_root(
    const cxpr_model* model,
    const cxpr_ast* call,
    cxpr_registry* function_registry,
    char* const* external_refs,
    size_t external_ref_count,
    const char* reference) {
    cxpr_func_entry* entry;
    if (!call || cxpr_ast_type(call) != CXPR_NODE_FUNCTION_CALL ||
        !function_registry || !reference) {
        return false;
    }
    entry = cxpr_registry_find(function_registry, cxpr_ast_function_name(call));
    if (!entry ||
        !entry->defined_param_fields ||
        entry->defined_param_count != cxpr_ast_function_argc(call)) {
        return false;
    }
    for (size_t arg_i = 0u; arg_i < cxpr_ast_function_argc(call); ++arg_i) {
        const cxpr_ast* arg = cxpr_ast_function_arg(call, arg_i);
        if (!arg ||
            cxpr_ast_type(arg) != CXPR_NODE_IDENTIFIER ||
            !cxpr_model_names_match(cxpr_ast_identifier_name(arg), reference)) {
            continue;
        }
        if (entry->defined_param_field_counts[arg_i] == 0u) return false;
        for (size_t field_i = 0u;
             field_i < entry->defined_param_field_counts[arg_i];
             ++field_i) {
            if (!cxpr_model_record_root_field_exists(
                    model,
                    external_refs,
                    external_ref_count,
                    reference,
                    entry->defined_param_fields[arg_i][field_i])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static bool cxpr_model_expr_accepts_record_root_ref(
    const cxpr_model* model,
    const cxpr_ast* expr,
    cxpr_registry* function_registry,
    char* const* external_refs,
    size_t external_ref_count,
    const char* reference) {
    if (!expr || !reference) return false;
    if (cxpr_model_function_call_accepts_record_root(
            model, expr, function_registry, external_refs, external_ref_count, reference)) {
        return true;
    }
    switch (cxpr_ast_type(expr)) {
    case CXPR_NODE_RECORD:
        for (size_t i = 0u; i < cxpr_ast_record_field_count(expr); ++i) {
            if (cxpr_model_expr_accepts_record_root_ref(
                    model, cxpr_ast_record_field_value(expr, i), function_registry,
                    external_refs, external_ref_count, reference)) {
                return true;
            }
        }
        return false;
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_expr_accepts_record_root_ref(
                   model, cxpr_ast_left(expr), function_registry,
                   external_refs, external_ref_count, reference) ||
               cxpr_model_expr_accepts_record_root_ref(
                   model, cxpr_ast_right(expr), function_registry,
                   external_refs, external_ref_count, reference);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_expr_accepts_record_root_ref(
            model, cxpr_ast_operand(expr), function_registry,
            external_refs, external_ref_count, reference);
    case CXPR_NODE_FUNCTION_CALL:
        for (size_t i = 0u; i < cxpr_ast_function_argc(expr); ++i) {
            if (cxpr_model_expr_accepts_record_root_ref(
                    model, cxpr_ast_function_arg(expr, i), function_registry,
                    external_refs, external_ref_count, reference)) {
                return true;
            }
        }
        return false;
    case CXPR_NODE_PRODUCER_ACCESS:
        for (size_t i = 0u; i < cxpr_ast_producer_argc(expr); ++i) {
            if (cxpr_model_expr_accepts_record_root_ref(
                    model, cxpr_ast_producer_arg(expr, i), function_registry,
                    external_refs, external_ref_count, reference)) {
                return true;
            }
        }
        return false;
    case CXPR_NODE_LOOKBACK:
        return cxpr_model_expr_accepts_record_root_ref(
                   model, cxpr_ast_lookback_target(expr), function_registry,
                   external_refs, external_ref_count, reference) ||
               cxpr_model_expr_accepts_record_root_ref(
                   model, cxpr_ast_lookback_index(expr), function_registry,
                   external_refs, external_ref_count, reference);
    case CXPR_NODE_TERNARY:
        return cxpr_model_expr_accepts_record_root_ref(
                   model, cxpr_ast_ternary_condition(expr), function_registry,
                   external_refs, external_ref_count, reference) ||
               cxpr_model_expr_accepts_record_root_ref(
                   model, cxpr_ast_ternary_true_branch(expr), function_registry,
                   external_refs, external_ref_count, reference) ||
               cxpr_model_expr_accepts_record_root_ref(
                   model, cxpr_ast_ternary_false_branch(expr), function_registry,
                   external_refs, external_ref_count, reference);
    default:
        return false;
    }
}

static bool cxpr_model_public_symbol_exists(const cxpr_model* model, const char* name) {
    if (cxpr_model_string_exists(model->inputs, model->input_count, name)) return true;
    if (!model || !name) return false;
    for (size_t i = 0; i < model->binding_count; ++i) {
        if (model->bindings[i].kind != CXPR_MODEL_BINDING_LOCAL &&
            cxpr_model_names_match(model->bindings[i].name, name)) {
            return true;
        }
    }
    return false;
}

static bool cxpr_model_validate_unique_strings(char* const* values, size_t count,
                                               const char* message, cxpr_error* err) {
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            if (cxpr_model_names_match(values[i], values[j])) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, message, 0, 0);
                return false;
            }
        }
    }
    return true;
}

static bool cxpr_model_validate_symbols(const cxpr_model* model, cxpr_error* err) {
    if (!cxpr_model_validate_unique_strings(model->uses, model->use_count,
                                            "Duplicate use import", err)) {
        return false;
    }
    for (size_t i = 0; i < model->use_count; ++i) {
        const char* ns_i = model->use_aliases && model->use_aliases[i]
                               ? model->use_aliases[i]
                               : model->uses[i];
        for (size_t j = i + 1u; j < model->use_count; ++j) {
            const char* ns_j = model->use_aliases && model->use_aliases[j]
                                   ? model->use_aliases[j]
                                   : model->uses[j];
            if (cxpr_model_names_match(ns_i, ns_j)) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Duplicate use namespace", 0, 0);
                return false;
            }
        }
    }
    if (!cxpr_model_validate_unique_strings(model->inputs, model->input_count,
                                            "Duplicate input", err)) {
        return false;
    }
    if (!cxpr_model_validate_unique_strings(model->outputs, model->output_count,
                                            "Duplicate output", err)) {
        return false;
    }

    for (size_t i = 0; i < model->function_count; ++i) {
        const char* open_i = strchr(model->functions[i], '(');
        size_t len_i = open_i ? (size_t)(open_i - model->functions[i]) : strlen(model->functions[i]);
        for (size_t j = i + 1; j < model->function_count; ++j) {
            const char* open_j = strchr(model->functions[j], '(');
            size_t len_j = open_j ? (size_t)(open_j - model->functions[j]) : strlen(model->functions[j]);
            if (len_i == len_j && strncmp(model->functions[i], model->functions[j], len_i) == 0) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Duplicate function", 0, 0);
                return false;
            }
        }
        for (size_t j = 0; j < model->record_function_count; ++j) {
            if (strlen(model->record_functions[j].name) == len_i &&
                strncmp(model->functions[i], model->record_functions[j].name, len_i) == 0) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Duplicate function", 0, 0);
                return false;
            }
        }
    }
    for (size_t i = 0; i < model->record_function_count; ++i) {
        for (size_t j = i + 1; j < model->record_function_count; ++j) {
            if (cxpr_model_names_match(model->record_functions[i].name,
                                       model->record_functions[j].name)) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Duplicate function", 0, 0);
                return false;
            }
        }
    }

    for (size_t i = 0; i < model->constant_count; ++i) {
        for (size_t j = i + 1; j < model->constant_count; ++j) {
            if (cxpr_model_names_match(model->constants[i].name, model->constants[j].name)) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Duplicate constant", 0, 0);
                return false;
            }
        }
    }

    for (size_t i = 0; i < model->binding_count; ++i) {
        if (cxpr_model_string_exists(model->inputs, model->input_count,
                                     model->bindings[i].name)) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Binding duplicates input", 0, 0);
            return false;
        }
        if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE &&
            !cxpr_model_state_exists(model, model->bindings[i].name)) {
            cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                 "state update references unknown state", 0, 0);
            return false;
        }
        for (size_t j = i + 1; j < model->binding_count; ++j) {
            if (cxpr_model_names_match(model->bindings[i].name, model->bindings[j].name)) {
                bool state_pair =
                    (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE &&
                     model->bindings[j].kind == CXPR_MODEL_BINDING_STATE_UPDATE) ||
                    (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE &&
                     model->bindings[j].kind == CXPR_MODEL_BINDING_STATE);
                if (state_pair) continue;
                if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE &&
                    model->bindings[j].kind == CXPR_MODEL_BINDING_STATE_UPDATE) {
                    cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                         "Duplicate state update", 0, 0);
                    return false;
                }
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Duplicate binding", 0, 0);
                return false;
            }
        }
    }

    for (size_t i = 0; i < model->output_count; ++i) {
        if (!cxpr_model_public_symbol_exists(model, model->outputs[i])) {
            cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                 "Output references unknown symbol", 0, 0);
            return false;
        }
    }

    return true;
}

static bool cxpr_model_validate_expr_refs_scoped(const cxpr_model* model,
                                                 const cxpr_ast* expr,
                                                 bool constant_expr,
                                                 cxpr_registry* function_registry,
                                                 char* const* external_refs,
                                                 size_t external_ref_count,
                                                 const char* const* local_refs,
                                                 size_t local_ref_count,
                                                 cxpr_error* err) {
    const char* refs[256];
    const char* params[256];
    size_t nrefs;
    size_t nparams;

    if (expr && cxpr_ast_type(expr) == CXPR_NODE_RECORD) {
        const size_t field_count = cxpr_ast_record_field_count(expr);
        const char** scoped_refs =
            (const char**)calloc(local_ref_count + field_count,
                                 sizeof(char*));
        if (!scoped_refs && local_ref_count + field_count > 0u) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        for (size_t i = 0u; i < local_ref_count; ++i) scoped_refs[i] = local_refs[i];
        for (size_t i = 0u; i < field_count; ++i) {
            if (!cxpr_model_validate_expr_refs_scoped(
                    model,
                    cxpr_ast_record_field_value(expr, i),
                    constant_expr,
                    function_registry,
                    external_refs,
                    external_ref_count,
                    scoped_refs,
                    local_ref_count + i,
                    err)) {
                free(scoped_refs);
                return false;
            }
            scoped_refs[local_ref_count + i] = cxpr_ast_record_field_name(expr, i);
        }
        free(scoped_refs);
        return true;
    }

    nparams = cxpr_ast_variables_used(expr, params, CXPR_ARRAY_COUNT(params));
    for (size_t i = 0; i < nparams && i < CXPR_ARRAY_COUNT(params); ++i) {
        if (!cxpr_model_constant_exists(model, params[i]) &&
            (constant_expr ||
             !cxpr_model_external_reference_exists(
                 external_refs, external_ref_count, params[i]))) {
            cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                 "Expression references unknown constant", 0, 0);
            return false;
        }
    }

    nrefs = cxpr_ast_references(expr, refs, CXPR_ARRAY_COUNT(refs));
    for (size_t i = 0; i < nrefs && i < CXPR_ARRAY_COUNT(refs); ++i) {
        if (cxpr_model_local_reference_exists(local_refs, local_ref_count, refs[i])) continue;
        if (!constant_expr &&
            cxpr_model_expr_accepts_record_root_ref(
                model, expr, function_registry, external_refs, external_ref_count, refs[i])) {
            continue;
        }
        if (constant_expr ||
            (!cxpr_model_reference_exists(model, refs[i]) &&
             !cxpr_model_external_reference_exists(
                 external_refs, external_ref_count, refs[i]))) {
            cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                 constant_expr
                                     ? "Constant expression references runtime symbol"
                                     : "Expression references unknown symbol",
                                 0, 0);
            return false;
        }
    }

    return true;
}

static bool cxpr_model_validate_expr_refs(const cxpr_model* model,
                                          const cxpr_ast* expr,
                                          bool constant_expr,
                                          cxpr_registry* function_registry,
                                          char* const* external_refs,
                                          size_t external_ref_count,
                                          cxpr_error* err) {
    return cxpr_model_validate_expr_refs_scoped(
        model, expr, constant_expr, function_registry, external_refs, external_ref_count, NULL, 0u, err);
}

static bool cxpr_model_param_exists(char* const* params, size_t count, const char* name) {
    if (!name) return false;
    for (size_t i = 0; i < count; ++i) {
        if (cxpr_model_reference_matches_symbol(name, params[i])) return true;
    }
    return false;
}

static bool cxpr_model_validate_function_expr_refs(const cxpr_model* model,
                                                   const cxpr_model_record_function* fn,
                                                   const cxpr_ast* expr,
                                                   const char* const* local_refs,
                                                   size_t local_ref_count,
                                                   char* const* external_refs,
                                                   size_t external_ref_count,
                                                   cxpr_error* err) {
    const char* refs[256];
    const char* params[256];
    size_t nrefs;
    size_t nparams;

    nparams = cxpr_ast_variables_used(expr, params, CXPR_ARRAY_COUNT(params));
    for (size_t i = 0; i < nparams && i < CXPR_ARRAY_COUNT(params); ++i) {
        if (!cxpr_model_constant_exists(model, params[i])) {
            cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                 "Expression references unknown constant", 0, 0);
            return false;
        }
    }

    nrefs = cxpr_ast_references(expr, refs, CXPR_ARRAY_COUNT(refs));
    for (size_t i = 0; i < nrefs && i < CXPR_ARRAY_COUNT(refs); ++i) {
        if (cxpr_model_param_exists(fn->params, fn->param_count, refs[i])) continue;
        if (cxpr_model_local_reference_exists(local_refs, local_ref_count, refs[i])) continue;
        if (cxpr_model_external_reference_exists(external_refs, external_ref_count, refs[i])) continue;
        cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                             "Function expression references unknown symbol", 0, 0);
        return false;
    }

    return true;
}

bool cxpr_model_validate_with_external_refs(const cxpr_model* model,
                                            char* const* external_refs,
                                            size_t external_ref_count,
                                            cxpr_error* err) {
    cxpr_registry* function_registry = NULL;
    if (err) *err = (cxpr_error){0};
    if (!model) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "NULL model", 0, 0);
        return false;
    }
    if (!model->name || model->name[0] == '\0') {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Model name is required", 0, 0);
        return false;
    }
    if (!cxpr_model_validate_symbols(model, err)) return false;

    if (model->function_count > 0u) {
        function_registry = cxpr_registry_new();
        if (!function_registry) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        for (size_t i = 0u; i < model->function_count; ++i) {
            cxpr_error fn_err = cxpr_registry_define_fn(function_registry, model->functions[i]);
            if (fn_err.code != CXPR_OK) {
                if (err) *err = fn_err;
                cxpr_registry_free(function_registry);
                return false;
            }
        }
    }

    for (size_t i = 0; i < model->constant_count; ++i) {
        if (!cxpr_model_validate_expr_refs(
                model,
                model->constants[i].expr,
                true,
                function_registry,
                external_refs,
                external_ref_count,
                err)) {
            cxpr_registry_free(function_registry);
            return false;
        }
    }
    for (size_t i = 0; i < model->binding_count; ++i) {
        if (!cxpr_model_validate_expr_refs(
                model,
                model->bindings[i].expr,
                false,
                function_registry,
                external_refs,
                external_ref_count,
                err)) {
            cxpr_registry_free(function_registry);
            return false;
        }
    }
    for (size_t i = 0; i < model->record_function_count; ++i) {
        const size_t field_count = model->record_functions[i].field_count;
        const char** local_refs =
            (const char**)calloc(field_count ? field_count : 1u, sizeof(char*));
        if (!local_refs && field_count > 0u) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        for (size_t f = 0; f < model->record_functions[i].field_count; ++f) {
            if (!cxpr_model_validate_function_expr_refs(
                    model,
                    &model->record_functions[i],
                    model->record_functions[i].fields[f].expr,
                    local_refs,
                    f,
                    external_refs,
                    external_ref_count,
                    err)) {
                free(local_refs);
                cxpr_registry_free(function_registry);
                return false;
            }
            local_refs[f] = model->record_functions[i].fields[f].name;
        }
        free(local_refs);
    }

    cxpr_registry_free(function_registry);
    if (err) err->code = CXPR_OK;
    return true;
}

bool cxpr_model_validate(const cxpr_model* model, cxpr_error* err) {
    return cxpr_model_validate_with_external_refs(model, NULL, 0u, err);
}

bool cxpr_model_resolve_uses(const cxpr_model* model,
                             const cxpr_model_use_resolver* resolver,
                             cxpr_error* err) {
    if (err) *err = (cxpr_error){0};
    if (!model) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "NULL model", 0, 0);
        return false;
    }
    if (!resolver || !resolver->resolve) {
        if (err) err->code = CXPR_OK;
        return true;
    }
    for (size_t i = 0u; i < model->use_count; ++i) {
        cxpr_model_use_resolution resolution = {0};
        if (!resolver->resolve(
                model,
                i,
                model->uses[i],
                model->use_aliases ? model->use_aliases[i] : NULL,
                &resolution,
                resolver->userdata,
                err)) {
            return false;
        }
    }
    if (err) err->code = CXPR_OK;
    return true;
}

bool cxpr_model_eval_order(const cxpr_model* model, size_t* out_order,
                           size_t max_order, cxpr_error* err) {
    cxpr_expression_def* defs;
    cxpr_analysis* analyses;
    bool ok;

    if (err) *err = (cxpr_error){0};
    if (!model || (model->binding_count > 0 && !out_order) ||
        max_order < (model ? model->binding_count : 0)) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid eval order arguments", 0, 0);
        return false;
    }
    if (model->binding_count == 0) return true;

    defs = (cxpr_expression_def*)calloc(model->binding_count, sizeof(cxpr_expression_def));
    analyses = (cxpr_analysis*)calloc(model->binding_count, sizeof(cxpr_analysis));
    if (!defs || !analyses) {
        free(defs);
        free(analyses);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }

    for (size_t i = 0; i < model->binding_count; ++i) {
        defs[i].name = model->bindings[i].name;
        defs[i].expression = model->bindings[i].source;
    }

    ok = cxpr_analyze_expressions(defs, model->binding_count, NULL,
                                  analyses, out_order, err);
    free(defs);
    free(analyses);
    return ok;
}
