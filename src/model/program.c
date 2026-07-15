#include "core.h"
#include "model/internal.h"
#include "model/window/plan.h"
#include "registry/internal.h"
#include <cxpr/codegen.h>
#include <stdlib.h>

static void cxpr_model_compiled_binding_free(cxpr_model_compiled_binding* binding) {
    if (!binding) return;
    free(binding->name);
    cxpr_ast_free(binding->ast);
    binding->name = NULL;
    binding->name_hash = 0u;
    binding->result_kind = CXPR_IR_VIEW_RESULT_UNKNOWN;
    binding->ast = NULL;
}

static bool cxpr_model_eval_ast_bool_result(const cxpr_ast* ast,
                                            const cxpr_context* ctx,
                                            const cxpr_registry* reg,
                                            bool* out_value,
                                            cxpr_error* err) {
    cxpr_value value = {0};
    if (!cxpr_eval_ast(ast, ctx, reg, &value, err)) return false;
    if (value.type != CXPR_VALUE_BOOL) {
        cxpr_value_free(&value);
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expression did not evaluate to bool";
        }
        return false;
    }
    if (out_value) *out_value = value.b;
    cxpr_value_free(&value);
    return true;
}

void cxpr_model_context_set_compiled_number(cxpr_context* ctx,
                                            const cxpr_model_compiled_binding* binding,
                                            double value) {
    if (!ctx || !binding || !binding->name) return;
    cxpr_context_set_prehashed(ctx, binding->name, binding->name_hash, value);
}

void cxpr_model_context_set_compiled_bool(cxpr_context* ctx,
                                          const cxpr_model_compiled_binding* binding,
                                          bool value) {
    if (!ctx || !binding || !binding->name) return;
    cxpr_context_set_bool(ctx, binding->name, value);
}

void cxpr_model_context_set_compiled_typed(cxpr_context* ctx,
                                           const cxpr_model_compiled_binding* binding,
                                           const cxpr_value* value) {
    if (!ctx || !binding || !binding->name || !value) return;
    cxpr_context_set_value(ctx, binding->name, value);
}

static void cxpr_model_history_spec_free(cxpr_model_history_spec* spec) {
    if (!spec) return;
    free(spec->name);
    cxpr_ast_free(spec->target);
    spec->name = NULL;
    spec->target = NULL;
    spec->depth = 0u;
}

void cxpr_model_program_free(cxpr_model_program* program) {
    if (!program) return;
    cxpr_model_fused_program_clear(program);
    for (size_t i = 0; i < program->constant_count; ++i) {
        cxpr_model_compiled_binding_free(&program->constants[i]);
    }
    free(program->constants);
    for (size_t i = 0; i < program->state_default_count; ++i) {
        cxpr_model_compiled_binding_free(&program->state_defaults[i]);
    }
    free(program->state_defaults);
    for (size_t i = 0; i < program->binding_count; ++i) {
        cxpr_model_compiled_binding_free(&program->bindings[i]);
    }
    free(program->bindings);
    for (size_t i = 0; i < program->input_count; ++i) free(program->inputs[i]);
    free(program->inputs);
    free(program->source_arg);
    for (size_t i = 0; i < program->child_count; ++i) {
        free(program->children[i].name);
        free(program->children[i].source_arg);
    }
    free(program->children);
    for (size_t i = 0; i < program->history_spec_count; ++i) {
        cxpr_model_history_spec_free(&program->history_specs[i]);
    }
    free(program->history_specs);
    for (size_t i = 0; i < program->output_count; ++i) free(program->outputs[i]);
    free(program->outputs);
    cxpr_registry_free(program->registry);
    free(program);
}

bool cxpr_model_program_seed_defaults(const cxpr_model_program* program,
                                      cxpr_context* ctx,
                                      const cxpr_registry* reg,
                                      cxpr_error* err) {
    const cxpr_registry* eval_reg;
    if (err) *err = (cxpr_error){0};
    if (!program || !ctx) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid model default arguments", 0, 0);
        return false;
    }
    eval_reg = program->registry ? program->registry : reg;

    for (size_t i = 0; i < program->constant_count; ++i) {
        cxpr_value value = {0};
        if (!cxpr_eval_ast(program->constants[i].ast, ctx, eval_reg, &value, err)) {
            return false;
        }
        cxpr_context_set_param_value(ctx, program->constants[i].name, &value);
        cxpr_value_free(&value);
    }

    if (err) err->code = CXPR_OK;
    return true;
}

bool cxpr_eval_model_program(const cxpr_model_program* program,
                             cxpr_context* ctx,
                             const cxpr_registry* reg,
                             cxpr_error* err) {
    const cxpr_registry* eval_reg;
    if (err) *err = (cxpr_error){0};
    if (!program || !ctx) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid model eval arguments", 0, 0);
        return false;
    }
    eval_reg = program->registry ? program->registry : reg;

    for (size_t i = 0; i < program->binding_count; ++i) {
        if (program->bindings[i].result_kind == CXPR_IR_VIEW_RESULT_NUMBER) {
            double value = 0.0;
            if (!cxpr_eval_ast_number(
                    program->bindings[i].ast, ctx, eval_reg, &value, err)) {
                return false;
            }
            cxpr_model_context_set_compiled_number(ctx, &program->bindings[i], value);
        } else if (program->bindings[i].result_kind == CXPR_IR_VIEW_RESULT_BOOL) {
            bool value = false;
            if (!cxpr_model_eval_ast_bool_result(
                    program->bindings[i].ast, ctx, eval_reg, &value, err)) {
                return false;
            }
            cxpr_model_context_set_compiled_bool(ctx, &program->bindings[i], value);
        } else {
            cxpr_value value = {0};
            if (!cxpr_eval_ast(program->bindings[i].ast, ctx, eval_reg, &value, err)) {
                return false;
            }
            cxpr_model_context_set_compiled_typed(ctx, &program->bindings[i], &value);
            cxpr_value_free(&value);
        }
    }

    if (err) err->code = CXPR_OK;
    return true;
}

size_t cxpr_model_program_binding_count(const cxpr_model_program* program) {
    return program ? program->binding_count : 0;
}

const char* cxpr_model_program_binding_name(const cxpr_model_program* program, size_t index) {
    return program && index < program->binding_count ? program->bindings[index].name : NULL;
}

size_t cxpr_model_program_output_count(const cxpr_model_program* program) {
    return program ? program->output_count : 0;
}

const char* cxpr_model_program_output_name(const cxpr_model_program* program, size_t index) {
    return program && index < program->output_count ? program->outputs[index] : NULL;
}

size_t cxpr_model_program_input_count(const cxpr_model_program* program) {
    return program ? program->input_count : 0u;
}

const char* cxpr_model_program_input_name(const cxpr_model_program* program, size_t index) {
    return program && index < program->input_count ? program->inputs[index] : NULL;
}

size_t cxpr_model_program_function_count(const cxpr_model_program* program) {
    return program && program->registry ? program->registry->count : 0u;
}

bool cxpr_model_program_uses_fused_ir(const cxpr_model_program* program) {
    return program && program->has_fused_ir;
}

size_t cxpr_model_program_fused_ir_instruction_count(const cxpr_model_program* program) {
    return program && program->has_fused_ir ? program->fused_ir.count : 0u;
}

const char* cxpr_model_program_fused_ir_disabled_opcode(const cxpr_model_program* program) {
    return program ? program->fused_disabled_opcode : NULL;
}

static bool cxpr_model_history_use_shift(size_t depth) {
    return depth <= 4u;
}

static size_t cxpr_model_history_capacity(size_t depth) {
    size_t capacity = 1u;
    if (cxpr_model_history_use_shift(depth)) return depth;
    while (capacity < depth && capacity <= ((size_t)-1) / 2u) capacity *= 2u;
    return capacity < depth ? depth : capacity;
}

static size_t cxpr_model_program_c_extra_slot_count(const cxpr_model_program* program) {
    (void)program;
    return 0u;
}

size_t cxpr_model_program_c_slot_count(const cxpr_model_program* program) {
    (void)program;
    return 0u;
}

size_t cxpr_model_program_c_param_count(const cxpr_model_program* program) {
    return program ? program->constant_count : 0u;
}

const char* cxpr_model_program_c_param_name(const cxpr_model_program* program, size_t index) {
    return program && index < program->constant_count ? program->constants[index].name : NULL;
}
