#include "model/codegen/internal.h"
#include <cxpr/codegen.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static bool cxpr_model_c_stack_effect(const cxpr_ir_instr* instr,
                                      size_t sp,
                                      size_t* next_sp,
                                      cxpr_error* err) {
    size_t pop = 0u;
    size_t push = 0u;
    if (!instr || !next_sp) return false;
    switch (instr->op) {
    case CXPR_OP_PUSH_CONST:
    case CXPR_OP_PUSH_BOOL:
    case CXPR_OP_LOAD_LOCAL:
    case CXPR_OP_LOAD_LOCAL_SQUARE:
    case CXPR_OP_LOAD_PARAM:
    case CXPR_OP_LOAD_PARAM_SQUARE:
        push = 1u;
        break;
    case CXPR_OP_ADD:
    case CXPR_OP_SUB:
    case CXPR_OP_MUL:
    case CXPR_OP_DIV:
    case CXPR_OP_MOD:
    case CXPR_OP_CMP_EQ:
    case CXPR_OP_CMP_NEQ:
    case CXPR_OP_CMP_LT:
    case CXPR_OP_CMP_LTE:
    case CXPR_OP_CMP_GT:
    case CXPR_OP_CMP_GTE:
    case CXPR_OP_POW:
        pop = 2u;
        push = 1u;
        break;
    case CXPR_OP_SQUARE:
    case CXPR_OP_NOT:
    case CXPR_OP_NEG:
    case CXPR_OP_SIGN:
    case CXPR_OP_SQRT:
    case CXPR_OP_ABS:
    case CXPR_OP_FLOOR:
    case CXPR_OP_CEIL:
    case CXPR_OP_ROUND:
        pop = 1u;
        push = 1u;
        break;
    case CXPR_OP_CLAMP:
        pop = 3u;
        push = 1u;
        break;
    case CXPR_OP_CALL_UNARY:
        pop = 1u;
        push = 1u;
        break;
    case CXPR_OP_CALL_BINARY:
        pop = 2u;
        push = 1u;
        break;
    case CXPR_OP_CALL_FUNC:
    case CXPR_OP_CALL_DEFINED:
        pop = instr->index;
        push = 1u;
        break;
    case CXPR_OP_STORE_LOCAL:
    case CXPR_OP_JUMP_IF_FALSE:
    case CXPR_OP_JUMP_IF_TRUE:
    case CXPR_OP_RETURN:
        pop = 1u;
        break;
    case CXPR_OP_JUMP:
        break;
    default:
        {
            static CXPR_THREAD_LOCAL char msg[128];
            snprintf(msg, sizeof(msg), "Unsupported opcode in model C backend: %s",
                     cxpr_ir_internal_opcode_name(instr->op));
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, msg, 0, 0);
        }
        return false;
    }
    if (sp < pop) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid model C stack depth", 0, 0);
        return false;
    }
    *next_sp = sp - pop + push;
    return true;
}

static bool cxpr_model_c_set_depth(size_t* depths,
                                   bool* queued,
                                   size_t* queue,
                                   size_t* tail,
                                   size_t index,
                                   size_t depth,
                                   size_t count,
                                   cxpr_error* err) {
    if (index >= count) return true;
    if (depths[index] == (size_t)-1) {
        depths[index] = depth;
    } else if (depths[index] != depth) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Inconsistent model C stack depth", 0, 0);
        return false;
    }
    if (!queued[index]) {
        queued[index] = true;
        queue[(*tail)++] = index;
    }
    return true;
}

static bool cxpr_model_c_compute_stack_depths(const cxpr_ir_program* ir,
                                              size_t** out_depths,
                                              size_t* out_max_depth,
                                              cxpr_error* err) {
    size_t* depths;
    bool* queued;
    size_t* queue;
    size_t head = 0u;
    size_t tail = 0u;
    size_t max_depth = 0u;

    if (!ir || !out_depths || !out_max_depth) return false;
    *out_depths = NULL;
    *out_max_depth = 0u;
    depths = (size_t*)malloc(ir->count * sizeof(size_t));
    queued = (bool*)calloc(ir->count ? ir->count : 1u, sizeof(bool));
    queue = (size_t*)malloc(ir->count * sizeof(size_t));
    if ((ir->count > 0u && (!depths || !queued || !queue))) {
        free(depths);
        free(queued);
        free(queue);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    for (size_t i = 0u; i < ir->count; ++i) depths[i] = (size_t)-1;
    if (ir->count > 0u &&
        !cxpr_model_c_set_depth(depths, queued, queue, &tail, 0u, 0u, ir->count, err)) {
        free(depths);
        free(queued);
        free(queue);
        return false;
    }
    while (head < tail) {
        size_t i = queue[head++];
        const cxpr_ir_instr* instr = &ir->code[i];
        size_t sp = depths[i];
        size_t next_sp = sp;
        if (sp > max_depth) max_depth = sp;
        if (!cxpr_model_c_stack_effect(instr, sp, &next_sp, err)) {
            free(depths);
            free(queued);
            free(queue);
            return false;
        }
        if (next_sp > max_depth) max_depth = next_sp;
        if (instr->op == CXPR_OP_JUMP) {
            if (!cxpr_model_c_set_depth(depths, queued, queue, &tail,
                                        instr->index, next_sp, ir->count, err)) goto fail;
        } else if (instr->op == CXPR_OP_JUMP_IF_FALSE ||
                   instr->op == CXPR_OP_JUMP_IF_TRUE) {
            if (!cxpr_model_c_set_depth(depths, queued, queue, &tail,
                                        instr->index, next_sp, ir->count, err)) goto fail;
            if (!cxpr_model_c_set_depth(depths, queued, queue, &tail,
                                        i + 1u, next_sp, ir->count, err)) goto fail;
        } else if (instr->op != CXPR_OP_RETURN) {
            if (!cxpr_model_c_set_depth(depths, queued, queue, &tail,
                                        i + 1u, next_sp, ir->count, err)) goto fail;
        }
    }
    free(queued);
    free(queue);
    *out_depths = depths;
    *out_max_depth = max_depth;
    return true;

fail:
    free(depths);
    free(queued);
    free(queue);
    return false;
}

static bool cxpr_model_c_validate_selected_outputs(const cxpr_model_program* program,
                                                   const size_t* output_indices,
                                                   size_t output_count,
                                                   cxpr_error* err) {
    if (!output_indices) return true;
    if (!program || output_count == 0u) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Model C selected-output backend requires outputs", 0, 0);
        return false;
    }
    for (size_t i = 0u; i < output_count; ++i) {
        if (output_indices[i] >= program->fused_output_count) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Model C selected-output index out of range", 0, 0);
            return false;
        }
    }
    return true;
}

char* cxpr_model_program_to_c_tick_function_select_outputs(
    const cxpr_model_program* program,
    const char* qualifiers,
    const char* function_name,
    const size_t* output_indices,
    size_t output_count,
    cxpr_error* err) {
    cxpr_model_c_buf b = {0};
    char* safe_name;
    size_t* depths = NULL;
    size_t max_depth = 0u;
    char* ast_source = NULL;
    cxpr_error ast_err = {0};

    if (err) *err = (cxpr_error){0};
    if (!program || (!program->has_fused_ir && !program->has_fused_layout) || !function_name) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Model C backend requires fused scalar IR", 0, 0);
        return NULL;
    }
    if (!cxpr_model_c_validate_selected_outputs(program, output_indices, output_count, err)) {
        return NULL;
    }
    if (cxpr_model_program_to_c_tick_function_ast(program, qualifiers, function_name,
                                                 NULL, 0u,
                                                 output_indices,
                                                 output_indices ? output_count : 0u,
                                                 &ast_source, &ast_err)) {
        return ast_source;
    }
    if (ast_err.code == CXPR_ERR_OUT_OF_MEMORY) {
        if (err) *err = ast_err;
        return NULL;
    }
    if (!program->has_fused_ir || !program->fused_ir.code) {
        if (ast_err.code != CXPR_OK) {
            if (err) *err = ast_err;
            return NULL;
        }
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Model C backend requires runnable fused scalar IR fallback", 0, 0);
        return NULL;
    }
    if (err) *err = (cxpr_error){0};

    safe_name = cxpr_model_c_safe_name(function_name);
    if (!safe_name) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }

    if (!cxpr_model_c_emit_defined_functions(program, &b, err)) goto fail;
    if (!cxpr_model_c_compute_stack_depths(&program->fused_ir, &depths, &max_depth, err)) {
        goto fail;
    }

    cxpr_model_c_printf(&b, "/* Source model tick: %s */\n", function_name);
    if (qualifiers && qualifiers[0]) cxpr_model_c_printf(&b, "%s ", qualifiers);
    cxpr_model_c_printf(
        &b,
        "void %s(double* _cx_slots, const double* _cx_inputs, const double* _cx_params, double* _cx_outputs) {\n",
        safe_name);
    free(safe_name);
    safe_name = NULL;
    for (size_t i = 0u; i < max_depth + 1u; ++i) {
        cxpr_model_c_printf(&b, "    double _cx_v%zu;\n", i);
    }
    for (size_t i = 0u; i < program->fused_input_count; ++i) {
        cxpr_model_c_printf(&b, "    _cx_slots[%zu] = _cx_inputs[%zu];\n",
                            program->fused_inputs[i].slot, i);
    }

    for (size_t i = 0u; i < program->fused_ir.count; ++i) {
        const cxpr_ir_instr* instr = &program->fused_ir.code[i];
        const char* op = cxpr_model_c_binary_op(instr->op);
        size_t sp = depths[i];
        cxpr_model_c_printf(&b, "L%zu:\n", i);
        switch (instr->op) {
        case CXPR_OP_PUSH_CONST:
            {
                char raw[64];
                cxpr_model_c_format_double(raw, sizeof(raw), instr->value);
                cxpr_model_c_printf(&b, "    _cx_v%zu = %s;\n", sp, raw);
            }
            break;
        case CXPR_OP_PUSH_BOOL:
            cxpr_model_c_printf(&b, "    _cx_v%zu = %.1f;\n", sp,
                                instr->value != 0.0 ? 1.0 : 0.0);
            break;
        case CXPR_OP_LOAD_LOCAL:
            cxpr_model_c_printf(&b, "    _cx_v%zu = _cx_slots[%zu];\n", sp, instr->index);
            break;
        case CXPR_OP_LOAD_LOCAL_SQUARE:
            cxpr_model_c_printf(&b, "    _cx_v%zu = _cx_slots[%zu] * _cx_slots[%zu];\n",
                                sp, instr->index, instr->index);
            break;
        case CXPR_OP_LOAD_PARAM: {
            size_t param_index = cxpr_model_program_param_index(program, instr->name);
            if (param_index == (size_t)-1) {
                cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                     "Unknown model C parameter", 0, 0);
                goto fail;
            }
            cxpr_model_c_printf(&b, "    _cx_v%zu = _cx_params[%zu];\n", sp, param_index);
            break;
        }
        case CXPR_OP_LOAD_PARAM_SQUARE: {
            size_t param_index = cxpr_model_program_param_index(program, instr->name);
            if (param_index == (size_t)-1) {
                cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                     "Unknown model C parameter", 0, 0);
                goto fail;
            }
            cxpr_model_c_printf(&b, "    _cx_v%zu = _cx_params[%zu] * _cx_params[%zu];\n",
                                sp, param_index, param_index);
            break;
        }
        case CXPR_OP_ADD:
        case CXPR_OP_SUB:
        case CXPR_OP_MUL:
        case CXPR_OP_DIV:
            cxpr_model_c_printf(&b, "    _cx_v%zu = _cx_v%zu %s _cx_v%zu;\n",
                                sp - 2u, sp - 2u, op, sp - 1u);
            break;
        case CXPR_OP_CMP_EQ:
        case CXPR_OP_CMP_NEQ:
        case CXPR_OP_CMP_LT:
        case CXPR_OP_CMP_LTE:
        case CXPR_OP_CMP_GT:
        case CXPR_OP_CMP_GTE:
            cxpr_model_c_printf(&b, "    _cx_v%zu = (_cx_v%zu %s _cx_v%zu) ? 1.0 : 0.0;\n",
                                sp - 2u, sp - 2u, op, sp - 1u);
            break;
        case CXPR_OP_MOD:
            cxpr_model_c_printf(&b, "    _cx_v%zu = fmod(_cx_v%zu, _cx_v%zu);\n",
                                sp - 2u, sp - 2u, sp - 1u);
            break;
        case CXPR_OP_SQUARE:
            cxpr_model_c_printf(&b, "    _cx_v%zu = _cx_v%zu * _cx_v%zu;\n",
                                sp - 1u, sp - 1u, sp - 1u);
            break;
        case CXPR_OP_NOT:
            cxpr_model_c_printf(&b, "    _cx_v%zu = (_cx_v%zu == 0.0) ? 1.0 : 0.0;\n",
                                sp - 1u, sp - 1u);
            break;
        case CXPR_OP_NEG:
            cxpr_model_c_printf(&b, "    _cx_v%zu = -_cx_v%zu;\n", sp - 1u, sp - 1u);
            break;
        case CXPR_OP_SIGN:
            cxpr_model_c_printf(&b, "    _cx_v%zu = (_cx_v%zu > 0.0) - (_cx_v%zu < 0.0);\n",
                                sp - 1u, sp - 1u, sp - 1u);
            break;
        case CXPR_OP_SQRT:
            cxpr_model_c_printf(&b, "    _cx_v%zu = sqrt(_cx_v%zu);\n", sp - 1u, sp - 1u);
            break;
        case CXPR_OP_ABS:
            cxpr_model_c_printf(&b, "    _cx_v%zu = fabs(_cx_v%zu);\n", sp - 1u, sp - 1u);
            break;
        case CXPR_OP_FLOOR:
            cxpr_model_c_printf(&b, "    _cx_v%zu = floor(_cx_v%zu);\n", sp - 1u, sp - 1u);
            break;
        case CXPR_OP_CEIL:
            cxpr_model_c_printf(&b, "    _cx_v%zu = ceil(_cx_v%zu);\n", sp - 1u, sp - 1u);
            break;
        case CXPR_OP_ROUND:
            cxpr_model_c_printf(&b, "    _cx_v%zu = round(_cx_v%zu);\n", sp - 1u, sp - 1u);
            break;
        case CXPR_OP_POW:
            cxpr_model_c_printf(&b, "    _cx_v%zu = pow(_cx_v%zu, _cx_v%zu);\n",
                                sp - 2u, sp - 2u, sp - 1u);
            break;
        case CXPR_OP_CLAMP:
            cxpr_model_c_printf(&b, "    { double _cx_clamp = _cx_v%zu; if (_cx_clamp < _cx_v%zu) _cx_clamp = _cx_v%zu; if (_cx_clamp > _cx_v%zu) _cx_clamp = _cx_v%zu; _cx_v%zu = _cx_clamp; }\n",
                                sp - 3u, sp - 2u, sp - 2u, sp - 1u, sp - 1u, sp - 3u);
            break;
        case CXPR_OP_CALL_UNARY:
            {
                const char* name = instr->func ? instr->func->name : NULL;
                const char* fn = NULL;
                if (cxpr_model_names_match(name, "abs")) fn = "fabs";
                else if (cxpr_model_names_match(name, "sqrt")) fn = "sqrt";
                else if (cxpr_model_names_match(name, "floor")) fn = "floor";
                else if (cxpr_model_names_match(name, "ceil")) fn = "ceil";
                else if (cxpr_model_names_match(name, "round")) fn = "round";
                if (!fn) {
                    cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_FUNCTION,
                                         "Unsupported native call in model C backend", 0, 0);
                    goto fail;
                }
                cxpr_model_c_printf(&b, "    _cx_v%zu = %s(_cx_v%zu);\n",
                                    sp - 1u, fn, sp - 1u);
            }
            break;
        case CXPR_OP_CALL_BINARY:
            {
                const char* name = instr->func ? instr->func->name : NULL;
                const char* fn = NULL;
                if (cxpr_model_names_match(name, "min")) fn = "fmin";
                else if (cxpr_model_names_match(name, "max")) fn = "fmax";
                else if (cxpr_model_names_match(name, "pow")) fn = "pow";
                if (!fn) {
                    cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_FUNCTION,
                                         "Unsupported native call in model C backend", 0, 0);
                    goto fail;
                }
                cxpr_model_c_printf(&b, "    _cx_v%zu = %s(_cx_v%zu, _cx_v%zu);\n",
                                    sp - 2u, fn, sp - 2u, sp - 1u);
            }
            break;
        case CXPR_OP_CALL_FUNC:
            {
                const char* name = instr->func ? instr->func->name : NULL;
                const char* fn = NULL;
                if (cxpr_model_names_match(name, "min")) fn = "fmin";
                else if (cxpr_model_names_match(name, "max")) fn = "fmax";
                if (!fn || instr->index == 0u) {
                    cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_FUNCTION,
                                         "Unsupported variadic call in model C backend", 0, 0);
                    goto fail;
                }
                cxpr_model_c_printf(&b, "    _cx_v%zu = _cx_v%zu;\n",
                                    sp - instr->index, sp - instr->index);
                for (size_t arg = 1u; arg < instr->index; ++arg) {
                    cxpr_model_c_printf(&b, "    _cx_v%zu = %s(_cx_v%zu, _cx_v%zu);\n",
                                        sp - instr->index, fn,
                                        sp - instr->index,
                                        sp - instr->index + arg);
                }
            }
            break;
        case CXPR_OP_CALL_DEFINED:
            {
                char* fn_name;
                if (!instr->func || !instr->func->name) {
                    cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_FUNCTION,
                                         "Unsupported defined call in model C backend", 0, 0);
                    goto fail;
                }
                fn_name = cxpr_model_c_function_name(instr->func->name);
                if (!fn_name) {
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                    goto fail;
                }
                cxpr_model_c_printf(&b, "    _cx_v%zu = %s(",
                                    sp - instr->index, fn_name);
                for (size_t arg = 0u; arg < instr->index; ++arg) {
                    if (arg > 0u) cxpr_model_c_puts(&b, ", ");
                    cxpr_model_c_printf(&b, "_cx_v%zu", sp - instr->index + arg);
                }
                cxpr_model_c_puts(&b, ");\n");
                free(fn_name);
            }
            break;
        case CXPR_OP_JUMP:
            cxpr_model_c_printf(&b, "    goto L%zu;\n", instr->index);
            break;
        case CXPR_OP_JUMP_IF_FALSE:
            cxpr_model_c_printf(&b, "    if (_cx_v%zu == 0.0) goto L%zu;\n",
                                sp - 1u, instr->index);
            break;
        case CXPR_OP_JUMP_IF_TRUE:
            cxpr_model_c_printf(&b, "    if (_cx_v%zu != 0.0) goto L%zu;\n",
                                sp - 1u, instr->index);
            break;
        case CXPR_OP_STORE_LOCAL:
            cxpr_model_c_printf(&b, "    _cx_slots[%zu] = _cx_v%zu;\n",
                                instr->index, sp - 1u);
            break;
        case CXPR_OP_RETURN:
            cxpr_model_c_puts(&b, "    goto _cx_done;\n");
            break;
        default:
            {
                static CXPR_THREAD_LOCAL char msg[128];
                snprintf(msg, sizeof(msg), "Unsupported opcode in model C backend: %s",
                         cxpr_ir_internal_opcode_name(instr->op));
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, msg, 0, 0);
            }
            goto fail;
        }
        if (b.oom) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            goto fail;
        }
    }
    cxpr_model_c_puts(&b, "_cx_done:\n");
    for (size_t i = 0u; i < program->fused_commit_count; ++i) {
        cxpr_model_c_printf(&b, "    _cx_slots[%zu] = _cx_slots[%zu];\n",
                            program->fused_commits[i].state_slot,
                            program->fused_commits[i].update_slot);
    }
    for (size_t out_i = 0u; out_i < (output_indices ? output_count : program->fused_output_count); ++out_i) {
        size_t i = output_indices ? output_indices[out_i] : out_i;
        cxpr_model_c_printf(&b, "    _cx_outputs[%zu] = _cx_slots[%zu];\n",
                            out_i, program->fused_outputs[i].slot);
    }
    cxpr_model_c_puts(&b, "}\n");
    if (b.oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        goto fail;
    }
    if (err) err->code = CXPR_OK;
    free(depths);
    return b.data;

fail:
    free(safe_name);
    free(depths);
    free(b.data);
    return NULL;
}

char* cxpr_model_program_to_c_tick_function(const cxpr_model_program* program,
                                            const char* qualifiers,
                                            const char* function_name,
                                            cxpr_error* err) {
    return cxpr_model_program_to_c_tick_function_select_outputs(
        program, qualifiers, function_name, NULL, 0u, err);
}

char* cxpr_model_program_to_c_tick_function_with_params(const cxpr_model_program* program,
                                                        const char* qualifiers,
                                                        const char* function_name,
                                                        const double* param_values,
                                                        size_t param_count,
                                                        cxpr_error* err) {
    return cxpr_model_program_to_c_tick_function_with_params_select_outputs(
        program, qualifiers, function_name, param_values, param_count, NULL, 0u, err);
}

char* cxpr_model_program_to_c_tick_function_with_params_select_outputs(
    const cxpr_model_program* program,
    const char* qualifiers,
    const char* function_name,
    const double* param_values,
    size_t param_count,
    const size_t* output_indices,
    size_t output_count,
    cxpr_error* err) {
    char* ast_source = NULL;
    cxpr_error ast_err = {0};

    if (err) *err = (cxpr_error){0};
    if (!program || (!program->has_fused_ir && !program->has_fused_layout) || !function_name) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Model C backend requires fused scalar IR", 0, 0);
        return NULL;
    }
    if (!param_values || param_count < program->constant_count) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Model C specialized backend requires all parameter values", 0, 0);
        return NULL;
    }
    if (!cxpr_model_c_validate_selected_outputs(program, output_indices, output_count, err)) {
        return NULL;
    }
    if (cxpr_model_program_to_c_tick_function_ast(program, qualifiers, function_name,
                                                 param_values, param_count,
                                                 output_indices,
                                                 output_indices ? output_count : 0u,
                                                 &ast_source, &ast_err)) {
        return ast_source;
    }
    if (ast_err.code == CXPR_ERR_OUT_OF_MEMORY) {
        if (err) *err = ast_err;
        return NULL;
    }
    return cxpr_model_program_to_c_tick_function_select_outputs(
        program, qualifiers, function_name, output_indices, output_count, err);
}
