/**
 * @file model.c
 * @brief Minimal host-agnostic .cxpr model parser.
 */

#include "core.h"
#include "ir/compile/internal.h"
#include "ir/exec/internal.h"
#include "lookback.h"
#include "model/internal.h"
#include "registry/internal.h"
#include <cxpr/codegen.h>
#include <cxpr/source_plan.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



void cxpr_model_set_error(cxpr_error* err, cxpr_error_code code,
                          const char* message, size_t line, size_t column) {
    if (!err) return;
    err->code = code;
    err->message = message;
    err->position = 0;
    err->line = line;
    err->column = column;
}


static void cxpr_model_compiled_binding_free(cxpr_model_compiled_binding* binding) {
    if (!binding) return;
    free(binding->name);
    cxpr_program_free(binding->program);
    binding->name = NULL;
    binding->name_hash = 0u;
    binding->result_kind = CXPR_IR_VIEW_RESULT_UNKNOWN;
    binding->program = NULL;
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

static size_t cxpr_model_program_binding_index_for_name(const cxpr_model_program* program,
                                                        const char* name) {
    if (!program || !name) return (size_t)-1;
    for (size_t i = 0u; i < program->binding_count; ++i) {
        if (cxpr_model_names_match(program->bindings[i].name, name)) return i;
    }
    return (size_t)-1;
}

static bool cxpr_model_program_mark_required_symbol(const cxpr_model_program* program,
                                                    const char* name,
                                                    bool* out_required,
                                                    cxpr_error* err);

static bool cxpr_model_program_mark_required_ast(const cxpr_model_program* program,
                                                 const cxpr_ast* ast,
                                                 bool* out_required,
                                                 cxpr_error* err) {
    if (!ast) return true;
    switch (cxpr_ast_type(ast)) {
    case CXPR_NODE_IDENTIFIER:
        return cxpr_model_program_mark_required_symbol(
            program, cxpr_ast_identifier_name(ast), out_required, err);
    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_model_program_mark_required_symbol(
            program, cxpr_ast_field_object(ast), out_required, err);
    case CXPR_NODE_CHAIN_ACCESS:
        return cxpr_model_program_mark_required_symbol(
            program, cxpr_ast_chain_segment(ast, 0u), out_required, err);
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_program_mark_required_ast(program, cxpr_ast_left(ast), out_required, err) &&
               cxpr_model_program_mark_required_ast(program, cxpr_ast_right(ast), out_required, err);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_program_mark_required_ast(program, cxpr_ast_operand(ast), out_required, err);
    case CXPR_NODE_FUNCTION_CALL:
        for (size_t i = 0u; i < cxpr_ast_function_argc(ast); ++i) {
            if (!cxpr_model_program_mark_required_ast(
                    program, cxpr_ast_function_arg(ast, i), out_required, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_PRODUCER_ACCESS:
        for (size_t i = 0u; i < cxpr_ast_producer_argc(ast); ++i) {
            if (!cxpr_model_program_mark_required_ast(
                    program, cxpr_ast_producer_arg(ast, i), out_required, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_LOOKBACK:
        return cxpr_model_program_mark_required_ast(
                   program, cxpr_ast_lookback_target(ast), out_required, err) &&
               cxpr_model_program_mark_required_ast(
                   program, cxpr_ast_lookback_index(ast), out_required, err);
    case CXPR_NODE_TERNARY:
        return cxpr_model_program_mark_required_ast(
                   program, cxpr_ast_ternary_condition(ast), out_required, err) &&
               cxpr_model_program_mark_required_ast(
                   program, cxpr_ast_ternary_true_branch(ast), out_required, err) &&
               cxpr_model_program_mark_required_ast(
                   program, cxpr_ast_ternary_false_branch(ast), out_required, err);
    default:
        return true;
    }
}

static bool cxpr_model_program_mark_required_symbol(const cxpr_model_program* program,
                                                    const char* name,
                                                    bool* out_required,
                                                    cxpr_error* err) {
    size_t index = cxpr_model_program_binding_index_for_name(program, name);
    if (index == (size_t)-1) return true;
    if (out_required[index]) return true;
    out_required[index] = true;
    if (!program->bindings[index].program) return true;
    return cxpr_model_program_mark_required_ast(
        program, program->bindings[index].program->ast, out_required, err);
}

bool cxpr_model_program_mark_required_bindings(const cxpr_model_program* program,
                                               const size_t* output_indices,
                                               size_t output_count,
                                               bool include_all_outputs,
                                               bool include_state_commits,
                                               bool include_history_captures,
                                               bool* out_required,
                                               cxpr_error* err) {
    if (!program || !out_required) return false;
    if (include_all_outputs) {
        for (size_t i = 0u; i < program->fused_output_count; ++i) {
            if (!cxpr_model_program_mark_required_symbol(
                    program, program->fused_outputs[i].name, out_required, err)) {
                return false;
            }
        }
    } else {
        if (output_count > 0u && !output_indices) return false;
        for (size_t out_i = 0u; out_i < output_count; ++out_i) {
            size_t i = output_indices[out_i];
            if (i >= program->fused_output_count) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Model selected-output index out of range", 0, 0);
                return false;
            }
            if (!cxpr_model_program_mark_required_symbol(
                    program, program->fused_outputs[i].name, out_required, err)) {
                return false;
            }
        }
    }
    if (include_state_commits) {
        for (size_t i = 0u; i < program->fused_commit_count; ++i) {
            size_t slot = program->fused_commits[i].state_slot;
            if (slot >= program->fused_slot_count) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Model state commit slot out of range", 0, 0);
                return false;
            }
            if (!cxpr_model_program_mark_required_symbol(
                    program, program->fused_slot_names[slot], out_required, err)) {
                return false;
            }
        }
    }
    if (include_history_captures) {
        for (size_t i = 0u; i < program->history_spec_count; ++i) {
            if (!cxpr_model_program_mark_required_symbol(
                    program, program->history_specs[i].name, out_required, err)) {
                return false;
            }
        }
    }
    return true;
}

static void cxpr_model_slot_ref_free(cxpr_model_slot_ref* ref) {
    if (!ref) return;
    free(ref->name);
    ref->name = NULL;
    ref->hash = 0u;
    ref->slot = 0u;
    ref->result_kind = CXPR_IR_VIEW_RESULT_UNKNOWN;
}

void cxpr_model_fused_program_clear(cxpr_model_program* program) {
    if (!program) return;
    cxpr_ir_program_reset(&program->fused_ir);
    for (size_t i = 0; i < program->fused_slot_count; ++i) {
        free(program->fused_slot_names[i]);
    }
    free(program->fused_slot_names);
    free(program->fused_slot_hashes);
    for (size_t i = 0; i < program->fused_input_count; ++i) {
        cxpr_model_slot_ref_free(&program->fused_inputs[i]);
    }
    free(program->fused_inputs);
    for (size_t i = 0; i < program->fused_export_count; ++i) {
        cxpr_model_slot_ref_free(&program->fused_exports[i]);
    }
    free(program->fused_exports);
    for (size_t i = 0; i < program->fused_output_count; ++i) {
        cxpr_model_slot_ref_free(&program->fused_outputs[i]);
    }
    free(program->fused_outputs);
    free(program->fused_commits);
    program->has_fused_ir = false;
    program->has_fused_layout = false;
    program->fused_disabled_opcode = NULL;
    program->fused_slot_names = NULL;
    program->fused_slot_hashes = NULL;
    program->fused_slot_count = 0u;
    program->fused_inputs = NULL;
    program->fused_input_count = 0u;
    program->fused_exports = NULL;
    program->fused_export_count = 0u;
    program->fused_outputs = NULL;
    program->fused_output_count = 0u;
    program->fused_commits = NULL;
    program->fused_commit_count = 0u;
}

bool cxpr_model_names_match(const char* a, const char* b) {
    return a && b && strcmp(a, b) == 0;
}


static bool cxpr_model_history_spec_add(cxpr_model_history_spec** specs,
                                        size_t* count,
                                        const char* name,
                                        const cxpr_ast* target,
                                        size_t depth) {
    cxpr_model_history_spec* grown;
    if (!specs || !count || !name || depth == 0u) return true;
    for (size_t i = 0; i < *count; ++i) {
        if (cxpr_model_names_match((*specs)[i].name, name)) {
            if ((*specs)[i].depth < depth) (*specs)[i].depth = depth;
            if (!(*specs)[i].target && target) {
                (*specs)[i].target = cxpr_ast_clone(target);
                if (!(*specs)[i].target) return false;
            }
            return true;
        }
    }
    grown = (cxpr_model_history_spec*)realloc(
        *specs, (*count + 1u) * sizeof(cxpr_model_history_spec));
    if (!grown) return false;
    *specs = grown;
    (*specs)[*count].name = cxpr_strdup(name);
    (*specs)[*count].target = target ? cxpr_ast_clone(target) : NULL;
    (*specs)[*count].depth = depth;
    if (!(*specs)[*count].name || (target && !(*specs)[*count].target)) return false;
    (*count)++;
    return true;
}

bool cxpr_model_lookback_target_key(const cxpr_ast* target,
                                    char** out_key,
                                    cxpr_error* err) {
    if (out_key) *out_key = NULL;
    if (!target || !out_key) return false;
    switch (cxpr_ast_type(target)) {
    case CXPR_NODE_IDENTIFIER:
    case CXPR_NODE_FIELD_ACCESS:
    case CXPR_NODE_CHAIN_ACCESS:
    case CXPR_NODE_PRODUCER_ACCESS:
        *out_key = cxpr_ast_to_string(target);
        if (!*out_key) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        return true;
    default:
        return false;
    }
}

static bool cxpr_model_collect_lookbacks_in_ast(const cxpr_ast* ast,
                                                cxpr_model_history_spec** specs,
                                                size_t* count,
                                                cxpr_error* err) {
    if (!ast) return true;
    switch (cxpr_ast_type(ast)) {
    case CXPR_NODE_LOOKBACK: {
        const cxpr_ast* target = cxpr_ast_lookback_target(ast);
        const cxpr_ast* index = cxpr_ast_lookback_index(ast);
        unsigned offset = 0u;
        if (!cxpr_lookback_literal_offset(index, &offset, err,
                                          "model lookback requires constant integer index")) {
            return false;
        }
        {
            char* key = NULL;
            bool supported = cxpr_model_lookback_target_key(target, &key, err);
            if (supported &&
                !cxpr_model_history_spec_add(specs, count, key, target, (size_t)offset)) {
                free(key);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            free(key);
        }
        return cxpr_model_collect_lookbacks_in_ast(target, specs, count, err);
    }
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_collect_lookbacks_in_ast(cxpr_ast_left(ast), specs, count, err) &&
               cxpr_model_collect_lookbacks_in_ast(cxpr_ast_right(ast), specs, count, err);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_collect_lookbacks_in_ast(cxpr_ast_operand(ast), specs, count, err);
    case CXPR_NODE_FUNCTION_CALL:
        for (size_t i = 0; i < cxpr_ast_function_argc(ast); ++i) {
            if (!cxpr_model_collect_lookbacks_in_ast(
                    cxpr_ast_function_arg(ast, i), specs, count, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_PRODUCER_ACCESS:
        for (size_t i = 0; i < cxpr_ast_producer_argc(ast); ++i) {
            if (!cxpr_model_collect_lookbacks_in_ast(
                    cxpr_ast_producer_arg(ast, i), specs, count, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_TERNARY:
        return cxpr_model_collect_lookbacks_in_ast(
                   cxpr_ast_ternary_condition(ast), specs, count, err) &&
               cxpr_model_collect_lookbacks_in_ast(
                   cxpr_ast_ternary_true_branch(ast), specs, count, err) &&
               cxpr_model_collect_lookbacks_in_ast(
                   cxpr_ast_ternary_false_branch(ast), specs, count, err);
    default:
        return true;
    }
}

static bool cxpr_model_collect_lookbacks(const cxpr_model* model,
                                         cxpr_model_history_spec** specs,
                                         size_t* count,
                                         cxpr_error* err) {
    if (!model || !specs || !count) return true;
    for (size_t i = 0; i < model->constant_count; ++i) {
        if (!cxpr_model_collect_lookbacks_in_ast(model->constants[i].expr, specs, count, err)) {
            return false;
        }
    }
    for (size_t i = 0; i < model->binding_count; ++i) {
        if (!cxpr_model_collect_lookbacks_in_ast(model->bindings[i].expr, specs, count, err)) {
            return false;
        }
    }
    for (size_t i = 0; i < model->record_function_count; ++i) {
        for (size_t f = 0; f < model->record_functions[i].field_count; ++f) {
            if (!cxpr_model_collect_lookbacks_in_ast(
                    model->record_functions[i].fields[f].expr, specs, count, err)) {
                return false;
            }
        }
    }
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

static cxpr_ir_view_result_kind cxpr_model_state_default_result_kind(
    const cxpr_model_program* program,
    const char* name) {
    if (!program || !name) return CXPR_IR_VIEW_RESULT_UNKNOWN;
    for (size_t i = 0; i < program->state_default_count; ++i) {
        if (cxpr_model_names_match(program->state_defaults[i].name, name)) {
            return program->state_defaults[i].result_kind;
        }
    }
    return CXPR_IR_VIEW_RESULT_UNKNOWN;
}

cxpr_model_program* cxpr_compile_model(const cxpr_model* model,
                                       const cxpr_registry* reg,
                                       cxpr_error* err) {
    cxpr_model_program* program;
    const cxpr_registry* compile_reg = reg;
    char** required_defaults = NULL;
    size_t required_default_count = 0u;
    size_t* order = NULL;

    if (err) *err = (cxpr_error){0};
    if (!cxpr_model_validate(model, err)) return NULL;
    if (!cxpr_model_collect_required_defaults(model, &required_defaults,
                                              &required_default_count, err)) {
        return NULL;
    }

    program = (cxpr_model_program*)calloc(1, sizeof(cxpr_model_program));
    if (!program) {
        for (size_t i = 0; i < required_default_count; ++i) free(required_defaults[i]);
        free(required_defaults);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    if (!cxpr_model_collect_lookbacks(model,
                                      &program->history_specs,
                                      &program->history_spec_count,
                                      err)) {
        for (size_t i = 0; i < required_default_count; ++i) free(required_defaults[i]);
        free(required_defaults);
        cxpr_model_program_free(program);
        return NULL;
    }

    if (model->function_count > 0 || model->record_function_count > 0u ||
        (!reg && required_default_count > 0u) ||
        program->history_spec_count > 0u) {
        if (reg && program->history_spec_count > 0u) {
            for (size_t i = 0; i < required_default_count; ++i) free(required_defaults[i]);
            free(required_defaults);
            cxpr_model_program_free(program);
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "model lookback with external registry is not supported yet",
                                 0, 0);
            return NULL;
        }
        program->registry = cxpr_registry_new();
        if (!program->registry) {
            for (size_t i = 0; i < required_default_count; ++i) free(required_defaults[i]);
            free(required_defaults);
            cxpr_model_program_free(program);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        if (program->history_spec_count > 0u) {
            cxpr_registry_set_lookback_resolver(
                program->registry, cxpr_model_lookback_resolver, NULL, NULL);
        }
        for (size_t i = 0; i < required_default_count; ++i) {
            if (!cxpr_register_default_named(program->registry, required_defaults[i])) {
                if (err) {
                    err->code = CXPR_ERR_UNKNOWN_FUNCTION;
                    err->message = "Unknown function";
                }
                for (size_t j = 0; j < required_default_count; ++j) free(required_defaults[j]);
                free(required_defaults);
                cxpr_model_program_free(program);
                return NULL;
            }
        }
        for (size_t i = 0; i < model->function_count; ++i) {
            cxpr_error fn_err = cxpr_registry_define_fn(program->registry, model->functions[i]);
            if (fn_err.code != CXPR_OK) {
                if (err) *err = fn_err;
                for (size_t j = 0; j < required_default_count; ++j) free(required_defaults[j]);
                free(required_defaults);
                cxpr_model_program_free(program);
                return NULL;
            }
        }
        for (size_t i = 0; i < model->record_function_count; ++i) {
            const char** field_names;
            const cxpr_ast** field_bodies;
            cxpr_error fn_err;
            field_names = (const char**)calloc(model->record_functions[i].field_count,
                                               sizeof(char*));
            field_bodies = (const cxpr_ast**)calloc(model->record_functions[i].field_count,
                                                    sizeof(cxpr_ast*));
            if (!field_names || !field_bodies) {
                free(field_names);
                free(field_bodies);
                for (size_t j = 0; j < required_default_count; ++j) free(required_defaults[j]);
                free(required_defaults);
                cxpr_model_program_free(program);
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
                (const cxpr_ast* const*)field_bodies,
                model->record_functions[i].field_count);
            free(field_names);
            free(field_bodies);
            if (fn_err.code != CXPR_OK) {
                if (err) *err = fn_err;
                for (size_t j = 0; j < required_default_count; ++j) free(required_defaults[j]);
                free(required_defaults);
                cxpr_model_program_free(program);
                return NULL;
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
            cxpr_model_program_free(program);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        program->constant_count = model->constant_count;
        for (size_t i = 0; i < model->constant_count; ++i) {
            program->constants[i].name = cxpr_strdup(model->constants[i].name);
            program->constants[i].name_hash = cxpr_hash_string(model->constants[i].name);
            program->constants[i].program = cxpr_compile(model->constants[i].expr, compile_reg, err);
            program->constants[i].result_kind =
                cxpr_ir_view_program_result_kind(program->constants[i].program);
            if (!program->constants[i].name || !program->constants[i].program) {
                cxpr_model_program_free(program);
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
                cxpr_model_program_free(program);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
            program->state_default_count = state_count;
            for (size_t i = 0, out_i = 0; i < model->binding_count; ++i) {
                if (model->bindings[i].kind != CXPR_MODEL_BINDING_STATE) continue;
                program->state_defaults[out_i].kind = model->bindings[i].kind;
                program->state_defaults[out_i].name = cxpr_strdup(model->bindings[i].name);
                program->state_defaults[out_i].name_hash = cxpr_hash_string(model->bindings[i].name);
                program->state_defaults[out_i].program =
                    cxpr_compile(model->bindings[i].expr, compile_reg, err);
                program->state_defaults[out_i].result_kind =
                    cxpr_ir_view_program_result_kind(program->state_defaults[out_i].program);
                if (!program->state_defaults[out_i].name ||
                    !program->state_defaults[out_i].program) {
                    cxpr_model_program_free(program);
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
            cxpr_model_program_free(program);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        if (!cxpr_model_executable_eval_order(model, order, executable_count, err)) {
            free(order);
            cxpr_model_program_free(program);
            return NULL;
        }
        program->binding_count = executable_count;
        for (size_t out_i = 0; out_i < executable_count; ++out_i) {
            size_t src_i = order[out_i];
            program->bindings[out_i].kind = model->bindings[src_i].kind;
            program->bindings[out_i].name = cxpr_strdup(model->bindings[src_i].name);
            program->bindings[out_i].name_hash = cxpr_hash_string(model->bindings[src_i].name);
            program->bindings[out_i].program = cxpr_compile(model->bindings[src_i].expr, compile_reg, err);
            program->bindings[out_i].result_kind =
                cxpr_ir_view_program_result_kind(program->bindings[out_i].program);
            if (program->bindings[out_i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) {
                program->bindings[out_i].result_kind = cxpr_model_state_default_result_kind(
                    program, program->bindings[out_i].name);
            }
            if (!program->bindings[out_i].name || !program->bindings[out_i].program) {
                free(order);
                cxpr_model_program_free(program);
                if (err && err->code == CXPR_OK) {
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                }
                return NULL;
            }
        }
        free(order);
    }

compile_outputs:
    if (model->output_count > 0) {
        program->outputs = (char**)calloc(model->output_count, sizeof(char*));
        if (!program->outputs) {
            cxpr_model_program_free(program);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        program->output_count = model->output_count;
        for (size_t i = 0; i < model->output_count; ++i) {
            program->outputs[i] = cxpr_strdup(model->outputs[i]);
            if (!program->outputs[i]) {
                cxpr_model_program_free(program);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
        }
    }

    if (!cxpr_model_try_compile_fused_ir(program, model, compile_reg, err)) {
        cxpr_model_program_free(program);
        return NULL;
    }

    if (err) err->code = CXPR_OK;
    return program;
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
        if (!cxpr_eval_program(program->constants[i].program, ctx, eval_reg, &value, err)) {
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
            if (!cxpr_eval_program_number(
                    program->bindings[i].program, ctx, eval_reg, &value, err)) {
                return false;
            }
            cxpr_model_context_set_compiled_number(ctx, &program->bindings[i], value);
        } else if (program->bindings[i].result_kind == CXPR_IR_VIEW_RESULT_BOOL) {
            bool value = false;
            if (!cxpr_eval_program_bool(
                    program->bindings[i].program, ctx, eval_reg, &value, err)) {
                return false;
            }
            cxpr_model_context_set_compiled_bool(ctx, &program->bindings[i], value);
        } else {
            cxpr_value value = {0};
            if (!cxpr_eval_program(program->bindings[i].program, ctx, eval_reg, &value, err)) {
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

size_t cxpr_model_program_c_slot_count(const cxpr_model_program* program) {
    size_t count;
    if (!program || !program->has_fused_layout) return 0u;
    count = program->state_default_count;
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        count += 2u + program->history_specs[i].depth;
    }
    return count;
}

size_t cxpr_model_program_c_param_count(const cxpr_model_program* program) {
    return program ? program->constant_count : 0u;
}

const char* cxpr_model_program_c_param_name(const cxpr_model_program* program, size_t index) {
    return program && index < program->constant_count ? program->constants[index].name : NULL;
}
