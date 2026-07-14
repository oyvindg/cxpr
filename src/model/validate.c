#include "model/internal.h"
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
        if (cxpr_model_names_match(model->constants[i].name, name)) return true;
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

static bool cxpr_model_validate_expr_refs(const cxpr_model* model, const cxpr_ast* expr,
                                          bool constant_expr, cxpr_error* err) {
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
        if (constant_expr || !cxpr_model_reference_exists(model, refs[i])) {
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
        cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                             "Function expression references unknown symbol", 0, 0);
        return false;
    }

    return true;
}

bool cxpr_model_validate(const cxpr_model* model, cxpr_error* err) {
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

    for (size_t i = 0; i < model->constant_count; ++i) {
        if (!cxpr_model_validate_expr_refs(model, model->constants[i].expr, true, err)) {
            return false;
        }
    }
    for (size_t i = 0; i < model->binding_count; ++i) {
        if (!cxpr_model_validate_expr_refs(model, model->bindings[i].expr, false, err)) {
            return false;
        }
    }
    for (size_t i = 0; i < model->record_function_count; ++i) {
        for (size_t f = 0; f < model->record_functions[i].field_count; ++f) {
            if (!cxpr_model_validate_function_expr_refs(
                    model,
                    &model->record_functions[i],
                    model->record_functions[i].fields[f].expr,
                    err)) {
                return false;
            }
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
