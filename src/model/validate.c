#include "model/internal.h"
#include "registry/internal.h"
#include <cxpr/resample.h>
#include <stdio.h>
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
    const cxpr_expr_ast* call,
    cxpr_registry* function_registry,
    char* const* external_refs,
    size_t external_ref_count,
    const char* reference) {
    cxpr_func_entry* entry;
    if (!call || cxpr_expr_ast_kind_of(call) != CXPR_NODE_FUNCTION_CALL ||
        !function_registry || !reference) {
        return false;
    }
    entry = cxpr_registry_find(function_registry, cxpr_expr_ast_call_name(call));
    if (!entry ||
        !entry->defined_param_fields ||
        entry->defined_param_count != cxpr_expr_ast_call_arg_count(call)) {
        return false;
    }
    for (size_t arg_i = 0u; arg_i < cxpr_expr_ast_call_arg_count(call); ++arg_i) {
        const cxpr_expr_ast* arg = cxpr_expr_ast_call_arg(call, arg_i);
        if (!arg ||
            cxpr_expr_ast_kind_of(arg) != CXPR_NODE_IDENTIFIER ||
            !cxpr_model_names_match(cxpr_expr_ast_identifier_name(arg), reference)) {
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
    const cxpr_expr_ast* expr,
    cxpr_registry* function_registry,
    char* const* external_refs,
    size_t external_ref_count,
    const char* reference) {
    if (!expr || !reference) return false;
    if (cxpr_model_function_call_accepts_record_root(
            model, expr, function_registry, external_refs, external_ref_count, reference)) {
        return true;
    }
    switch (cxpr_expr_ast_kind_of(expr)) {
    case CXPR_NODE_RECORD:
        for (size_t i = 0u; i < cxpr_expr_ast_record_field_count(expr); ++i) {
            if (cxpr_model_expr_accepts_record_root_ref(
                    model, cxpr_expr_ast_record_field_value(expr, i), function_registry,
                    external_refs, external_ref_count, reference)) {
                return true;
            }
        }
        return false;
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_expr_accepts_record_root_ref(
                   model, cxpr_expr_ast_binary_left(expr), function_registry,
                   external_refs, external_ref_count, reference) ||
               cxpr_model_expr_accepts_record_root_ref(
                   model, cxpr_expr_ast_binary_right(expr), function_registry,
                   external_refs, external_ref_count, reference);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_expr_accepts_record_root_ref(
            model, cxpr_expr_ast_unary_operand(expr), function_registry,
            external_refs, external_ref_count, reference);
    case CXPR_NODE_FUNCTION_CALL:
        for (size_t i = 0u; i < cxpr_expr_ast_call_arg_count(expr); ++i) {
            if (cxpr_model_expr_accepts_record_root_ref(
                    model, cxpr_expr_ast_call_arg(expr, i), function_registry,
                    external_refs, external_ref_count, reference)) {
                return true;
            }
        }
        return false;
    case CXPR_NODE_PRODUCER_ACCESS:
        for (size_t i = 0u; i < cxpr_expr_ast_producer_arg_count(expr); ++i) {
            if (cxpr_model_expr_accepts_record_root_ref(
                    model, cxpr_expr_ast_producer_arg(expr, i), function_registry,
                    external_refs, external_ref_count, reference)) {
                return true;
            }
        }
        return false;
    case CXPR_NODE_INDEX:
        return cxpr_model_expr_accepts_record_root_ref(
                   model, cxpr_expr_ast_index_target(expr), function_registry,
                   external_refs, external_ref_count, reference) ||
               cxpr_model_expr_accepts_record_root_ref(
                   model, cxpr_expr_ast_index_expression(expr), function_registry,
                   external_refs, external_ref_count, reference);
    case CXPR_NODE_TERNARY:
        return cxpr_model_expr_accepts_record_root_ref(
                   model, cxpr_expr_ast_ternary_condition(expr), function_registry,
                   external_refs, external_ref_count, reference) ||
               cxpr_model_expr_accepts_record_root_ref(
                   model, cxpr_expr_ast_ternary_true(expr), function_registry,
                   external_refs, external_ref_count, reference) ||
               cxpr_model_expr_accepts_record_root_ref(
                   model, cxpr_expr_ast_ternary_false(expr), function_registry,
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
                                                 const cxpr_expr_ast* expr,
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

    if (expr && cxpr_expr_ast_kind_of(expr) == CXPR_NODE_RECORD) {
        const size_t field_count = cxpr_expr_ast_record_field_count(expr);
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
                    cxpr_expr_ast_record_field_value(expr, i),
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
            scoped_refs[local_ref_count + i] = cxpr_expr_ast_record_field_name(expr, i);
        }
        free(scoped_refs);
        return true;
    }

    nparams = cxpr_expr_ast_variables_used(expr, params, CXPR_ARRAY_COUNT(params));
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

    nrefs = cxpr_expr_ast_references(expr, refs, CXPR_ARRAY_COUNT(refs));
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
            static CXPR_THREAD_LOCAL char message[256];
            snprintf(message, sizeof(message), "%s '%s'",
                     constant_expr
                         ? "Constant expression references runtime symbol"
                         : "Expression references unknown symbol",
                     refs[i] ? refs[i] : "");
            cxpr_model_set_error(
                err, CXPR_ERR_UNKNOWN_IDENTIFIER, message, 0, 0);
            return false;
        }
    }

    return true;
}

static bool cxpr_model_validate_expr_refs(const cxpr_model* model,
                                          const cxpr_expr_ast* expr,
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
                                                   const cxpr_expr_ast* expr,
                                                   const char* const* local_refs,
                                                   size_t local_ref_count,
                                                   char* const* external_refs,
                                                   size_t external_ref_count,
                                                   cxpr_error* err) {
    const char* refs[256];
    const char* params[256];
    size_t nrefs;
    size_t nparams;

    nparams = cxpr_expr_ast_variables_used(expr, params, CXPR_ARRAY_COUNT(params));
    for (size_t i = 0; i < nparams && i < CXPR_ARRAY_COUNT(params); ++i) {
        if (!cxpr_model_constant_exists(model, params[i])) {
            cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                 "Expression references unknown constant", 0, 0);
            return false;
        }
    }

    nrefs = cxpr_expr_ast_references(expr, refs, CXPR_ARRAY_COUNT(refs));
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
        if (!cxpr_resample_validate_ast(model->constants[i].expr, err)) {
            cxpr_registry_free(function_registry);
            return false;
        }
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
        if (!cxpr_resample_validate_ast(model->bindings[i].expr, err)) {
            if (err && model->bindings[i].has_span) {
                err->position = model->bindings[i].span.start.offset;
                err->line = model->bindings[i].span.start.line;
                err->column = model->bindings[i].span.start.column;
            }
            cxpr_registry_free(function_registry);
            return false;
        }
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
            if (!cxpr_resample_validate_ast(model->record_functions[i].fields[f].expr, err)) {
                free(local_refs);
                cxpr_registry_free(function_registry);
                return false;
            }
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

static char* cxpr_model_path_strdup(const char* s) {
    size_t len;
    char* copy;
    if (!s) return NULL;
    len = strlen(s);
    copy = (char*)malloc(len + 1u);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1u);
    return copy;
}

static char* cxpr_model_path_strndup(const char* s, size_t len) {
    char* copy;
    if (!s) return NULL;
    copy = (char*)malloc(len + 1u);
    if (!copy) return NULL;
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

static char* cxpr_model_path_dirname(const char* path) {
    const char* slash = path ? strrchr(path, '/') : NULL;
    if (!slash) return cxpr_model_path_strdup(".");
    if (slash == path) return cxpr_model_path_strdup("/");
    return cxpr_model_path_strndup(path, (size_t)(slash - path));
}

static int cxpr_model_path_exists(const char* path) {
    FILE* f;
    if (!path) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static char* cxpr_model_join_import_path(const char* dir, const char* use_name) {
    size_t dir_len;
    size_t use_len;
    int need_slash;
    int has_suffix;
    char* out;
    if (!dir || !use_name) return NULL;
    dir_len = strlen(dir);
    use_len = strlen(use_name);
    need_slash = dir_len > 0u && dir[dir_len - 1u] != '/';
    has_suffix = use_len > 5u && strcmp(use_name + use_len - 5u, ".cxpr") == 0;
    out = (char*)malloc(dir_len + (need_slash ? 1u : 0u) + use_len +
                        (has_suffix ? 1u : 6u));
    if (!out) return NULL;
    sprintf(out, "%s%s%s%s", dir, need_slash ? "/" : "", use_name,
            has_suffix ? "" : ".cxpr");
    return out;
}

static char* cxpr_model_join_path2(const char* dir, const char* name) {
    size_t dir_len;
    size_t name_len;
    int need_slash;
    char* out;
    if (!dir || !name) return NULL;
    dir_len = strlen(dir);
    name_len = strlen(name);
    need_slash = dir_len > 0u && dir[dir_len - 1u] != '/';
    out = (char*)malloc(dir_len + (need_slash ? 1u : 0u) + name_len + 1u);
    if (!out) return NULL;
    sprintf(out, "%s%s%s", dir, need_slash ? "/" : "", name);
    return out;
}

static char* cxpr_model_join_dyn_cxpr_import_path(const char* root, const char* use_name) {
    size_t root_len;
    size_t use_len;
    int need_slash;
    int has_suffix;
    size_t len;
    char* out;
    if (!root || !use_name) return NULL;
    root_len = strlen(root);
    use_len = strlen(use_name);
    need_slash = root_len > 0u && root[root_len - 1u] != '/';
    has_suffix = use_len > 5u && strcmp(use_name + use_len - 5u, ".cxpr") == 0;
    len = root_len + (need_slash ? 1u : 0u) +
          strlen("libs/dyn/cxpr/") + use_len + (has_suffix ? 0u : 5u) + 1u;
    out = (char*)malloc(len);
    if (!out) return NULL;
    snprintf(out, len, "%s%slibs/dyn/cxpr/%s%s",
             root, need_slash ? "/" : "", use_name, has_suffix ? "" : ".cxpr");
    return out;
}

static char* cxpr_model_resolve_dyn_cxpr_import_from_ancestors(
    const char* dir,
    const char* use_name) {
    char* cursor = cxpr_model_path_strdup(dir ? dir : ".");
    if (!cursor) return NULL;
    for (;;) {
        char* candidate = cxpr_model_join_dyn_cxpr_import_path(cursor, use_name);
        char* parent;
        if (!candidate) {
            free(cursor);
            return NULL;
        }
        if (cxpr_model_path_exists(candidate)) {
            free(cursor);
            return candidate;
        }
        free(candidate);
        if (strcmp(cursor, ".") == 0 || strcmp(cursor, "/") == 0) break;
        parent = cxpr_model_path_dirname(cursor);
        if (!parent) {
            free(cursor);
            return NULL;
        }
        if (strcmp(parent, cursor) == 0) {
            free(parent);
            break;
        }
        free(cursor);
        cursor = parent;
    }
    free(cursor);
    return NULL;
}

static char* cxpr_model_resolve_dyn_cxpr_import_from_cwd(const char* use_name) {
    return cxpr_model_resolve_dyn_cxpr_import_from_ancestors(".", use_name);
}

static char* cxpr_model_resolve_preset_import_from_ancestors(
    const char* dir,
    const char* use_name) {
    const char* preset_ref = use_name ? use_name + strlen("presets/") : NULL;
    char* cursor;

    if (!dir || !use_name ||
        strncmp(use_name, "presets/", strlen("presets/")) != 0 ||
        !preset_ref ||
        preset_ref[0] == '\0') {
        return NULL;
    }
    cursor = cxpr_model_path_strdup(dir);
    if (!cursor) return NULL;
    for (;;) {
        char* preset_dir = cxpr_model_join_path2(cursor, "presets");
        char* candidate = NULL;
        char* parent;
        if (!preset_dir) {
            free(cursor);
            return NULL;
        }
        candidate = cxpr_model_join_import_path(preset_dir, preset_ref);
        free(preset_dir);
        if (!candidate) {
            free(cursor);
            return NULL;
        }
        if (cxpr_model_path_exists(candidate)) {
            free(cursor);
            return candidate;
        }
        free(candidate);
        if (strcmp(cursor, ".") == 0 || strcmp(cursor, "/") == 0) break;
        parent = cxpr_model_path_dirname(cursor);
        if (!parent) {
            free(cursor);
            return NULL;
        }
        if (strcmp(parent, cursor) == 0) {
            free(parent);
            break;
        }
        free(cursor);
        cursor = parent;
    }
    free(cursor);
    return NULL;
}

static char* cxpr_model_resolve_use_file_path(const char* model_path, const char* use_name) {
    char* dir;
    char* path;
    if (!model_path || !use_name) return NULL;
    dir = cxpr_model_path_dirname(model_path);
    if (!dir) return NULL;
    path = cxpr_model_join_import_path(dir, use_name);
    if (!path) {
        free(dir);
        return NULL;
    }
    if (cxpr_model_path_exists(path)) {
        free(dir);
        return path;
    }
    free(path);
    if (strncmp(use_name, "presets/", strlen("presets/")) == 0) {
        const char* slash = strrchr(dir, '/');
        const char* leaf = slash ? slash + 1 : dir;
        if (strcmp(leaf, "presets") == 0) {
            char* parent = cxpr_model_path_dirname(dir);
            if (!parent) {
                free(dir);
                return NULL;
            }
            path = cxpr_model_join_import_path(parent, use_name);
            free(parent);
            if (!path) {
                free(dir);
                return NULL;
            }
            if (cxpr_model_path_exists(path)) {
                free(dir);
                return path;
            }
            free(path);
        }
        path = cxpr_model_resolve_preset_import_from_ancestors(dir, use_name);
        if (path) {
            free(dir);
            return path;
        }
    }
    if (strncmp(use_name, "indicators/", strlen("indicators/")) == 0) {
        path = cxpr_model_resolve_dyn_cxpr_import_from_ancestors(dir, use_name);
        if (path) {
            free(dir);
            return path;
        }
        path = cxpr_model_resolve_dyn_cxpr_import_from_cwd(use_name);
        if (path) {
            free(dir);
            return path;
        }
    }
    free(dir);
    return NULL;
}

bool cxpr_model_validate_use_files(const cxpr_model* model,
                                   const char* model_path,
                                   cxpr_error* err) {
    if (err) *err = (cxpr_error){0};
    if (!model) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "NULL model", 0, 0);
        return false;
    }
    if (!model_path || model_path[0] == '\0') {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Model path is required for use validation", 0, 0);
        return false;
    }
    for (size_t i = 0u; i < model->use_count; ++i) {
        char* path = cxpr_model_resolve_use_file_path(model_path, model->uses[i]);
        if (!path) {
            cxpr_source_span span = {0};
            size_t line = 0u;
            size_t column = 0u;
            if (cxpr_model_use_source_span(model, i, &span)) {
                line = span.start.line;
                column = span.start.column;
            }
            cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                 "CXPR use target was not found", line, column);
            return false;
        }
        free(path);
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
