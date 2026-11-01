/**
 * @file bindings.c
 * @brief Immutable input descriptors and evaluator-owned IR binding state.
 */

#include "internal.h"
#include "../expression/internal.h"
#include <stdatomic.h>

static _Atomic uint64_t g_binding_generation = 1u;

static bool cxpr_ir_input_opcode(cxpr_opcode op, bool* param) {
    switch (op) {
    case CXPR_OP_LOAD_VAR:
    case CXPR_OP_LOAD_VAR_SQUARE:
        *param = false;
        return true;
    case CXPR_OP_LOAD_PARAM:
    case CXPR_OP_LOAD_PARAM_SQUARE:
        *param = true;
        return true;
    default:
        return false;
    }
}

bool cxpr_ir_program_prepare_inputs(cxpr_ir_program* program, cxpr_error* err) {
    cxpr_ir_input_descriptor* inputs;
    size_t input_count = 0u;

    if (!program) return false;
    free(program->inputs);
    program->inputs = NULL;
    program->input_count = 0u;
    if (program->count == 0u) return true;

    inputs = (cxpr_ir_input_descriptor*)calloc(program->count, sizeof(*inputs));
    if (!inputs) {
        free(inputs);
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return false;
    }

    for (size_t ip = 0u; ip < program->count; ++ip) {
        const cxpr_ir_instr* instruction = &program->code[ip];
        bool param = false;
        size_t input_index = SIZE_MAX;
        program->code[ip].input_index = UINT32_MAX;
        if (!cxpr_ir_input_opcode(instruction->op, &param)) continue;
        for (size_t i = 0u; i < input_count; ++i) {
            if (inputs[i].param == param && inputs[i].hash == instruction->hash &&
                strcmp(inputs[i].name, instruction->name) == 0) {
                input_index = i;
                break;
            }
        }
        if (input_index == SIZE_MAX) {
            input_index = input_count++;
            inputs[input_index] = (cxpr_ir_input_descriptor){
                .name = instruction->name,
                .hash = instruction->hash,
                .param = param,
            };
        }
        if (input_index > UINT32_MAX) {
            free(inputs);
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Too many IR inputs";
            }
            return false;
        }
        program->code[ip].input_index = (uint32_t)input_index;
    }

    program->inputs = inputs;
    program->input_count = input_count;
    program->binding_generation = atomic_fetch_add_explicit(
        &g_binding_generation, 1u, memory_order_relaxed);
    if (program->binding_generation == 0u) {
        program->binding_generation = atomic_fetch_add_explicit(
            &g_binding_generation, 1u, memory_order_relaxed);
    }
    return true;
}

void cxpr_ir_bindings_reset(cxpr_ir_bindings* bindings) {
    if (!bindings) return;
    free(bindings->inputs);
    free(bindings->direct_values);
    free(bindings->direct_typed_values);
    *bindings = (cxpr_ir_bindings){0};
}

bool cxpr_ir_bindings_prepare(cxpr_ir_bindings* bindings,
                              const cxpr_ir_program* program, cxpr_error* err) {
    cxpr_ir_lookup_cache* inputs = NULL;
    const double** direct_values = NULL;
    const cxpr_value** direct_typed_values = NULL;

    if (!bindings || !program) return false;
    if (cxpr_ir_bindings_prepared(bindings, program)) {
        return true;
    }
    if (program->count != 0u) {
        inputs = (cxpr_ir_lookup_cache*)calloc(program->count, sizeof(*inputs));
        if (!inputs) {
            free(inputs);
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return false;
        }
    }
    if (program->count != 0u) {
        direct_typed_values =
            (const cxpr_value**)calloc(program->count, sizeof(*direct_typed_values));
        if (!direct_typed_values) {
            free(inputs);
            free(direct_values);
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return false;
        }
    }
    if (program->count != 0u) {
        direct_values = (const double**)calloc(program->count, sizeof(*direct_values));
        if (!direct_values) {
            free(inputs);
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return false;
        }
    }
    free(bindings->inputs);
    free(bindings->direct_values);
    free(bindings->direct_typed_values);
    bindings->program_key = program;
    bindings->program_generation = program->binding_generation;
    bindings->inputs = inputs;
    bindings->direct_values = direct_values;
    bindings->direct_typed_values = direct_typed_values;
    bindings->count = program->input_count;
    bindings->direct_enabled = true;
    return true;
}

void cxpr_ir_bindings_refresh_slow(const cxpr_ir_program* program,
                                   cxpr_ir_bindings* bindings, const cxpr_context* ctx) {
    if (!program || !bindings || !bindings->direct_enabled ||
        bindings->program_key != program ||
        bindings->program_generation != program->binding_generation ||
        !ctx || ctx->parent) {
        if (bindings) bindings->direct_ready = false;
        return;
    }
    if (bindings->direct_ready && bindings->direct_ctx == ctx &&
        bindings->direct_scope == ctx->expression_scope &&
        bindings->variable_base == ctx->variables.entries &&
        bindings->param_base == ctx->params.entries &&
        bindings->variable_version == ctx->variables_version &&
        bindings->param_version == ctx->params_version) {
        return;
    }

    bindings->direct_ready = true;
    bindings->direct_ctx = ctx;
    bindings->direct_scope = ctx->expression_scope;
    bindings->variable_base = ctx->variables.entries;
    bindings->param_base = ctx->params.entries;
    bindings->variable_version = ctx->variables_version;
    bindings->param_version = ctx->params_version;
    for (size_t ip = 0u; ip < program->count; ++ip) {
        const uint32_t input_index = program->code[ip].input_index;
        const cxpr_ir_input_descriptor* input;
        bindings->direct_typed_values[ip] = NULL;
        if (program->code[ip].op == CXPR_OP_LOAD_NAMED_FIELD &&
            !ctx->expression_scope) {
            const cxpr_struct_value* value =
                cxpr_context_lookup_struct_map(&ctx->structs, program->code[ip].name);
            if (value) {
                for (size_t field = 0u; field < value->field_count; ++field) {
                    if (strcmp(value->field_names[field], program->code[ip].aux_name) == 0) {
                        bindings->direct_typed_values[ip] = &value->field_values[field];
                        break;
                    }
                }
            }
        }
        if (input_index == UINT32_MAX || input_index >= program->input_count) {
            bindings->direct_values[ip] = NULL;
            continue;
        }
        input = &program->inputs[input_index];
        if (!input->param && ctx->expression_scope &&
            cxpr_expression_has_name(ctx->expression_scope, input->name)) {
            bindings->direct_values[ip] = NULL;
            continue;
        }
        const cxpr_hashmap* map = input->param ? &ctx->params : &ctx->variables;
        const cxpr_hashmap_entry* entry =
            cxpr_hashmap_find_prehashed_entry(map, input->name, input->hash);
        bindings->direct_values[ip] = entry ? &entry->value : NULL;
    }
}

