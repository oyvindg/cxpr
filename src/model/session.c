#include "ir/exec/internal.h"
#include "lookback.h"
#include "model/internal.h"
#include "registry/internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static CXPR_THREAD_LOCAL cxpr_model_session* g_model_active_session = NULL;

static bool cxpr_model_ast_uses_defined_record_producer(const cxpr_ast* ast,
                                                        const cxpr_registry* reg) {
    if (!ast) return false;
    switch (cxpr_ast_type(ast)) {
    case CXPR_NODE_PRODUCER_ACCESS: {
        cxpr_func_entry* entry = cxpr_registry_find(reg, cxpr_ast_producer_name(ast));
        if (entry && entry->defined_return_field_count > 0u) return true;
        for (size_t i = 0u; i < cxpr_ast_producer_argc(ast); ++i) {
            if (cxpr_model_ast_uses_defined_record_producer(
                    cxpr_ast_producer_arg(ast, i), reg)) {
                return true;
            }
        }
        return false;
    }
    case CXPR_NODE_LOOKBACK:
        return cxpr_model_ast_uses_defined_record_producer(
                   cxpr_ast_lookback_target(ast), reg) ||
               cxpr_model_ast_uses_defined_record_producer(
                   cxpr_ast_lookback_index(ast), reg);
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_ast_uses_defined_record_producer(cxpr_ast_left(ast), reg) ||
               cxpr_model_ast_uses_defined_record_producer(cxpr_ast_right(ast), reg);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_ast_uses_defined_record_producer(cxpr_ast_operand(ast), reg);
    case CXPR_NODE_TERNARY:
        return cxpr_model_ast_uses_defined_record_producer(
                   cxpr_ast_ternary_condition(ast), reg) ||
               cxpr_model_ast_uses_defined_record_producer(
                   cxpr_ast_ternary_true_branch(ast), reg) ||
               cxpr_model_ast_uses_defined_record_producer(
                   cxpr_ast_ternary_false_branch(ast), reg);
    case CXPR_NODE_FUNCTION_CALL:
        for (size_t i = 0u; i < cxpr_ast_function_argc(ast); ++i) {
            if (cxpr_model_ast_uses_defined_record_producer(
                    cxpr_ast_function_arg(ast, i), reg)) {
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

static bool cxpr_model_binding_prefers_ast_eval(const cxpr_model_compiled_binding* binding,
                                                const cxpr_registry* reg) {
    return binding &&
           binding->program &&
           binding->program->ast &&
           cxpr_model_ast_uses_defined_record_producer(binding->program->ast, reg);
}

static void cxpr_model_history_entry_free(cxpr_model_history_entry* entry) {
    if (!entry) return;
    free(entry->name);
    for (size_t i = 0; i < entry->capacity; ++i) {
        cxpr_value_free(&entry->values[i]);
    }
    free(entry->values);
    entry->name = NULL;
    entry->values = NULL;
    entry->capacity = 0u;
    entry->count = 0u;
    entry->next = 0u;
}
static cxpr_value cxpr_model_context_get_history_value(const cxpr_context* ctx,
                                                       const char* key,
                                                       bool* found) {
    const char* dot;
    char root[256];
    if (found) *found = false;
    if (!ctx || !key) return cxpr_num(0.0);
    {
        cxpr_value value = cxpr_context_get_typed(ctx, key, found);
        if (found && *found) return value;
    }
    dot = strchr(key, '.');
    if (dot && !strchr(dot + 1, '.')) {
        size_t root_len = (size_t)(dot - key);
        if (root_len > 0u && root_len < sizeof(root)) {
            memcpy(root, key, root_len);
            root[root_len] = '\0';
            return cxpr_context_get_field(ctx, root, dot + 1, found);
        }
    }
    return cxpr_num(0.0);
}

static cxpr_model_history_entry*
cxpr_model_session_find_history(cxpr_model_session* session, const char* name) {
    if (!session || !name) return NULL;
    for (size_t i = 0; i < session->history_count; ++i) {
        if (cxpr_model_names_match(session->histories[i].name, name)) {
            return &session->histories[i];
        }
    }
    return NULL;
}

static bool cxpr_model_session_history_push(cxpr_model_session* session,
                                            const char* name,
                                            const cxpr_value* value) {
    cxpr_model_history_entry* entry = cxpr_model_session_find_history(session, name);
    cxpr_value clone;
    if (!entry || !value || entry->capacity == 0u) return true;
    clone = cxpr_value_clone(value);
    cxpr_value_free(&entry->values[entry->next]);
    entry->values[entry->next] = clone;
    entry->next = (entry->next + 1u) % entry->capacity;
    if (entry->count < entry->capacity) entry->count++;
    return true;
}

static bool cxpr_model_session_capture_history(const cxpr_model_program* program,
                                               cxpr_model_session* session,
                                               const cxpr_registry* reg,
                                               cxpr_error* err) {
    if (!session || !session->ctx) return false;
    for (size_t i = 0; i < session->history_count; ++i) {
        bool found = false;
        bool owns_value = false;
        cxpr_value value = cxpr_model_context_get_history_value(
            session->ctx, session->histories[i].name, &found);
        if (!found && program && i < program->history_spec_count &&
            program->history_specs[i].target) {
            if (!cxpr_eval_ast(program->history_specs[i].target,
                               session->ctx,
                               reg,
                               &value,
                               err)) {
                return false;
            }
            found = true;
            owns_value = true;
        }
        if (found) {
            if (!cxpr_model_session_history_push(session, session->histories[i].name, &value)) {
                if (owns_value) cxpr_value_free(&value);
                return false;
            }
            if (owns_value) cxpr_value_free(&value);
        }
    }
    return true;
}

bool cxpr_model_lookback_resolver(const cxpr_ast* target,
                                  const cxpr_ast* index,
                                  const cxpr_context* ctx,
                                  const cxpr_registry* reg,
                                  void* userdata,
                                  cxpr_value* out,
                                  cxpr_error* err) {
    cxpr_model_session* session = g_model_active_session;
    cxpr_model_history_entry* entry;
    char* key = NULL;
    unsigned offset = 0u;
    size_t slot;
    bool found = false;

    (void)reg;
    (void)userdata;
    if (!target || !index || !out) return false;
    if (!cxpr_lookback_literal_offset(index, &offset, err,
                                      "model lookback requires constant integer index")) {
        return false;
    }
    if (!cxpr_model_lookback_target_key(target, &key, err)) return false;
    if (offset == 0u) {
        *out = cxpr_model_context_get_history_value(ctx, key, &found);
        free(key);
        return found;
    }
    entry = cxpr_model_session_find_history(session, key);
    free(key);
    if (!entry || entry->capacity == 0u || entry->count < (size_t)offset) {
        *out = cxpr_num(NAN);
        return true;
    }
    slot = (entry->next + entry->capacity - (size_t)offset) % entry->capacity;
    *out = cxpr_value_clone(&entry->values[slot]);
    return true;
}

static void cxpr_model_session_refresh_outputs(const cxpr_model_program* program,
                                               cxpr_model_session* session) {
    if (!program || !session) return;
    for (size_t i = 0; i < session->output_count; ++i) {
        bool found = false;
        bool numeric_found = false;
        double numeric = 0.0;
        bool value = cxpr_context_get_bool(session->ctx, program->outputs[i], &found);
        if (!found) {
            numeric = cxpr_context_get(session->ctx, program->outputs[i], &numeric_found);
            if (numeric_found) value = numeric != 0.0;
            found = numeric_found;
        } else {
            numeric = value ? 1.0 : 0.0;
            numeric_found = false;
        }
        session->outputs[i].number_previous = session->outputs[i].number_current;
        session->outputs[i].has_number_previous = session->outputs[i].has_number_current;
        session->outputs[i].number_current = numeric;
        session->outputs[i].has_number_current = numeric_found;
        session->outputs[i].previous = session->outputs[i].current;
        session->outputs[i].has_previous = session->outputs[i].has_current;
        session->outputs[i].current = value;
        session->outputs[i].has_current = found;
    }
}

static void cxpr_model_output_state_set_number(cxpr_model_output_state* state,
                                               double value) {
    if (!state) return;
    state->number_previous = state->number_current;
    state->has_number_previous = state->has_number_current;
    state->number_current = value;
    state->has_number_current = true;
    state->previous = state->current;
    state->has_previous = state->has_current;
    state->current = value != 0.0;
    state->has_current = true;
}

static void cxpr_model_output_state_set_bool(cxpr_model_output_state* state,
                                             bool value) {
    if (!state) return;
    state->number_previous = state->number_current;
    state->has_number_previous = state->has_number_current;
    state->number_current = value ? 1.0 : 0.0;
    state->has_number_current = false;
    state->previous = state->current;
    state->has_previous = state->has_current;
    state->current = value;
    state->has_current = true;
}

static bool cxpr_model_session_refresh_outputs_fused(
    const cxpr_model_program* program,
    cxpr_model_session* session) {
    if (!program || !session || !session->fused_slots ||
        program->fused_output_count != session->output_count) {
        return false;
    }
    for (size_t i = 0; i < session->output_count; ++i) {
        const cxpr_model_slot_ref* output = &program->fused_outputs[i];
        double value = session->fused_slots[output->slot];
        if (output->result_kind == CXPR_IR_VIEW_RESULT_BOOL) {
            cxpr_model_output_state_set_bool(&session->outputs[i], value != 0.0);
        } else {
            cxpr_model_output_state_set_number(&session->outputs[i], value);
        }
    }
    return true;
}

cxpr_model_session* cxpr_model_session_new(const cxpr_model_program* program,
                                           const cxpr_registry* reg,
                                           cxpr_error* err) {
    const cxpr_registry* eval_reg;
    cxpr_model_session* session;

    if (err) *err = (cxpr_error){0};
    if (!program) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "NULL model program", 0, 0);
        return NULL;
    }

    session = (cxpr_model_session*)calloc(1, sizeof(cxpr_model_session));
    if (!session) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    session->ctx = cxpr_context_new();
    if (!session->ctx) {
        cxpr_model_session_free(session);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }

    if (program->output_count > 0u) {
        session->outputs =
            (cxpr_model_output_state*)calloc(program->output_count,
                                             sizeof(cxpr_model_output_state));
        if (!session->outputs) {
            cxpr_model_session_free(session);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        session->output_count = program->output_count;
        for (size_t i = 0; i < program->output_count; ++i) {
            session->outputs[i].name = cxpr_strdup(program->outputs[i]);
            if (!session->outputs[i].name) {
                cxpr_model_session_free(session);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
        }
    }
    if (program->history_spec_count > 0u) {
        session->histories = (cxpr_model_history_entry*)calloc(
            program->history_spec_count, sizeof(cxpr_model_history_entry));
        if (!session->histories) {
            cxpr_model_session_free(session);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        session->history_count = program->history_spec_count;
        for (size_t i = 0; i < program->history_spec_count; ++i) {
            session->histories[i].name = cxpr_strdup(program->history_specs[i].name);
            session->histories[i].capacity = program->history_specs[i].depth;
            session->histories[i].values = (cxpr_value*)calloc(
                session->histories[i].capacity, sizeof(cxpr_value));
            if (!session->histories[i].name ||
                (session->histories[i].capacity > 0u && !session->histories[i].values)) {
                cxpr_model_session_free(session);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
        }
    }
    if (program->binding_count > 0u) {
        session->pending_values =
            (cxpr_value*)calloc(program->binding_count, sizeof(cxpr_value));
        session->pending_binding_indices =
            (size_t*)calloc(program->binding_count, sizeof(size_t));
        if (!session->pending_values || !session->pending_binding_indices) {
            cxpr_model_session_free(session);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        session->pending_capacity = program->binding_count;
    }
    if (program->has_fused_ir && program->fused_slot_count > 0u) {
        session->fused_slots = (double*)calloc(program->fused_slot_count, sizeof(double));
        if (!session->fused_slots) {
            cxpr_model_session_free(session);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        session->fused_slot_count = program->fused_slot_count;
        if (program->fused_input_count > 0u) {
            session->fused_input_slots =
                (cxpr_context_slot*)calloc(program->fused_input_count,
                                           sizeof(cxpr_context_slot));
            session->fused_input_slot_bound =
                (bool*)calloc(program->fused_input_count, sizeof(bool));
            if (!session->fused_input_slots || !session->fused_input_slot_bound) {
                cxpr_model_session_free(session);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
            session->fused_input_slot_count = program->fused_input_count;
        }
        if (program->fused_export_count > 0u) {
            session->fused_export_slots =
                (cxpr_context_slot*)calloc(program->fused_export_count,
                                           sizeof(cxpr_context_slot));
            session->fused_export_slot_bound =
                (bool*)calloc(program->fused_export_count, sizeof(bool));
            if (!session->fused_export_slots || !session->fused_export_slot_bound) {
                cxpr_model_session_free(session);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
            session->fused_export_slot_count = program->fused_export_count;
        }
        if (program->fused_commit_count > 0u) {
            session->fused_commit_slots =
                (cxpr_context_slot*)calloc(program->fused_commit_count,
                                           sizeof(cxpr_context_slot));
            session->fused_commit_slot_bound =
                (bool*)calloc(program->fused_commit_count, sizeof(bool));
            if (!session->fused_commit_slots || !session->fused_commit_slot_bound) {
                cxpr_model_session_free(session);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
            session->fused_commit_slot_count = program->fused_commit_count;
        }
    }

    eval_reg = program->registry ? program->registry : reg;
    if (!cxpr_model_program_seed_defaults(program, session->ctx, eval_reg, err)) {
        cxpr_model_session_free(session);
        return NULL;
    }
    for (size_t i = 0; i < program->state_default_count; ++i) {
        if (program->state_defaults[i].result_kind == CXPR_IR_VIEW_RESULT_NUMBER) {
            double value = 0.0;
            if (cxpr_model_binding_prefers_ast_eval(&program->state_defaults[i], eval_reg)
                    ? !cxpr_eval_ast_number(program->state_defaults[i].program->ast,
                                            session->ctx, eval_reg, &value, err)
                    : !cxpr_eval_program_number(program->state_defaults[i].program,
                                                session->ctx, eval_reg, &value, err)) {
                cxpr_model_session_free(session);
                return NULL;
            }
            cxpr_model_context_set_compiled_number(
                session->ctx, &program->state_defaults[i], value);
        } else if (program->state_defaults[i].result_kind == CXPR_IR_VIEW_RESULT_BOOL) {
            bool value = false;
            if (cxpr_model_binding_prefers_ast_eval(&program->state_defaults[i], eval_reg)
                    ? !cxpr_eval_ast_bool(program->state_defaults[i].program->ast,
                                          session->ctx, eval_reg, &value, err)
                    : !cxpr_eval_program_bool(program->state_defaults[i].program,
                                              session->ctx, eval_reg, &value, err)) {
                cxpr_model_session_free(session);
                return NULL;
            }
            cxpr_model_context_set_compiled_bool(
                session->ctx, &program->state_defaults[i], value);
        } else {
            cxpr_value value = {0};
            if (cxpr_model_binding_prefers_ast_eval(&program->state_defaults[i], eval_reg)
                    ? !cxpr_eval_ast(program->state_defaults[i].program->ast,
                                     session->ctx, eval_reg, &value, err)
                    : !cxpr_eval_program(program->state_defaults[i].program,
                                         session->ctx, eval_reg, &value, err)) {
                cxpr_model_session_free(session);
                return NULL;
            }
            cxpr_model_context_set_compiled_typed(
                session->ctx, &program->state_defaults[i], &value);
            cxpr_value_free(&value);
        }
    }
    if (program->has_fused_ir && session->fused_slots) {
        for (size_t i = 0; i < program->fused_slot_count; ++i) {
            bool found = false;
            double value = cxpr_context_get(session->ctx, program->fused_slot_names[i], &found);
            if (found) session->fused_slots[i] = value;
        }
    }

    if (err) err->code = CXPR_OK;
    return session;
}

void cxpr_model_session_free(cxpr_model_session* session) {
    if (!session) return;
    cxpr_context_free(session->ctx);
    for (size_t i = 0; i < session->output_count; ++i) free(session->outputs[i].name);
    free(session->outputs);
    for (size_t i = 0; i < session->history_count; ++i) {
        cxpr_model_history_entry_free(&session->histories[i]);
    }
    free(session->histories);
    free(session->fused_slots);
    free(session->fused_input_slots);
    free(session->fused_input_slot_bound);
    free(session->fused_export_slots);
    free(session->fused_export_slot_bound);
    free(session->fused_commit_slots);
    free(session->fused_commit_slot_bound);
    for (size_t i = 0; i < session->pending_capacity; ++i) {
        cxpr_value_free(&session->pending_values[i]);
    }
    free(session->pending_values);
    free(session->pending_binding_indices);
    free(session);
}

cxpr_context* cxpr_model_session_context(cxpr_model_session* session) {
    return session ? session->ctx : NULL;
}

static bool cxpr_model_session_tick_fused(const cxpr_model_program* program,
                                          cxpr_model_session* session,
                                          const cxpr_registry* eval_reg,
                                          bool materialize_context,
                                          cxpr_error* err) {
    double ignored;

    if (!program->has_fused_ir || !session->fused_slots) return false;
    for (size_t i = 0; i < program->fused_input_count; ++i) {
        bool found = false;
        double value;
        if (i < session->fused_input_slot_count &&
            session->fused_input_slot_bound[i] &&
            cxpr_context_slot_valid(session->ctx, &session->fused_input_slots[i])) {
            value = cxpr_context_slot_get(&session->fused_input_slots[i]);
            found = true;
        } else {
            value = cxpr_context_get(
                session->ctx, program->fused_inputs[i].name, &found);
            if (found && i < session->fused_input_slot_count &&
                cxpr_context_slot_bind(session->ctx,
                                       program->fused_inputs[i].name,
                                       &session->fused_input_slots[i])) {
                session->fused_input_slot_bound[i] = true;
            }
        }
        if (!found) {
            cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                 "Missing model input", 0, 0);
            return false;
        }
        session->fused_slots[program->fused_inputs[i].slot] = value;
    }

    ignored = cxpr_ir_exec_scalar_fast(&program->fused_ir,
                                       session->ctx,
                                       eval_reg,
                                       session->fused_slots,
                                       session->fused_slot_count,
                                       err);
    (void)ignored;
    if (err && err->code != CXPR_OK) return false;

    for (size_t i = 0; i < program->fused_commit_count; ++i) {
        session->fused_slots[program->fused_commits[i].state_slot] =
            session->fused_slots[program->fused_commits[i].update_slot];
    }

    if (materialize_context) {
        for (size_t i = 0; i < program->fused_export_count; ++i) {
            double value = session->fused_slots[program->fused_exports[i].slot];
            if (program->fused_exports[i].result_kind == CXPR_IR_VIEW_RESULT_BOOL) {
                cxpr_context_set_bool(session->ctx, program->fused_exports[i].name, value != 0.0);
            } else if (i < session->fused_export_slot_count &&
                       session->fused_export_slot_bound[i] &&
                       cxpr_context_slot_valid(session->ctx, &session->fused_export_slots[i])) {
                cxpr_context_slot_set(&session->fused_export_slots[i], value);
            } else {
                cxpr_context_set_prehashed(session->ctx,
                                           program->fused_exports[i].name,
                                           program->fused_exports[i].hash,
                                           value);
                if (i < session->fused_export_slot_count &&
                    cxpr_context_slot_bind(session->ctx,
                                           program->fused_exports[i].name,
                                           &session->fused_export_slots[i])) {
                    session->fused_export_slot_bound[i] = true;
                }
            }
        }
        for (size_t i = 0; i < program->fused_commit_count; ++i) {
            size_t slot = program->fused_commits[i].state_slot;
            double value = session->fused_slots[slot];
            if (i < session->fused_commit_slot_count &&
                session->fused_commit_slot_bound[i] &&
                cxpr_context_slot_valid(session->ctx, &session->fused_commit_slots[i])) {
                cxpr_context_slot_set(&session->fused_commit_slots[i], value);
            } else {
                cxpr_context_set_prehashed(session->ctx,
                                           program->fused_slot_names[slot],
                                           program->fused_slot_hashes[slot],
                                           value);
                if (i < session->fused_commit_slot_count &&
                    cxpr_context_slot_bind(session->ctx,
                                           program->fused_slot_names[slot],
                                           &session->fused_commit_slots[i])) {
                    session->fused_commit_slot_bound[i] = true;
                }
            }
        }
    }
    if (!cxpr_model_session_refresh_outputs_fused(program, session)) {
        cxpr_model_session_refresh_outputs(program, session);
    }
    if (materialize_context &&
        !cxpr_model_session_capture_history(program, session, eval_reg, err)) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    if (err) err->code = CXPR_OK;
    return true;
}

bool cxpr_model_session_tick(const cxpr_model_program* program,
                             cxpr_model_session* session,
                             const cxpr_registry* reg,
                             cxpr_error* err) {
    const cxpr_registry* eval_reg;
    size_t pending_count = 0u;
    cxpr_model_session* previous_active_session;

    if (err) *err = (cxpr_error){0};
    if (!program || !session || !session->ctx) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid model session tick arguments", 0, 0);
        return false;
    }
    eval_reg = program->registry ? program->registry : reg;
    previous_active_session = g_model_active_session;
    g_model_active_session = session;
    if (program->has_fused_ir && session->fused_slots) {
        bool ok = cxpr_model_session_tick_fused(program, session, eval_reg, true, err);
        g_model_active_session = previous_active_session;
        return ok;
    }

    for (size_t i = 0; i < program->binding_count; ++i) {
        cxpr_value value = {0};
        if (program->bindings[i].result_kind == CXPR_IR_VIEW_RESULT_NUMBER) {
            double number = 0.0;
            if (cxpr_model_binding_prefers_ast_eval(&program->bindings[i], eval_reg)
                    ? !cxpr_eval_ast_number(program->bindings[i].program->ast,
                                            session->ctx, eval_reg, &number, err)
                    : !cxpr_eval_program_number(program->bindings[i].program, session->ctx,
                                                eval_reg, &number, err)) {
                for (size_t j = 0; j < pending_count; ++j) {
                    cxpr_value_free(&session->pending_values[j]);
                }
                g_model_active_session = previous_active_session;
                return false;
            }
            value = cxpr_num(number);
        } else if (program->bindings[i].result_kind == CXPR_IR_VIEW_RESULT_BOOL) {
            bool boolean = false;
            if (cxpr_model_binding_prefers_ast_eval(&program->bindings[i], eval_reg)
                    ? !cxpr_eval_ast_bool(program->bindings[i].program->ast,
                                          session->ctx, eval_reg, &boolean, err)
                    : !cxpr_eval_program_bool(program->bindings[i].program, session->ctx,
                                              eval_reg, &boolean, err)) {
                for (size_t j = 0; j < pending_count; ++j) {
                    cxpr_value_free(&session->pending_values[j]);
                }
                g_model_active_session = previous_active_session;
                return false;
            }
            value = cxpr_bool(boolean);
        } else if (cxpr_model_binding_prefers_ast_eval(&program->bindings[i], eval_reg)
                       ? !cxpr_eval_ast(program->bindings[i].program->ast,
                                        session->ctx, eval_reg, &value, err)
                       : !cxpr_eval_program(program->bindings[i].program, session->ctx,
                                            eval_reg, &value, err)) {
            for (size_t j = 0; j < pending_count; ++j) {
                cxpr_value_free(&session->pending_values[j]);
            }
            g_model_active_session = previous_active_session;
            return false;
        }
        if (program->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) {
            if (pending_count >= session->pending_capacity) {
                cxpr_value_free(&value);
                for (size_t j = 0; j < pending_count; ++j) {
                    cxpr_value_free(&session->pending_values[j]);
                }
                g_model_active_session = previous_active_session;
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            session->pending_binding_indices[pending_count] = i;
            session->pending_values[pending_count] = value;
            pending_count++;
        } else {
            if (value.type == CXPR_VALUE_NUMBER) {
                cxpr_model_context_set_compiled_number(
                    session->ctx, &program->bindings[i], value.d);
            } else if (value.type == CXPR_VALUE_BOOL) {
                cxpr_model_context_set_compiled_bool(
                    session->ctx, &program->bindings[i], value.b);
            } else {
                cxpr_model_context_set_compiled_typed(
                    session->ctx, &program->bindings[i], &value);
            }
            cxpr_value_free(&value);
        }
    }

    for (size_t i = 0; i < pending_count; ++i) {
        size_t binding_index = session->pending_binding_indices[i];
        if (session->pending_values[i].type == CXPR_VALUE_NUMBER) {
            cxpr_model_context_set_compiled_number(
                session->ctx,
                &program->bindings[binding_index],
                session->pending_values[i].d);
        } else if (session->pending_values[i].type == CXPR_VALUE_BOOL) {
            cxpr_model_context_set_compiled_bool(
                session->ctx,
                &program->bindings[binding_index],
                session->pending_values[i].b);
        } else {
            cxpr_model_context_set_compiled_typed(
                session->ctx,
                &program->bindings[binding_index],
                &session->pending_values[i]);
        }
        cxpr_value_free(&session->pending_values[i]);
    }

    cxpr_model_session_refresh_outputs(program, session);
    if (!cxpr_model_session_capture_history(program, session, eval_reg, err)) {
        g_model_active_session = previous_active_session;
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    g_model_active_session = previous_active_session;
    if (err) err->code = CXPR_OK;
    return true;
}

bool cxpr_model_session_tick_fast(const cxpr_model_program* program,
                                  cxpr_model_session* session,
                                  const cxpr_registry* reg,
                                  cxpr_error* err) {
    const cxpr_registry* eval_reg;
    cxpr_model_session* previous_active_session;

    if (err) *err = (cxpr_error){0};
    if (!program || !session || !session->ctx) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid model session tick arguments", 0, 0);
        return false;
    }
    if (!program->has_fused_ir || !session->fused_slots || session->history_count > 0u) {
        return cxpr_model_session_tick(program, session, reg, err);
    }
    eval_reg = program->registry ? program->registry : reg;
    previous_active_session = g_model_active_session;
    g_model_active_session = session;
    {
        bool ok = cxpr_model_session_tick_fused(program, session, eval_reg, false, err);
        g_model_active_session = previous_active_session;
        return ok;
    }
}

static const cxpr_model_output_state*
cxpr_model_session_find_output(const cxpr_model_session* session, const char* name) {
    if (!session || !name) return NULL;
    for (size_t i = 0; i < session->output_count; ++i) {
        if (cxpr_model_names_match(session->outputs[i].name, name)) return &session->outputs[i];
    }
    return NULL;
}

bool cxpr_model_session_output_bool(const cxpr_model_session* session,
                                    const char* name,
                                    bool* out_value) {
    const cxpr_model_output_state* state = cxpr_model_session_find_output(session, name);
    if (!state || !state->has_current) return false;
    if (out_value) *out_value = state->current;
    return true;
}

bool cxpr_model_session_output_number(const cxpr_model_session* session,
                                      const char* name,
                                      double* out_value) {
    const cxpr_model_output_state* state = cxpr_model_session_find_output(session, name);
    if (!state || !state->has_number_current) return false;
    if (out_value) *out_value = state->number_current;
    return true;
}

bool cxpr_model_session_output_rising(const cxpr_model_session* session, const char* name) {
    const cxpr_model_output_state* state = cxpr_model_session_find_output(session, name);
    return state && state->has_current && state->current &&
           (!state->has_previous || !state->previous);
}

bool cxpr_model_session_output_falling(const cxpr_model_session* session, const char* name) {
    const cxpr_model_output_state* state = cxpr_model_session_find_output(session, name);
    return state && state->has_current && !state->current &&
           state->has_previous && state->previous;
}

bool cxpr_model_session_output_changed(const cxpr_model_session* session, const char* name) {
    const cxpr_model_output_state* state = cxpr_model_session_find_output(session, name);
    return state && state->has_current &&
           (!state->has_previous || state->current != state->previous);
}
