/**
 * @file model/compile/compile.c
 * @brief Build executable model programs from parsed models.
 */

#include "core.h"
#include "ast/internal.h"
#include "ir/compile/internal.h"
#include "lookback.h"
#include "model/internal.h"
#include "model/compile/internal.h"
#include <cxpr/resample.h>
#include <cxpr/typecheck.h>
#include <stdlib.h>
#include <string.h>

static bool cxpr_model_collect_resamples_ast(cxpr_model_compiled* program,
                                             const cxpr_expr_ast* ast,
                                             cxpr_error* err) {
    cxpr_resample_call call = {0};
    size_t i;
    if (!ast) return true;
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL &&
        cxpr_expr_ast_call_name(ast) &&
        strcmp(cxpr_expr_ast_call_name(ast), "resample") == 0) {
        const char* source_name;
        cxpr_model_resample_requirement* grown;
        if (!cxpr_resample_call_parse(ast, &call, err) || !call.source ||
            cxpr_expr_ast_kind_of(call.source) != CXPR_NODE_IDENTIFIER) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Generated resample requires an identifier source", 0, 0);
            return false;
        }
        source_name = cxpr_expr_ast_identifier_name(call.source);
        for (i = 0; i < program->resample_requirement_count; ++i) {
            if (program->resample_requirements[i].duration_ns == call.every.duration_ns &&
                cxpr_model_names_match(program->resample_requirements[i].source_name,
                                       source_name)) return true;
        }
        grown = realloc(program->resample_requirements,
                        (program->resample_requirement_count + 1u) * sizeof(*grown));
        if (!grown) return false;
        program->resample_requirements = grown;
        grown += program->resample_requirement_count++;
        *grown = (cxpr_model_resample_requirement){
            .source_name = cxpr_strdup(source_name),
            .duration_ns = call.every.duration_ns,
            .canonical = cxpr_strdup(call.every.canonical),
        };
        return grown->source_name && grown->canonical;
    }
    switch (cxpr_expr_ast_kind_of(ast)) {
    case CXPR_NODE_FUNCTION_CALL:
        for (i = 0; i < cxpr_expr_ast_call_arg_count(ast); ++i)
            if (!cxpr_model_collect_resamples_ast(program, cxpr_expr_ast_call_arg(ast, i), err)) return false;
        break;
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_collect_resamples_ast(program, cxpr_expr_ast_binary_left(ast), err) &&
               cxpr_model_collect_resamples_ast(program, cxpr_expr_ast_binary_right(ast), err);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_collect_resamples_ast(program, cxpr_expr_ast_unary_operand(ast), err);
    case CXPR_NODE_INDEX:
        return cxpr_model_collect_resamples_ast(program, cxpr_expr_ast_index_target(ast), err) &&
               cxpr_model_collect_resamples_ast(program, cxpr_expr_ast_index_expression(ast), err);
    case CXPR_NODE_TERNARY:
        return cxpr_model_collect_resamples_ast(program, cxpr_expr_ast_ternary_condition(ast), err) &&
               cxpr_model_collect_resamples_ast(program, cxpr_expr_ast_ternary_true(ast), err) &&
               cxpr_model_collect_resamples_ast(program, cxpr_expr_ast_ternary_false(ast), err);
    default: break;
    }
    return true;
}
#include "model/window/window.h"
#include "registry/internal.h"
#include <cxpr/source.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



static char* cxpr_model_dup_trimmed_metadata_value(const char* value) {
    const char* end;
    size_t len;
    char* out;
    if (!value) return NULL;
    end = value;
    while (*end && *end != '\n' && *end != ',' && *end != '}') end++;
    while (value < end && (*value == ' ' || *value == '\t' || *value == '\r')) value++;
    while (end > value && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
    if (end > value + 1u &&
        ((*value == '"' && end[-1] == '"') || (*value == '\'' && end[-1] == '\''))) {
        value++;
        end--;
    }
    len = (size_t)(end - value);
    if (len == 0u) return NULL;
    out = (char*)malloc(len + 1u);
    if (!out) return NULL;
    memcpy(out, value, len);
    out[len] = '\0';
    return out;
}

static const char* cxpr_model_model_field_value(const cxpr_model* model,
                                                const char* key) {
    const cxpr_model_host_block* block;
    if (!model) return NULL;
    for (size_t i = 0u; i < model->metadata_count; ++i) {
        if (model->metadatas[i].target_kind != CXPR_MODEL_METADATA_TARGET_MODEL) continue;
        {
            const char* value = cxpr_model_metadata_field_value(model, i, key);
            if (value) return value;
        }
    }
    block = cxpr_model_host_block_by_kind(model, "model");
    return block ? cxpr_host_block_field_value_by_key(block, key) : NULL;
}

static char* cxpr_model_parse_source_arg_metadata(const cxpr_model* model) {
    const char* value = cxpr_model_model_field_value(model, "source_arg");
    if (value) {
        return cxpr_model_dup_trimmed_metadata_value(value);
    }
    return NULL;
}

static bool cxpr_model_parse_lifetime_metadata(const cxpr_model* model,
                                               cxpr_model_lifetime* out_lifetime,
                                               bool* out_saw_type,
                                               cxpr_error* err) {
    cxpr_model_lifetime lifetime = CXPR_MODEL_LIFETIME_SINGLETON;
    bool saw_type = false;
    if (!model) {
        if (out_lifetime) *out_lifetime = lifetime;
        if (out_saw_type) *out_saw_type = false;
        return true;
    }
    {
        const char* value;
        char* type;
        value = cxpr_model_model_field_value(model, "lifecycle");
        if (!value) value = cxpr_model_model_field_value(model, "type");
        if (!value) goto done;
        type = cxpr_model_dup_trimmed_metadata_value(value);
        if (!type) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        saw_type = true;
        if (cxpr_model_names_match(type, "singleton")) {
            lifetime = CXPR_MODEL_LIFETIME_SINGLETON;
        } else if (cxpr_model_names_match(type, "scoped")) {
            lifetime = CXPR_MODEL_LIFETIME_SCOPED;
        } else if (cxpr_model_names_match(type, "transient")) {
            lifetime = CXPR_MODEL_LIFETIME_TRANSIENT;
        } else {
            free(type);
            goto done;
        }
        free(type);
    }
done:
    if (out_lifetime) *out_lifetime = lifetime;
    if (out_saw_type) *out_saw_type = saw_type;
    return true;
}

static bool cxpr_model_executable_eval_order(const cxpr_model* model,
                                             size_t* out_order,
                                             size_t executable_count,
                                             cxpr_error* err) {
    cxpr_expression_def* defs;
    cxpr_analysis* analyses;
    size_t* map;
    char** def_names;
    size_t def_count = 0u;
    bool ok;

    if (executable_count == 0u) return true;
    defs = (cxpr_expression_def*)calloc(executable_count, sizeof(cxpr_expression_def));
    analyses = (cxpr_analysis*)calloc(executable_count, sizeof(cxpr_analysis));
    map = (size_t*)calloc(executable_count, sizeof(size_t));
    def_names = (char**)calloc(executable_count, sizeof(char*));
    if (!defs || !analyses || !map || !def_names) {
        free(defs);
        free(analyses);
        free(map);
        free(def_names);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }

    for (size_t i = 0; i < model->binding_count; ++i) {
        if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE) continue;
        if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) {
            size_t len = strlen(model->bindings[i].name) + strlen("__state_update_") + 1u;
            def_names[def_count] = (char*)malloc(len);
            if (!def_names[def_count]) {
                ok = false;
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                goto cleanup;
            }
            snprintf(def_names[def_count], len, "__state_update_%s", model->bindings[i].name);
            defs[def_count].name = def_names[def_count];
        } else {
            defs[def_count].name = model->bindings[i].name;
        }
        defs[def_count].expression = model->bindings[i].source;
        map[def_count] = i;
        def_count++;
    }

    ok = cxpr_analyze_expressions(defs, def_count, NULL, analyses, out_order, err);
    if (ok) {
        for (size_t i = 0; i < def_count; ++i) out_order[i] = map[out_order[i]];
    }
cleanup:
    for (size_t i = 0; i < executable_count; ++i) free(def_names[i]);
    free(defs);
    free(analyses);
    free(map);
    free(def_names);
    return ok;
}

static cxpr_model_result_kind cxpr_model_state_default_result_kind(
    const cxpr_model_compiled* program,
    const char* name) {
    if (!program || !name) return CXPR_MODEL_RESULT_UNKNOWN;
    for (size_t i = 0; i < program->state_default_count; ++i) {
        if (cxpr_model_names_match(program->state_defaults[i].name, name)) {
            return program->state_defaults[i].result_kind;
        }
    }
    return CXPR_MODEL_RESULT_UNKNOWN;
}

static cxpr_model_result_kind cxpr_model_declared_result_kind(
    cxpr_model_decl_type declared_type,
    cxpr_model_result_kind inferred) {
    switch (declared_type) {
    case CXPR_MODEL_DECL_NUMBER: return CXPR_MODEL_RESULT_NUMBER;
    case CXPR_MODEL_DECL_BOOL: return CXPR_MODEL_RESULT_BOOL;
    case CXPR_MODEL_DECL_INT: return CXPR_MODEL_RESULT_INT64;
    default: return inferred;
    }
}

static const cxpr_model_compile_options cxpr_model_default_compile_options = {
    CXPR_MODEL_BACKEND_AUTO,
    true,
    false,
    NULL,
    0u,
};

static bool cxpr_model_compile_options_resolve(
    const cxpr_model_compile_options* options,
    cxpr_model_compile_options* out,
    cxpr_error* err) {
    if (!out) return false;
    *out = options ? *options : cxpr_model_default_compile_options;
    switch (out->backend) {
    case CXPR_MODEL_BACKEND_AUTO:
    case CXPR_MODEL_BACKEND_IR:
    case CXPR_MODEL_BACKEND_C:
        break;
    default:
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Invalid model compile backend option", 0, 0);
        return false;
    }
    if (out->backend == CXPR_MODEL_BACKEND_IR && !out->fuse) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Model IR backend requires fuse=true", 0, 0);
        return false;
    }
    if (out->enable_trace && out->backend != CXPR_MODEL_BACKEND_AUTO) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Model backend tracing is not supported for explicit backends", 0, 0);
        return false;
    }
    if (out->host_binding_count > 0u && !out->host_bindings) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Model host binding schema is missing", 0, 0);
        return false;
    }
    return true;
}

static bool cxpr_model_apply_host_binding_schema(
    cxpr_model_compiled* program,
    const cxpr_model_compile_options* options,
    cxpr_error* err) {
    size_t i;
    size_t j;

    if (!program || !options || options->host_binding_count == 0u) return true;
    if (options->host_binding_count != program->input_count) {
        cxpr_model_set_error(
            err, CXPR_ERR_SYNTAX,
            "Model host binding schema must declare every input", 0, 0);
        return false;
    }
    for (i = 0u; i < options->host_binding_count; ++i) {
        const cxpr_model_host_binding* binding = &options->host_bindings[i];
        cxpr_model_slot_ref* slot = NULL;
        int input_found = 0;
        if (!binding->name || binding->name[0] == '\0' ||
            (binding->type != CXPR_MODEL_RESULT_NUMBER &&
             binding->type != CXPR_MODEL_RESULT_BOOL)) {
            cxpr_model_set_error(
                err, CXPR_ERR_SYNTAX,
                "Model host binding has an invalid name or scalar type", 0, 0);
            return false;
        }
        for (j = 0u; j < i; ++j) {
            if (strcmp(options->host_bindings[j].name, binding->name) == 0) {
                cxpr_model_set_error(
                    err, CXPR_ERR_SYNTAX,
                    "Model host binding schema contains a duplicate name", 0, 0);
                return false;
            }
        }
        for (j = 0u; j < program->input_count; ++j) {
            if (strcmp(program->inputs[j], binding->name) == 0) {
                input_found = 1;
                break;
            }
        }
        if (!input_found) {
            cxpr_model_set_error(
                err, CXPR_ERR_SYNTAX,
                "Model host binding does not match a declared input", 0, 0);
            return false;
        }
        for (j = 0u; j < program->fused_input_count; ++j) {
            if (program->fused_inputs[j].name &&
                strcmp(program->fused_inputs[j].name, binding->name) == 0) {
                slot = &program->fused_inputs[j];
                break;
            }
        }
        if (!slot) {
            cxpr_model_set_error(
                err, CXPR_ERR_SYNTAX,
                "Model host binding is unavailable in generated-C layout", 0, 0);
            return false;
        }
        slot->result_kind = binding->type;
    }
    return true;
}

static void cxpr_model_compiled_drop_runnable_fast_path(cxpr_model_compiled* program) {
    if (!program) return;
    cxpr_ir_program_reset(&program->fused_ir);
    program->has_fused_ir = false;
}

static bool cxpr_model_compiled_validate_c_backend(cxpr_model_compiled* program,
                                                  cxpr_error* err) {
    cxpr_error codegen_err = {0};
    char* source = cxpr_model_compiled_generate_c(
        program, "", "cxpr_model_backend_validate", &codegen_err);
    if (!source) {
        if (err) {
            *err = codegen_err;
            if (err->code == CXPR_OK) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Model C backend is not supported for this model", 0, 0);
            }
        }
        return false;
    }
    free(source);
    if (err) err->code = CXPR_OK;
    return true;
}

static bool cxpr_model_compiled_select_backend(cxpr_model_compiled* program,
                                              const cxpr_model* model,
                                              const cxpr_registry* compile_reg,
                                              const cxpr_model_compile_options* options,
                                              cxpr_error* err) {
    if (!program || !options) return false;
    program->requested_backend = options->backend;
    program->selected_backend = CXPR_MODEL_BACKEND_AUTO;
    program->compile_fuse = options->fuse;
    program->compile_trace = options->enable_trace;

    if (options->backend == CXPR_MODEL_BACKEND_AUTO) {
        if (options->enable_trace) {
            program->fused_disabled_opcode = "trace enabled";
            return true;
        }
        if (!options->fuse) {
            program->fused_disabled_opcode = "fast path disabled by compile options";
            return true;
        }
        if (!cxpr_model_try_compile_fused_ir(program, model, compile_reg, err)) {
            return false;
        }
        program->selected_backend = program->has_fused_ir
                                        ? CXPR_MODEL_BACKEND_IR
                                        : CXPR_MODEL_BACKEND_AUTO;
        return true;
    }

    if (!cxpr_model_try_compile_fused_ir(program, model, compile_reg, err)) {
        return false;
    }

    if (options->backend == CXPR_MODEL_BACKEND_IR) {
        if (!program->has_fused_ir) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Model IR backend requires scalar fast-path support", 0, 0);
            return false;
        }
        program->selected_backend = CXPR_MODEL_BACKEND_IR;
        return true;
    }

    if (!options->fuse) {
        cxpr_model_compiled_drop_runnable_fast_path(program);
    }
    if (!cxpr_model_compiled_validate_c_backend(program, err)) {
        return false;
    }
    program->selected_backend = CXPR_MODEL_BACKEND_C;
    return true;
}

cxpr_model_compiled* cxpr_model_compile(const cxpr_model* model,
                                       const cxpr_registry* reg,
                                       cxpr_error* err) {
    return cxpr_model_compile_with_options(model, reg, NULL, err);
}

cxpr_model_compiled* cxpr_model_compile_with_options(
    const cxpr_model* model,
    const cxpr_registry* reg,
    const cxpr_model_compile_options* options,
    cxpr_error* err) {
    return cxpr_model_compile_full(model, reg, NULL, 0u, options, err);
}

cxpr_model_compiled* cxpr_model_compile_with_imports(const cxpr_model* model,
                                                    const cxpr_registry* reg,
                                                    const cxpr_model_import* imports,
                                                    size_t import_count,
                                                    cxpr_error* err) {
    return cxpr_model_compile_full(
        model, reg, imports, import_count, NULL, err);
}

cxpr_model_compiled* cxpr_model_compile_full(
    const cxpr_model* model,
    const cxpr_registry* reg,
    const cxpr_model_import* imports,
    size_t import_count,
    const cxpr_model_compile_options* options,
    cxpr_error* err) {
    cxpr_model_compiled* program;
    cxpr_model inferred_model = {0};
    cxpr_model_compile_options compile_options;
    const cxpr_registry* compile_reg = reg;
    char** inferred_inputs = NULL;
    size_t inferred_input_count = 0u;
    char** required_defaults = NULL;
    size_t required_default_count = 0u;
    size_t* order = NULL;
    bool expanded_anonymous_outputs = false;

    if (err) *err = (cxpr_error){0};
    if (!cxpr_model_compile_options_resolve(options, &compile_options, err)) {
        return NULL;
    }
    if (model && model->anonymous_output_count > 0u) {
        if (!cxpr_model_copy_bindings_and_outputs(&inferred_model, model, err)) {
            cxpr_model_expanded_copy_free(&inferred_model);
            return NULL;
        }
        model = &inferred_model;
        if (!cxpr_model_expand_anonymous_outputs(&inferred_model, imports, import_count, err)) {
            cxpr_model_expanded_copy_free(&inferred_model);
            return NULL;
        }
        expanded_anonymous_outputs = true;
    }
    if (!cxpr_model_infer_inputs_for_compile(
            model, imports, import_count, &inferred_inputs, &inferred_input_count, err)) {
        if (expanded_anonymous_outputs) {
            cxpr_model_expanded_copy_free(&inferred_model);
        }
        return NULL;
    }
    if (inferred_input_count > 0u) {
        inferred_model = *model;
        inferred_model.inputs = inferred_inputs;
        inferred_model.input_count = inferred_input_count;
        model = &inferred_model;
    }
    if (!cxpr_model_validate(model, err)) {
        for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
        free(inferred_inputs);
        if (expanded_anonymous_outputs) cxpr_model_expanded_copy_free(&inferred_model);
        return NULL;
    }
    if (!cxpr_model_collect_required_defaults(model, &required_defaults,
                                              &required_default_count, err)) {
        for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
        free(inferred_inputs);
        if (expanded_anonymous_outputs) cxpr_model_expanded_copy_free(&inferred_model);
        return NULL;
    }
    program = (cxpr_model_compiled*)calloc(1, sizeof(cxpr_model_compiled));
    if (!program) {
        for (size_t i = 0; i < required_default_count; ++i) free(required_defaults[i]);
        free(required_defaults);
        for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
        free(inferred_inputs);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    program->requested_backend = compile_options.backend;
    program->selected_backend = CXPR_MODEL_BACKEND_AUTO;
    program->compile_fuse = compile_options.fuse;
    program->compile_trace = compile_options.enable_trace;
    program->lifetime = CXPR_MODEL_LIFETIME_SINGLETON;
    {
        bool saw_type = false;
        if (!cxpr_model_parse_lifetime_metadata(model, &program->lifetime, &saw_type, err)) {
            for (size_t d = 0; d < required_default_count; ++d) free(required_defaults[d]);
            free(required_defaults);
            for (size_t d = 0u; d < inferred_input_count; ++d) free(inferred_inputs[d]);
            free(inferred_inputs);
            cxpr_model_compiled_free(program);
            return NULL;
        }
        (void)saw_type;
    }
    program->source_arg = cxpr_model_parse_source_arg_metadata(model);
    {
        const char* guard = cxpr_model_model_field_value(model, "invalid_input_guard");
        if (guard) {
            program->invalid_input_guard =
                cxpr_model_dup_trimmed_metadata_value(guard);
            if (!program->invalid_input_guard) {
                for (size_t d = 0; d < required_default_count; ++d) {
                    free(required_defaults[d]);
                }
                free(required_defaults);
                for (size_t d = 0u; d < inferred_input_count; ++d) {
                    free(inferred_inputs[d]);
                }
                free(inferred_inputs);
                cxpr_model_compiled_free(program);
                cxpr_model_set_error(
                    err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
        }
    }
    if (model && cxpr_model_metadata_count(model) > 0u && !program->source_arg) {
        for (size_t i = 0u; i < model->metadata_count; ++i) {
            if (model->metadatas[i].target_kind == CXPR_MODEL_METADATA_TARGET_MODEL &&
                cxpr_model_metadata_field_value(model, i, "source_arg")) {
                for (size_t d = 0; d < required_default_count; ++d) free(required_defaults[d]);
                free(required_defaults);
                for (size_t d = 0u; d < inferred_input_count; ++d) free(inferred_inputs[d]);
                free(inferred_inputs);
                cxpr_model_compiled_free(program);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
        }
    }
    if (!cxpr_model_collect_lookbacks(model, reg,
                                      &program->history_specs,
                                      &program->history_spec_count,
                                      err)) {
        for (size_t i = 0; i < required_default_count; ++i) free(required_defaults[i]);
        free(required_defaults);
        for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
        free(inferred_inputs);
        cxpr_model_compiled_free(program);
        return NULL;
    }
    if (reg && program->history_spec_count > 0u &&
        model->function_count == 0u && model->record_function_count == 0u &&
        import_count == 0u) {
        /* Borrow external producer/codegen metadata through source generation. */
        program->registry = (cxpr_registry*)reg;
        program->owns_registry = false;
        compile_reg = reg;
    } else if (model->function_count > 0 || model->record_function_count > 0u ||
        import_count > 0u ||
        (!reg && required_default_count > 0u) ||
        program->history_spec_count > 0u) {
        program->registry = cxpr_registry_new();
        if (!program->registry) {
            for (size_t i = 0; i < required_default_count; ++i) free(required_defaults[i]);
            free(required_defaults);
            for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
            free(inferred_inputs);
            cxpr_model_compiled_free(program);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        program->owns_registry = true;
        if (!cxpr_model_compiled_register_imports(program, model, imports, import_count, err)) {
            for (size_t j = 0; j < required_default_count; ++j) free(required_defaults[j]);
            free(required_defaults);
            for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
            free(inferred_inputs);
            cxpr_model_compiled_free(program);
            return NULL;
        }
        for (size_t i = 0; i < required_default_count; ++i) {
            if (!cxpr_register_default_named(program->registry, required_defaults[i])) {
                if (err) {
                    static CXPR_THREAD_LOCAL char message[256];
                    err->code = CXPR_ERR_UNKNOWN_FUNCTION;
                    snprintf(message, sizeof(message), "Unknown function '%s'",
                             required_defaults[i]);
                    err->message = message;
                }
                for (size_t j = 0; j < required_default_count; ++j) free(required_defaults[j]);
                free(required_defaults);
                for (size_t k = 0u; k < inferred_input_count; ++k) free(inferred_inputs[k]);
                free(inferred_inputs);
                cxpr_model_compiled_free(program);
                return NULL;
            }
        }
        for (size_t i = 0; i < model->function_count; ++i) {
            cxpr_error fn_err = cxpr_registry_define_fn(program->registry, model->functions[i]);
            if (fn_err.code != CXPR_OK) {
                if (err) *err = fn_err;
                for (size_t j = 0; j < required_default_count; ++j) free(required_defaults[j]);
                free(required_defaults);
                for (size_t k = 0u; k < inferred_input_count; ++k) free(inferred_inputs[k]);
                free(inferred_inputs);
                cxpr_model_compiled_free(program);
                return NULL;
            }
        }
        for (size_t i = 0; i < model->record_function_count; ++i) {
            const char** field_names;
            const cxpr_expr_ast** field_bodies;
            cxpr_error fn_err;
            field_names = (const char**)calloc(model->record_functions[i].field_count,
                                               sizeof(char*));
            field_bodies = (const cxpr_expr_ast**)calloc(model->record_functions[i].field_count,
                                                    sizeof(cxpr_expr_ast*));
            if (!field_names || !field_bodies) {
                free(field_names);
                free(field_bodies);
                for (size_t j = 0; j < required_default_count; ++j) free(required_defaults[j]);
                free(required_defaults);
                for (size_t k = 0u; k < inferred_input_count; ++k) free(inferred_inputs[k]);
                free(inferred_inputs);
                cxpr_model_compiled_free(program);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
            for (size_t f = 0; f < model->record_functions[i].field_count; ++f) {
                field_names[f] = model->record_functions[i].fields[f].name;
                field_bodies[f] = model->record_functions[i].fields[f].expr;
            }
            fn_err = cxpr_registry_define_record_fn(
                program->registry,
                model->record_functions[i].name,
                (const char* const*)model->record_functions[i].params,
                model->record_functions[i].param_count,
                (const char* const*)field_names,
                (const cxpr_expr_ast* const*)field_bodies,
                model->record_functions[i].field_count);
            free(field_names);
            free(field_bodies);
            if (fn_err.code != CXPR_OK) {
                if (err) *err = fn_err;
                for (size_t j = 0; j < required_default_count; ++j) free(required_defaults[j]);
                free(required_defaults);
                for (size_t k = 0u; k < inferred_input_count; ++k) free(inferred_inputs[k]);
                free(inferred_inputs);
                cxpr_model_compiled_free(program);
                return NULL;
            }
        }
        if (program->registry) {
            for (size_t i = 0u; i < program->history_spec_count; ++i) {
                free(program->history_specs[i].name);
                cxpr_expr_ast_free(program->history_specs[i].target);
            }
            free(program->history_specs);
            program->history_specs = NULL;
            program->history_spec_count = 0u;
            if (!cxpr_model_collect_lookbacks(
                    model,
                    program->registry,
                    &program->history_specs,
                    &program->history_spec_count,
                    err)) {
                for (size_t j = 0; j < required_default_count; ++j) {
                    free(required_defaults[j]);
                }
                free(required_defaults);
                for (size_t j = 0u; j < inferred_input_count; ++j) {
                    free(inferred_inputs[j]);
                }
                free(inferred_inputs);
                cxpr_model_compiled_free(program);
                return NULL;
            }
            bool has_state_buffer = false;
            for (size_t binding_i = 0u; binding_i < model->binding_count; ++binding_i) {
                if (model->bindings[binding_i].kind == CXPR_MODEL_BINDING_STATE &&
                    model->bindings[binding_i].declared_type == CXPR_MODEL_DECL_BUFFER) {
                    has_state_buffer = true;
                    break;
                }
            }
            if (program->history_spec_count > 0u || has_state_buffer) {
                cxpr_registry_set_lookback_resolver(
                    program->registry, cxpr_model_lookback_resolver, NULL, NULL);
            }
        }
        compile_reg = program->registry;
    }

    for (size_t i = 0; i < required_default_count; ++i) free(required_defaults[i]);
    free(required_defaults);

    if (model->constant_count > 0) {
        program->constants =
            (cxpr_model_compiled_binding*)calloc(model->constant_count,
                                                 sizeof(cxpr_model_compiled_binding));
        if (!program->constants) {
            cxpr_model_compiled_free(program);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        program->constant_count = model->constant_count;
        for (size_t i = 0; i < model->constant_count; ++i) {
            program->constants[i].name = cxpr_strdup(model->constants[i].name);
            program->constants[i].source = cxpr_strdup(model->constants[i].source);
            program->constants[i].name_hash = cxpr_hash_string(model->constants[i].name);
            program->constants[i].ast = cxpr_model_inline_defined_calls(
                model->constants[i].expr, compile_reg, err);
            if (program->constants[i].ast &&
                !cxpr_typecheck(program->constants[i].ast, compile_reg, NULL, err)) {
                cxpr_model_compiled_free(program);
                return NULL;
            }
            program->constants[i].result_kind =
                cxpr_model_declared_result_kind(
                    model->constants[i].declared_type,
                    cxpr_model_infer_result_kind(program->constants[i].ast, compile_reg));
            program->constants[i].declared_type = model->constants[i].declared_type;
            program->constants[i].element_type = model->constants[i].element_type;
            program->constants[i].is_call_param = model->constants[i].is_call_param;
            for (size_t m = 0u; m < cxpr_model_metadata_count(model); ++m) {
                const char* target;
                if (cxpr_model_metadata_target_kind_at(model, m) !=
                    CXPR_MODEL_METADATA_TARGET_PARAM) continue;
                target = cxpr_model_metadata_target_name(model, m);
                if (!target || !cxpr_model_names_match(target, model->constants[i].name)) continue;
                program->constants[i].has_min_value =
                    cxpr_model_metadata_field_number(
                        model, m, "min", &program->constants[i].min_value);
                program->constants[i].has_max_value =
                    cxpr_model_metadata_field_number(
                        model, m, "max", &program->constants[i].max_value);
                break;
            }
            if (!program->constants[i].name ||
                !program->constants[i].source ||
                !program->constants[i].ast) {
                cxpr_model_compiled_free(program);
                if (err && err->code == CXPR_OK) {
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                }
                return NULL;
            }
        }
    }

    {
        size_t state_count = 0u;
        size_t executable_count = 0u;
        for (size_t i = 0; i < model->binding_count; ++i) {
            if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE) state_count++;
            else executable_count++;
        }

        if (state_count > 0u) {
            program->state_defaults =
                (cxpr_model_compiled_binding*)calloc(state_count,
                                                     sizeof(cxpr_model_compiled_binding));
            if (!program->state_defaults) {
                cxpr_model_compiled_free(program);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
            program->state_default_count = state_count;
            for (size_t i = 0, out_i = 0; i < model->binding_count; ++i) {
                if (model->bindings[i].kind != CXPR_MODEL_BINDING_STATE) continue;
                program->state_defaults[out_i].kind = model->bindings[i].kind;
                program->state_defaults[out_i].declared_type = model->bindings[i].declared_type;
                program->state_defaults[out_i].element_type = model->bindings[i].element_type;
                program->state_defaults[out_i].buffer_samples = model->bindings[i].buffer_samples;
                program->state_defaults[out_i].name = cxpr_strdup(model->bindings[i].name);
                program->state_defaults[out_i].source = model->bindings[i].source
                    ? cxpr_strdup(model->bindings[i].source) : NULL;
                program->state_defaults[out_i].name_hash = cxpr_hash_string(model->bindings[i].name);
                program->state_defaults[out_i].ast = cxpr_model_inline_defined_calls(
                    model->bindings[i].expr, compile_reg, err);
                if (program->state_defaults[out_i].ast &&
                    !cxpr_typecheck(program->state_defaults[out_i].ast,
                                    compile_reg, NULL, err)) {
                    cxpr_model_compiled_free(program);
                    return NULL;
                }
                program->state_defaults[out_i].result_kind =
                    cxpr_model_declared_result_kind(
                        program->state_defaults[out_i].declared_type,
                        cxpr_model_infer_result_kind(program->state_defaults[out_i].ast, compile_reg));
                if (!program->state_defaults[out_i].name ||
                    (model->bindings[i].source && !program->state_defaults[out_i].source) ||
                    (model->bindings[i].declared_type != CXPR_MODEL_DECL_BUFFER &&
                     !program->state_defaults[out_i].ast)) {
                    cxpr_model_compiled_free(program);
                    if (err && err->code == CXPR_OK) {
                        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                    }
                    return NULL;
                }
                out_i++;
            }
        }

        if (executable_count == 0u) goto compile_outputs;

        order = (size_t*)calloc(executable_count, sizeof(size_t));
        program->bindings =
            (cxpr_model_compiled_binding*)calloc(executable_count,
                                                 sizeof(cxpr_model_compiled_binding));
        if (!order || !program->bindings) {
            free(order);
            cxpr_model_compiled_free(program);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        if (!cxpr_model_executable_eval_order(model, order, executable_count, err)) {
            free(order);
            cxpr_model_compiled_free(program);
            return NULL;
        }
        program->binding_count = executable_count;
        for (size_t out_i = 0; out_i < executable_count; ++out_i) {
            size_t src_i = order[out_i];
            program->bindings[out_i].kind = model->bindings[src_i].kind;
            program->bindings[out_i].declared_type = model->bindings[src_i].declared_type;
            program->bindings[out_i].element_type = model->bindings[src_i].element_type;
            program->bindings[out_i].buffer_samples = model->bindings[src_i].buffer_samples;
            if (program->bindings[out_i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) {
                for (size_t state_i = 0u; state_i < model->binding_count; ++state_i) {
                    if (model->bindings[state_i].kind == CXPR_MODEL_BINDING_STATE &&
                        cxpr_model_names_match(model->bindings[state_i].name,
                                               model->bindings[src_i].name)) {
                        program->bindings[out_i].declared_type =
                            model->bindings[state_i].declared_type;
                        program->bindings[out_i].element_type =
                            model->bindings[state_i].element_type;
                        program->bindings[out_i].buffer_samples =
                            model->bindings[state_i].buffer_samples;
                        break;
                    }
                }
            }
            program->bindings[out_i].name = cxpr_strdup(model->bindings[src_i].name);
            program->bindings[out_i].source = cxpr_strdup(model->bindings[src_i].source);
            program->bindings[out_i].name_hash = cxpr_hash_string(model->bindings[src_i].name);
            program->bindings[out_i].ast = cxpr_model_inline_defined_calls(
                model->bindings[src_i].expr, compile_reg, err);
            if (program->bindings[out_i].ast &&
                !cxpr_typecheck(program->bindings[out_i].ast,
                                compile_reg, NULL, err)) {
                free(order);
                cxpr_model_compiled_free(program);
                return NULL;
            }
            program->bindings[out_i].result_kind =
                cxpr_model_declared_result_kind(
                    program->bindings[out_i].declared_type,
                    cxpr_model_infer_result_kind(program->bindings[out_i].ast, compile_reg));
            if (program->bindings[out_i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) {
                program->bindings[out_i].result_kind = cxpr_model_state_default_result_kind(
                    program, program->bindings[out_i].name);
            }
            if (!program->bindings[out_i].name ||
                !program->bindings[out_i].source ||
                !program->bindings[out_i].ast) {
                free(order);
                cxpr_model_compiled_free(program);
                if (err && err->code == CXPR_OK) {
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                }
                return NULL;
            }
        }
        free(order);
    }

compile_outputs:
    if (model->input_count > 0) {
        program->inputs = (char**)calloc(model->input_count, sizeof(char*));
        program->input_declared_types = calloc(model->input_count, sizeof(*program->input_declared_types));
        program->input_element_types = calloc(model->input_count, sizeof(*program->input_element_types));
        if (!program->inputs || !program->input_declared_types || !program->input_element_types) {
            cxpr_model_compiled_free(program);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        program->input_count = model->input_count;
        for (size_t i = 0; i < model->input_count; ++i) {
            program->inputs[i] = cxpr_strdup(model->inputs[i]);
            program->input_declared_types[i] = model->input_declared_types
                ? model->input_declared_types[i] : CXPR_MODEL_DECL_INFERRED;
            program->input_element_types[i] = model->input_element_types
                ? model->input_element_types[i] : CXPR_MODEL_ELEMENT_UNKNOWN;
            if (!program->inputs[i]) {
                cxpr_model_compiled_free(program);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
        }
    }
    if (model->output_count > 0) {
        program->outputs = (char**)calloc(model->output_count, sizeof(char*));
        program->output_declared_types = calloc(model->output_count, sizeof(*program->output_declared_types));
        program->output_element_types = calloc(model->output_count, sizeof(*program->output_element_types));
        if (!program->outputs || !program->output_declared_types || !program->output_element_types) {
            cxpr_model_compiled_free(program);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        program->output_count = model->output_count;
        for (size_t i = 0; i < model->output_count; ++i) {
            program->outputs[i] = cxpr_strdup(model->outputs[i]);
            program->output_declared_types[i] = model->output_declared_types
                ? model->output_declared_types[i] : CXPR_MODEL_DECL_INFERRED;
            program->output_element_types[i] = model->output_element_types
                ? model->output_element_types[i] : CXPR_MODEL_ELEMENT_UNKNOWN;
            if (program->output_declared_types[i] == CXPR_MODEL_DECL_INFERRED) {
                for (size_t j = 0u; j < program->binding_count; ++j) {
                    if (cxpr_model_names_match(program->bindings[j].name, program->outputs[i])) {
                        program->output_declared_types[i] = program->bindings[j].declared_type;
                        program->output_element_types[i] = program->bindings[j].element_type;
                        break;
                    }
                }
            }
            if (!program->outputs[i]) {
                cxpr_model_compiled_free(program);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
        }
    }

    for (size_t i = 0u; i < program->constant_count; ++i) {
        if (!cxpr_model_collect_resamples_ast(program, program->constants[i].ast, err)) goto resample_fail;
    }
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        if (!cxpr_model_collect_resamples_ast(program, program->state_defaults[i].ast, err)) goto resample_fail;
    }
    for (size_t i = 0u; i < program->binding_count; ++i) {
        if (!cxpr_model_collect_resamples_ast(program, program->bindings[i].ast, err)) goto resample_fail;
    }

    if (!cxpr_model_compiled_select_backend(program, model, compile_reg, &compile_options, err)) {
        cxpr_model_compiled_free(program);
        for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
        free(inferred_inputs);
        return NULL;
    }
    if (!cxpr_model_apply_host_binding_schema(program, &compile_options, err)) {
        cxpr_model_compiled_free(program);
        for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
        free(inferred_inputs);
        return NULL;
    }

    for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
    free(inferred_inputs);
    if (expanded_anonymous_outputs) {
        cxpr_model_expanded_copy_free(&inferred_model);
    }
    if (err) err->code = CXPR_OK;
    return program;

resample_fail:
    cxpr_model_compiled_free(program);
    for (size_t i = 0u; i < inferred_input_count; ++i) free(inferred_inputs[i]);
    free(inferred_inputs);
    return NULL;
}
