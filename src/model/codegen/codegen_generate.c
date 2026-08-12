#include <stdlib.h>
#include <string.h>

#include "model/codegen/codegen_ast_internal.h"

bool cxpr_model_compiled_generate_c_ast(const cxpr_model_compiled* program,
                                               const char* qualifiers,
                                               const char* function_name,
                                               const double* literal_param_values,
                                               size_t literal_param_count,
                                               const size_t* output_indices,
                                               size_t selected_output_count,
                                               char** out_source,
                                               cxpr_error* err) {
    cxpr_model_c_buf b = {0};
    char* safe_name = NULL;
    char** state_next_names = NULL;
    bool* needed_bindings = NULL;
    bool* skip_bindings = NULL;
    char** cse_names = NULL;
    cxpr_model_window_plan window_plan = {0};
    cxpr_model_ast_c_target ast_target_data = {0};
    cxpr_c_target ast_target = {0};
    size_t child_call_capacity = 0u;

    if (out_source) *out_source = NULL;
    if (!program || !program->has_fused_layout || !function_name || !out_source) return false;
    ast_target_data.program = program;
    ast_target_data.function_prefix = function_name;
    ast_target_data.literal_param_values = literal_param_values;
    ast_target_data.literal_param_count = literal_param_count;
    ast_target_data.inline_defined_functions =
        qualifiers && strstr(qualifiers, "__device__") != NULL;
    ast_target.api_version = CXPR_C_TARGET_API_VERSION;
    ast_target.emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf;
    ast_target.emit_call_at_offset = cxpr_model_ast_c_emit_call;
    ast_target.emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback;
    ast_target.userdata = &ast_target_data;
    state_next_names = (char**)calloc(program->fused_slot_count ? program->fused_slot_count : 1u,
                                      sizeof(char*));
    if (!state_next_names && program->fused_slot_count > 0u) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    needed_bindings = (bool*)calloc(program->binding_count ? program->binding_count : 1u,
                                    sizeof(bool));
    if (!needed_bindings && program->binding_count > 0u) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        free(state_next_names);
        return false;
    }
    skip_bindings = (bool*)calloc(program->binding_count ? program->binding_count : 1u,
                                  sizeof(bool));
    if (!skip_bindings && program->binding_count > 0u) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        free(needed_bindings);
        free(state_next_names);
        return false;
    }
    cse_names = (char**)calloc(program->binding_count ? program->binding_count : 1u,
                               sizeof(char*));
    if (!cse_names && program->binding_count > 0u) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        free(skip_bindings);
        free(needed_bindings);
        free(state_next_names);
        return false;
    }
    if (!cxpr_model_compiled_mark_required_bindings(
            program,
            output_indices,
            output_indices ? selected_output_count : 0u,
            output_indices ? false : true,
            true,
            true,
            needed_bindings,
            err)) {
        goto fail;
    }
    /*
     * The AST backend emits identifier references between canonical bindings.
     * Keep every binding materialized: imported producer fields, history
     * captures and selected outputs can retain such references even when the
     * fused-slot dependency walk cannot see through their lowered form.
     */
    for (size_t i = 0u; i < program->binding_count; ++i) {
        needed_bindings[i] = true;
        if (!cxpr_model_collect_resample_cse(
                &ast_target_data, program->bindings[i].ast, 0u)) goto oom;
    }
    if (!cxpr_model_window_plan_build(program, &window_plan, err)) goto fail;
    for (size_t i = 0u; i < program->binding_count; ++i) {
        if (!needed_bindings[i]) continue;
        if (!cxpr_model_c_collect_child_calls_from_ast(
                program,
                program->bindings[i].ast,
                &ast_target_data.child_call_keys,
                &ast_target_data.child_call_child_indices,
                &ast_target_data.child_call_count,
                &child_call_capacity,
                err)) {
            goto fail;
        }
    }
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        if (!cxpr_model_c_collect_child_calls_from_ast(
                program,
                program->history_specs[i].target,
                &ast_target_data.child_call_keys,
                &ast_target_data.child_call_child_indices,
                &ast_target_data.child_call_count,
                &child_call_capacity,
                err)) {
            goto fail;
        }
    }
    cxpr_model_c_emit_common_helpers(&b);
    if (program->resample_requirement_count > 0u) {
        cxpr_model_c_puts(&b,
            "#ifndef CXPR_RESAMPLE_VIEW_ABI_VERSION\n#define CXPR_RESAMPLE_VIEW_ABI_VERSION 1u\n#endif\n"
            "#ifndef CXPR_RESAMPLE_VIEW_VALUE_TYPE\n#define CXPR_RESAMPLE_VIEW_VALUE_TYPE 1u /* numeric double */\n#endif\n"
            "#ifndef CXPR_RESAMPLE_ALIGNMENT_MISSING\n#define CXPR_RESAMPLE_ALIGNMENT_MISSING ((size_t)-1)\n#endif\n"
            "#ifndef CXPR_RESAMPLE_VIEW_DEFINED\n"
            "#define CXPR_RESAMPLE_VIEW_DEFINED 1\n"
            "typedef struct cxpr_resample_view { const double* values; const size_t* alignment; size_t value_count; size_t primary_count; } cxpr_resample_view;\n"
            "#endif\n\n");
    }
    safe_name = cxpr_model_c_safe_name(function_name);
    if (!safe_name) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        goto fail;
    }
    cxpr_model_c_printf(&b, "typedef struct %s_state %s_state;\n\n", safe_name, safe_name);
    if (!cxpr_model_c_emit_child_model_helpers(
            program,
            function_name,
            ast_target_data.child_call_child_indices,
            ast_target_data.child_call_count,
            &b,
            err)) {
        goto fail;
    }
    if (!ast_target_data.inline_defined_functions &&
        !cxpr_model_c_emit_defined_functions_ast(program, function_name, &b, err)) goto fail;
    if (!cxpr_model_c_emit_state_typedefs(
            &b, program, &window_plan, safe_name, err)) {
        goto fail;
    }
    if (!cxpr_model_c_emit_runtime_state_typedef(
            &b,
            program,
            &window_plan,
            safe_name,
            ast_target_data.child_call_child_indices,
            ast_target_data.child_call_count,
            err)) {
        goto fail;
    }
    if (!cxpr_model_c_emit_slot_init_function(
            &b, program, &window_plan, qualifiers, safe_name, err)) {
        goto fail;
    }
    cxpr_model_c_printf(&b, "/* Source model tick: %s */\n", function_name);
    if (qualifiers && qualifiers[0]) cxpr_model_c_printf(&b, "%s ", qualifiers);
    if (program->resample_requirement_count > 0u) {
        cxpr_model_c_printf(
            &b,
            "void %s(%s_state* restrict _cx_state, const double* restrict _cx_inputs, const double* restrict _cx_params, double* restrict _cx_outputs, const cxpr_resample_view* restrict _cx_resample_views, size_t _cx_primary_cursor) {\n",
            safe_name, safe_name);
    } else {
        cxpr_model_c_printf(
            &b,
            "void %s(%s_state* restrict _cx_state, const double* restrict _cx_inputs, const double* restrict _cx_params, double* restrict _cx_outputs) {\n",
            safe_name, safe_name);
    }

    {
        size_t init_sentinel = 0u;
        if (cxpr_model_c_init_sentinel_slot(program, &window_plan, &init_sentinel)) {
            (void)init_sentinel;
            cxpr_model_c_printf(
                &b,
                "    if (CXPR_UNLIKELY(_cx_state->init == 0u)) %s_init_state(_cx_state);\n",
                safe_name);
        }
    }
    for (size_t i = 0u; i < program->fused_input_count; ++i) {
        char* name = cxpr_model_c_prefixed_name("_cx_input_", program->fused_inputs[i].name);
        if (!name) goto oom;
        cxpr_model_c_printf(&b, "    const double _cx_input_%zu = _cx_inputs[%zu];\n", i, i);
        free(name);
    }
    if (!literal_param_values || literal_param_count < program->constant_count) {
        for (size_t i = 0u; i < program->constant_count; ++i) {
            const cxpr_model_compiled_binding* param = &program->constants[i];
            char min_raw[64];
            char max_raw[64];
            cxpr_model_c_format_double(min_raw, sizeof(min_raw), param->min_value);
            cxpr_model_c_format_double(max_raw, sizeof(max_raw), param->max_value);
            if (param->has_min_value && param->has_max_value) {
                cxpr_model_c_printf(
                    &b,
                    "    const double _cx_param_%zu = isfinite(_cx_params[%zu]) ? "
                    "fmax(%s, fmin(%s, _cx_params[%zu])) : _cx_params[%zu];\n",
                    i, i, min_raw, max_raw, i, i);
            } else if (param->has_min_value) {
                cxpr_model_c_printf(
                    &b,
                    "    const double _cx_param_%zu = isfinite(_cx_params[%zu]) ? "
                    "fmax(%s, _cx_params[%zu]) : _cx_params[%zu];\n",
                    i, i, min_raw, i, i);
            } else if (param->has_max_value) {
                cxpr_model_c_printf(
                    &b,
                    "    const double _cx_param_%zu = isfinite(_cx_params[%zu]) ? "
                    "fmin(%s, _cx_params[%zu]) : _cx_params[%zu];\n",
                    i, i, max_raw, i, i);
            } else {
                cxpr_model_c_printf(
                    &b,
                    "    const double _cx_param_%zu = _cx_params[%zu];\n", i, i);
            }
        }
    }
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        char* name = cxpr_model_c_prefixed_name("_cx_state_", program->state_defaults[i].name);
        char* field_name = cxpr_model_c_prefixed_name("state_", program->state_defaults[i].name);
        if (!name || !field_name) {
            free(name);
            free(field_name);
            goto fail;
        }
        cxpr_model_c_printf(&b, "    const double %s = _cx_state->%s;\n", name, field_name);
        free(name);
        free(field_name);
    }
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        size_t depth = program->history_specs[i].depth;
        size_t capacity = cxpr_model_c_history_capacity(depth);
        if (depth == 0u) continue;
        if (capacity > 1u && !cxpr_model_c_history_use_shift(depth)) {
            cxpr_model_c_printf(&b, "    const size_t _cx_history_next_%zu = (size_t)_cx_state->history_%zu.next;\n",
                                i, i);
        }
    }
    for (size_t i = 0u; i < ast_target_data.resample_cse_count; ++i) {
        const cxpr_model_resample_cse* cse = &ast_target_data.resample_cse[i];
        if (cse->uses < 2u) continue;
        cxpr_model_c_printf(&b,
            "    const size_t _cx_resample_cursor_%zu_%u = (_cx_primary_cursor < _cx_resample_views[%zu].primary_count && _cx_resample_views[%zu].alignment) ? _cx_resample_views[%zu].alignment[_cx_primary_cursor] : (size_t)-1;\n"
            "    const double _cx_resample_value_%zu_%u = (_cx_resample_views[%zu].values && _cx_resample_cursor_%zu_%u >= %uu && _cx_resample_cursor_%zu_%u - %uu < _cx_resample_views[%zu].value_count) ? _cx_resample_views[%zu].values[_cx_resample_cursor_%zu_%u - %uu] : NAN;\n",
            cse->slot, cse->lookback, cse->slot, cse->slot, cse->slot,
            cse->slot, cse->lookback,
            cse->slot,
            cse->slot, cse->lookback, cse->lookback,
            cse->slot, cse->lookback, cse->lookback,
            cse->slot, cse->slot, cse->slot, cse->lookback, cse->lookback);
    }
    for (size_t i = 0u; i < ast_target_data.child_call_count; ++i) {
        cxpr_model_c_printf(&b, "    _cx_state->child_call_%zu_initialized = 0u;\n", i);
    }
    if (program->invalid_input_guard) {
        size_t guard_index = (size_t)-1;
        char* guard_expr;
        char* guard_name;
        size_t emitted_output_count =
            output_indices ? selected_output_count : program->fused_output_count;
        for (size_t i = 0u; i < program->binding_count; ++i) {
            if (program->bindings[i].kind != CXPR_MODEL_BINDING_STATE_UPDATE &&
                cxpr_model_names_match(
                    program->bindings[i].name, program->invalid_input_guard)) {
                guard_index = i;
                break;
            }
        }
        if (guard_index == (size_t)-1) {
            cxpr_model_set_error(
                err, CXPR_ERR_SYNTAX,
                "Model invalid_input_guard references unknown binding", 0, 0);
            goto fail;
        }
        guard_expr = cxpr_expr_ast_to_c(
            program->bindings[guard_index].ast, &ast_target, err);
        guard_name = cxpr_model_c_safe_name(program->invalid_input_guard);
        if (!guard_expr || !guard_name) {
            free(guard_expr);
            free(guard_name);
            goto fail;
        }
        cxpr_model_c_emit_source_comment(
            &b, ".cxpr guard", program->bindings[guard_index].source);
        cxpr_model_c_printf(&b, "    const bool %s =\n", guard_name);
        for (size_t i = 0u; i < program->fused_input_count; ++i) {
            cxpr_model_c_printf(
                &b, "        isfinite(_cx_input_%zu) &&\n", i);
        }
        for (size_t i = 0u; i < program->constant_count; ++i) {
            cxpr_model_c_printf(
                &b, "        isfinite(_cx_params[%zu]) &&\n", i);
        }
        if (cxpr_expr_ast_kind_of(program->bindings[guard_index].ast) == CXPR_NODE_BOOL &&
            cxpr_expr_ast_bool_value(program->bindings[guard_index].ast)) {
            cxpr_model_c_puts(&b, "        true;\n");
        } else {
            cxpr_model_c_printf(&b, "        ((%s) != 0);\n", guard_expr);
        }
        cxpr_model_c_printf(&b, "    if (CXPR_UNLIKELY(!%s)) {\n", guard_name);
        for (size_t i = 0u; i < emitted_output_count; ++i) {
            cxpr_model_c_printf(&b, "        _cx_outputs[%zu] = NAN;\n", i);
        }
        cxpr_model_c_puts(&b, "        return;\n    }\n");
        skip_bindings[guard_index] = true;
        free(guard_expr);
        free(guard_name);
    }
    for (size_t i = 0u; i < program->binding_count; ++i) {
        char* expr;
        char* name;
        bool owns_name = false;
        if (!needed_bindings[i] || skip_bindings[i]) continue;
        if (cxpr_model_ast_is_record_like(program, program->bindings[i].ast, 0u)) continue;
        if (program->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) {
            size_t slot = cxpr_model_fused_slot_find(program->fused_slot_names,
                                                     program->fused_slot_count,
                                                     program->bindings[i].name);
            name = cxpr_model_c_prefixed_name("_cx_next_", program->bindings[i].name);
            if (slot == (size_t)-1) {
                free(name);
                name = NULL;
            } else if (name) {
                state_next_names[slot] = name;
            }
        } else {
            name = cxpr_model_c_safe_name(program->bindings[i].name);
            owns_name = true;
        }
        if (!name) goto oom;
        if (program->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE &&
            cxpr_expr_ast_kind_of(program->bindings[i].ast) == CXPR_NODE_IDENTIFIER) {
            size_t slot = cxpr_model_fused_slot_find(
                program->fused_slot_names,
                program->fused_slot_count,
                program->bindings[i].name);
            expr = cxpr_expr_ast_to_c(program->bindings[i].ast, &ast_target, err);
            if (!expr || slot == (size_t)-1) {
                free(expr);
                goto fail;
            }
            free(state_next_names[slot]);
            state_next_names[slot] = expr;
            continue;
        }
        if (program->bindings[i].kind != CXPR_MODEL_BINDING_STATE_UPDATE) {
            const char* common = cxpr_model_c_find_common_binding_expr(
                program, i, needed_bindings, skip_bindings, cse_names);
            if (common) {
                cse_names[i] = cxpr_strdup(common);
                if (!cse_names[i]) {
                    if (owns_name) free(name);
                    goto oom;
                }
                cxpr_model_c_emit_source_comment(&b, ".cxpr", program->bindings[i].source);
                cxpr_model_c_printf(
                    &b, "    const %s %s = %s;\n",
                    program->bindings[i].result_kind == CXPR_MODEL_RESULT_BOOL
                        ? "bool"
                        : "double",
                    name, common);
                if (owns_name) free(name);
                continue;
            }
        }
        if (i + 1u < program->binding_count &&
            needed_bindings[i + 1u] &&
            !skip_bindings[i + 1u] &&
            program->bindings[i].kind != CXPR_MODEL_BINDING_STATE_UPDATE &&
            program->bindings[i + 1u].kind != CXPR_MODEL_BINDING_STATE_UPDATE) {
            char* next_name = cxpr_model_c_safe_name(program->bindings[i + 1u].name);
            if (!next_name) {
                if (owns_name) free(name);
                goto oom;
            }
            if (cxpr_model_c_emit_optimized_binding_pair(
                    &b,
                    name,
                    next_name,
                    program->bindings[i].ast,
                    program->bindings[i + 1u].ast,
                    &ast_target,
                    program,
                    err)) {
                skip_bindings[i + 1u] = true;
                cse_names[i] = cxpr_strdup(name);
                cse_names[i + 1u] = cxpr_strdup(next_name);
                if (!cse_names[i] || !cse_names[i + 1u]) {
                    free(next_name);
                    if (owns_name) free(name);
                    goto oom;
                }
                free(next_name);
                if (owns_name) free(name);
                continue;
            } else if (err && err->code != CXPR_OK) {
                free(next_name);
                if (owns_name) free(name);
                goto fail;
            }
            free(next_name);
        }
        {
            const cxpr_model_window_plan_node* window_node =
                cxpr_model_window_plan_find_ast(&window_plan, program->bindings[i].ast);
            if (window_node &&
                cxpr_model_c_emit_planned_roc_aggregate_binding(
                    &b, name, &window_plan, window_node, &ast_target, program, err)) {
                cse_names[i] = cxpr_strdup(name);
                if (!cse_names[i]) {
                    if (owns_name) free(name);
                    goto oom;
                }
                if (owns_name) free(name);
                continue;
            } else if (window_node &&
                       cxpr_model_c_emit_planned_simple_aggregate_binding(
                           &b, name, &window_plan, window_node, &ast_target, program, err)) {
                cse_names[i] = cxpr_strdup(name);
                if (!cse_names[i]) {
                    if (owns_name) free(name);
                    goto oom;
                }
                if (owns_name) free(name);
                continue;
            } else if (err && err->code != CXPR_OK) {
                if (owns_name) free(name);
                goto fail;
            }
        }
        if (cxpr_model_c_emit_optimized_single_binding(
                &b, name, program->bindings[i].ast, &ast_target, program, err)) {
            cse_names[i] = cxpr_strdup(name);
            if (!cse_names[i]) {
                if (owns_name) free(name);
                goto oom;
            }
            if (owns_name) free(name);
            continue;
        } else if (err && err->code != CXPR_OK) {
            if (owns_name) free(name);
            goto fail;
        }
        expr = cxpr_expr_ast_to_c(program->bindings[i].ast, &ast_target, err);
        if (!expr) {
            if (owns_name) free(name);
            goto fail;
        }
        cxpr_model_c_emit_source_comment(&b, ".cxpr", program->bindings[i].source);
        cxpr_model_c_printf(
            &b, "    const %s %s = %s;\n",
            program->bindings[i].result_kind == CXPR_MODEL_RESULT_BOOL
                ? "bool"
                : "double",
            name, expr);
        free(expr);
        cse_names[i] = cxpr_strdup(name);
        if (!cse_names[i]) {
            if (owns_name) free(name);
            goto oom;
        }
        if (owns_name) free(name);
    }
    for (size_t i = 0u; i < program->fused_commit_count; ++i) {
        char* next_name = state_next_names[program->fused_commits[i].state_slot];
        char* field_name = cxpr_model_c_prefixed_name(
            "state_", program->fused_slot_names[program->fused_commits[i].state_slot]);
        if (!next_name) goto oom;
        if (!field_name) {
            free(field_name);
            goto oom;
        }
        cxpr_model_c_printf(&b, "    _cx_state->%s = %s;\n", field_name, next_name);
        free(field_name);
    }
    for (size_t out_i = 0u; out_i < (output_indices ? selected_output_count : program->fused_output_count); ++out_i) {
        size_t i = output_indices ? output_indices[out_i] : out_i;
        const char* name;
        size_t state_slot = 0u;
        if (i >= program->fused_output_count) goto fail;
        name = program->fused_outputs[i].name;
        if (cxpr_model_c_symbol_is_state(program, name, &state_slot)) {
            char* field_name = cxpr_model_c_prefixed_name("state_", name);
            (void)state_slot;
            if (!field_name) {
                free(field_name);
                goto oom;
            }
            cxpr_model_c_emit_source_comment(&b, ".cxpr", cxpr_model_c_source_for_name(program, name));
            cxpr_model_c_printf(&b, "    _cx_outputs[%zu] = _cx_state->%s;\n",
                                out_i, field_name);
            free(field_name);
        } else {
            char* local_name = cxpr_model_c_safe_name(name);
            if (!local_name) goto oom;
            cxpr_model_c_emit_source_comment(&b, ".cxpr", cxpr_model_c_source_for_name(program, name));
            cxpr_model_c_printf(&b, "    _cx_outputs[%zu] = %s;\n", out_i, local_name);
            free(local_name);
        }
    }
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        size_t depth = program->history_specs[i].depth;
        size_t capacity = cxpr_model_c_history_capacity(depth);
        char* current = NULL;
        if (program->history_specs[i].target) {
            current = cxpr_model_ast_expr_to_c(program,
                                               program->history_specs[i].target,
                                               function_name,
                                               literal_param_values,
                                               literal_param_count,
                                               ast_target_data.child_call_keys,
                                               ast_target_data.child_call_child_indices,
                                               ast_target_data.child_call_count,
                                               err);
        } else {
            current = cxpr_model_c_current_symbol_expr(
                program, state_next_names, program->history_specs[i].name, err);
        }
        if (!current) goto fail;
        if (depth > 0u) {
            if (cxpr_model_c_history_use_shift(depth)) {
                for (size_t j = depth; j > 1u; --j) {
                    cxpr_model_c_printf(&b, "    _cx_state->history_%zu.values[%zu] = _cx_state->history_%zu.values[%zu];\n",
                                        i,
                                        j - 1u,
                                        i,
                                        j - 2u);
                }
                cxpr_model_c_printf(
                    &b,
                    "    _cx_state->history_%zu.values[0] = %s;\n",
                    i,
                    current);
            } else if (cxpr_model_c_is_power_of_two(capacity)) {
                cxpr_model_c_printf(
                    &b,
                    "    { const double _cx_history_value_%zu = %s; _cx_state->history_%zu.values[_cx_history_next_%zu] = _cx_history_value_%zu; _cx_state->history_%zu.next = (%s)((_cx_history_next_%zu + 1u) & %zuu); }\n",
                    i,
                    current,
                    i,
                    i,
                    i,
                    i,
                    cxpr_model_c_history_counter_type(capacity),
                    i,
                    capacity - 1u);
            } else {
                cxpr_model_c_printf(
                    &b,
                    "    { const double _cx_history_value_%zu = %s; _cx_state->history_%zu.values[_cx_history_next_%zu] = _cx_history_value_%zu; _cx_state->history_%zu.next = (%s)((_cx_history_next_%zu + 1u) %% %zuu); }\n",
                    i,
                    current,
                    i,
                    i,
                    i,
                    i,
                    cxpr_model_c_history_counter_type(capacity),
                    i,
                    capacity);
            }
        }
        free(current);
    }
    cxpr_model_c_puts(&b, "}\n");
    if (b.oom) goto oom;
    for (size_t i = 0u; i < program->fused_slot_count; ++i) free(state_next_names[i]);
    free(state_next_names);
    free(needed_bindings);
    free(skip_bindings);
    for (size_t i = 0u; i < program->binding_count; ++i) free(cse_names[i]);
    free(cse_names);
    for (size_t i = 0u; i < ast_target_data.child_call_count; ++i) {
        free(ast_target_data.child_call_keys[i]);
    }
    free(ast_target_data.child_call_keys);
    free(ast_target_data.child_call_child_indices);
    free(ast_target_data.resample_cse);
    free(safe_name);
    cxpr_model_window_plan_free(&window_plan);
    *out_source = b.data;
    return true;

oom:
    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
fail:
    free(safe_name);
    if (state_next_names) {
        for (size_t i = 0u; i < program->fused_slot_count; ++i) free(state_next_names[i]);
        free(state_next_names);
    }
    free(needed_bindings);
    free(skip_bindings);
    if (cse_names) {
        for (size_t i = 0u; i < program->binding_count; ++i) free(cse_names[i]);
        free(cse_names);
    }
    for (size_t i = 0u; i < ast_target_data.child_call_count; ++i) {
        free(ast_target_data.child_call_keys[i]);
    }
    free(ast_target_data.child_call_keys);
    free(ast_target_data.child_call_child_indices);
    free(ast_target_data.resample_cse);
    cxpr_model_window_plan_free(&window_plan);
    free(b.data);
    return false;
}
