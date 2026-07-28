#include "ir/exec/internal.h"
#include "eval/internal.h"
#include "lookback.h"
#include "model/internal.h"
#include "registry/internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static CXPR_THREAD_LOCAL cxpr_model_session* g_model_active_session = NULL;

cxpr_model_session* cxpr_model_active_session(void) {
    return g_model_active_session;
}
static char* cxpr_model_child_cache_key(const cxpr_expr_ast* ast) {
    size_t len;
    char* out;
    size_t pos;
    const char* name;
    size_t argc;
    if (!ast) return NULL;
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS) {
        name = cxpr_expr_ast_producer_name(ast);
        argc = cxpr_expr_ast_producer_arg_count(ast);
    } else if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL) {
        name = cxpr_expr_ast_call_name(ast);
        argc = cxpr_expr_ast_call_arg_count(ast);
    } else {
        return NULL;
    }
    if (!name) return NULL;
    len = strlen(name) + 3u;
    for (size_t i = 0u; i < argc; ++i) {
        const cxpr_expr_ast* arg_ast = cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS
                                      ? cxpr_expr_ast_producer_arg(ast, i)
                                      : cxpr_expr_ast_call_arg(ast, i);
        const char* arg_name = cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS
                                   ? cxpr_expr_ast_producer_arg_name(ast, i)
                                   : cxpr_expr_ast_call_arg_name(ast, i);
        char* arg = cxpr_expr_ast_to_string(arg_ast);
        len += arg_name ? strlen(arg_name) + 1u : 0u;
        len += arg ? strlen(arg) : 0u;
        len += 2u;
        free(arg);
    }
    out = (char*)malloc(len + 1u);
    if (!out) return NULL;
    pos = (size_t)snprintf(out, len + 1u, "%s(", name);
    for (size_t i = 0u; i < argc; ++i) {
        const cxpr_expr_ast* arg_ast = cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS
                                      ? cxpr_expr_ast_producer_arg(ast, i)
                                      : cxpr_expr_ast_call_arg(ast, i);
        const char* arg_name = cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS
                                   ? cxpr_expr_ast_producer_arg_name(ast, i)
                                   : cxpr_expr_ast_call_arg_name(ast, i);
        char* arg = cxpr_expr_ast_to_string(arg_ast);
        if (i > 0u && pos < len) out[pos++] = ',';
        if (arg_name) {
            size_t n = strlen(arg_name);
            if (pos + n < len + 1u) memcpy(out + pos, arg_name, n);
            pos += n;
            if (pos < len) out[pos++] = '=';
        }
        if (arg) {
            size_t n = strlen(arg);
            if (pos + n < len + 1u) memcpy(out + pos, arg, n);
            pos += n;
            free(arg);
        }
    }
    if (pos < len) out[pos++] = ')';
    out[pos < len + 1u ? pos : len] = '\0';
    return out;
}

static size_t cxpr_model_child_call_argc(const cxpr_expr_ast* ast) {
    if (!ast) return 0u;
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS) return cxpr_expr_ast_producer_arg_count(ast);
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL) return cxpr_expr_ast_call_arg_count(ast);
    return 0u;
}

static const cxpr_expr_ast* cxpr_model_child_call_arg(const cxpr_expr_ast* ast, size_t index) {
    if (!ast) return NULL;
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS) return cxpr_expr_ast_producer_arg(ast, index);
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL) return cxpr_expr_ast_call_arg(ast, index);
    return NULL;
}

static const char* cxpr_model_child_call_arg_name(const cxpr_expr_ast* ast, size_t index) {
    if (!ast) return NULL;
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS) return cxpr_expr_ast_producer_arg_name(ast, index);
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL) return cxpr_expr_ast_call_arg_name(ast, index);
    return NULL;
}

static const cxpr_expr_ast* cxpr_model_child_call_named_arg(const cxpr_expr_ast* ast,
                                                       const char* name) {
    if (!ast || !name) return NULL;
    for (size_t i = 0u; i < cxpr_model_child_call_argc(ast); ++i) {
        const char* arg_name = cxpr_model_child_call_arg_name(ast, i);
        if (arg_name && cxpr_model_names_match(arg_name, name)) {
            return cxpr_model_child_call_arg(ast, i);
        }
    }
    return NULL;
}

static bool cxpr_model_child_call_has_named_args(const cxpr_expr_ast* ast) {
    if (!ast) return false;
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS) return cxpr_expr_ast_producer_has_named_args(ast);
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL) return cxpr_expr_ast_call_has_named_args(ast);
    return false;
}

static bool cxpr_model_eval_ast_bool_result(const cxpr_expr_ast* ast,
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

static bool cxpr_model_session_capture_history(const cxpr_model_compiled* program,
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

static void cxpr_model_output_state_set_number(cxpr_model_output_state* state,
                                               double value);
static void cxpr_model_output_state_set_bool(cxpr_model_output_state* state,
                                             bool value);

bool cxpr_model_lookback_resolver(const cxpr_expr_ast* target,
                                  const cxpr_expr_ast* index,
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
    if (!cxpr_lookback_literal_offset(index, &offset, NULL, NULL)) {
        double dynamic_offset = 0.0;
        if (!cxpr_eval_ast_number(index, ctx, reg, &dynamic_offset, err) ||
            !isfinite(dynamic_offset) || dynamic_offset < 0.0 ||
            floor(dynamic_offset) != dynamic_offset || dynamic_offset > 512.0) {
            if (err && err->code == CXPR_OK) {
                err->code = CXPR_ERR_SYNTAX;
                err->message = "Dynamic lookback index must be a non-negative integer";
            }
            return false;
        }
        offset = (unsigned)dynamic_offset;
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

static void cxpr_model_session_refresh_outputs(const cxpr_model_compiled* program,
                                               cxpr_model_session* session) {
    if (!program || !session) return;
    for (size_t i = 0; i < session->output_count; ++i) {
        bool found = false;
        bool numeric_found = false;
        double numeric = 0.0;
        bool value = cxpr_context_get_bool(session->ctx, program->outputs[i], &found);
        for (size_t j = 0u; j < session->pending_count; ++j) {
            size_t binding_index = session->pending_binding_indices[j];
            if (binding_index >= program->binding_count ||
                program->bindings[binding_index].kind != CXPR_MODEL_BINDING_STATE_UPDATE ||
                !cxpr_model_names_match(program->bindings[binding_index].name,
                                        program->outputs[i])) {
                continue;
            }
            if (session->pending_values[j].type == CXPR_VALUE_NUMBER) {
                cxpr_model_output_state_set_number(
                    &session->outputs[i], session->pending_values[j].d);
                goto next_output;
            }
            if (session->pending_values[j].type == CXPR_VALUE_BOOL) {
                cxpr_model_output_state_set_bool(
                    &session->outputs[i], session->pending_values[j].b);
                goto next_output;
            }
        }
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
next_output:
        ;
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
    const cxpr_model_compiled* program,
    cxpr_model_session* session) {
    if (!program || !session || !session->fused_slots ||
        program->fused_output_count != session->output_count) {
        return false;
    }
    for (size_t i = 0; i < session->output_count; ++i) {
        const cxpr_model_slot_ref* output = &program->fused_outputs[i];
        double value = session->fused_slots[output->slot];
        for (size_t j = 0u; j < program->fused_commit_count; ++j) {
            if (program->fused_commits[j].state_slot == output->slot &&
                j < session->fused_pending_count &&
                session->fused_pending_bound[j]) {
                value = session->fused_pending_values[j];
                break;
            }
        }
        if (output->result_kind == CXPR_MODEL_RESULT_BOOL) {
            cxpr_model_output_state_set_bool(&session->outputs[i], value != 0.0);
        } else {
            cxpr_model_output_state_set_number(&session->outputs[i], value);
        }
    }
    return true;
}

cxpr_model_session* cxpr_model_session_new(const cxpr_model_compiled* program,
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
    session->program = program;
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
            session->fused_pending_values =
                (double*)calloc(program->fused_commit_count, sizeof(double));
            session->fused_pending_bound =
                (bool*)calloc(program->fused_commit_count, sizeof(bool));
            if (!session->fused_commit_slots || !session->fused_commit_slot_bound ||
                !session->fused_pending_values || !session->fused_pending_bound) {
                cxpr_model_session_free(session);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return NULL;
            }
            session->fused_commit_slot_count = program->fused_commit_count;
            session->fused_pending_count = program->fused_commit_count;
        }
    }

    eval_reg = program->registry ? program->registry : reg;
    if (!cxpr_model_compiled_seed_defaults(program, session->ctx, eval_reg, err)) {
        cxpr_model_session_free(session);
        return NULL;
    }
    for (size_t i = 0; i < program->state_default_count; ++i) {
        if (program->state_defaults[i].result_kind == CXPR_MODEL_RESULT_NUMBER) {
            double value = 0.0;
            if (!cxpr_eval_ast_number(program->state_defaults[i].ast,
                                      session->ctx, eval_reg, &value, err)) {
                cxpr_model_session_free(session);
                return NULL;
            }
            cxpr_model_context_set_compiled_number(
                session->ctx, &program->state_defaults[i], value);
        } else if (program->state_defaults[i].result_kind == CXPR_MODEL_RESULT_BOOL) {
            bool value = false;
            if (!cxpr_model_eval_ast_bool_result(program->state_defaults[i].ast,
                                                 session->ctx, eval_reg, &value, err)) {
                cxpr_model_session_free(session);
                return NULL;
            }
            cxpr_model_context_set_compiled_bool(
                session->ctx, &program->state_defaults[i], value);
        } else {
            cxpr_value value = {0};
            if (!cxpr_eval_ast(program->state_defaults[i].ast,
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
    if (program->child_count > 0u) {
        session->child_sessions = (cxpr_model_session**)calloc(
            program->child_count, sizeof(cxpr_model_session*));
        if (!session->child_sessions) {
            cxpr_model_session_free(session);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        session->child_session_count = program->child_count;
        for (size_t i = 0u; i < program->child_count; ++i) {
            session->child_sessions[i] = cxpr_model_session_new(
                program->children[i].program, reg, err);
            if (!session->child_sessions[i]) {
                cxpr_model_session_free(session);
                return NULL;
            }
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
    free(session->fused_pending_values);
    free(session->fused_pending_bound);
    for (size_t i = 0; i < session->child_session_count; ++i) {
        cxpr_model_session_free(session->child_sessions[i]);
    }
    free(session->child_sessions);
    for (size_t i = 0; i < session->child_instance_count; ++i) {
        free(session->child_instances[i].key);
        cxpr_model_session_free(session->child_instances[i].session);
    }
    free(session->child_instances);
    for (size_t i = 0; i < session->pending_capacity; ++i) {
        cxpr_value_free(&session->pending_values[i]);
    }
    free(session->pending_values);
    free(session->pending_binding_indices);
    free(session);
}

static void cxpr_model_session_clear_pending(cxpr_model_session* session) {
    if (!session) return;
    for (size_t i = 0u; i < session->pending_count; ++i) {
        cxpr_value_free(&session->pending_values[i]);
        session->pending_values[i] = (cxpr_value){0};
    }
    session->pending_count = 0u;
}

static cxpr_model_session* cxpr_model_session_child_instance(
    const cxpr_model_compiled* parent_program,
    cxpr_model_session* parent,
    size_t child_index,
    const char* key,
    const cxpr_registry* reg,
    cxpr_error* err) {
    cxpr_model_child_instance* grown;
    cxpr_model_session* child_session;
    char* owned_key;
    if (!parent_program || !parent || !key || child_index >= parent_program->child_count) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid child model call instance", 0, 0);
        return NULL;
    }
    for (size_t i = 0u; i < parent->child_instance_count; ++i) {
        if (parent->child_instances[i].child_index == child_index &&
            parent->child_instances[i].key &&
            strcmp(parent->child_instances[i].key, key) == 0) {
            return parent->child_instances[i].session;
        }
    }
    if (parent->child_instance_count >= parent->child_instance_capacity) {
        size_t next_capacity = parent->child_instance_capacity == 0u
                                   ? 4u
                                   : parent->child_instance_capacity * 2u;
        grown = (cxpr_model_child_instance*)realloc(
            parent->child_instances, next_capacity * sizeof(*parent->child_instances));
        if (!grown) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        parent->child_instances = grown;
        parent->child_instance_capacity = next_capacity;
    }
    owned_key = cxpr_strdup(key);
    if (!owned_key) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    child_session = cxpr_model_session_new(parent_program->children[child_index].program, reg, err);
    if (!child_session) {
        free(owned_key);
        return NULL;
    }
    parent->child_instances[parent->child_instance_count++] = (cxpr_model_child_instance){
        owned_key,
        child_index,
        child_session,
    };
    return child_session;
}

static void cxpr_model_session_commit_pending(const cxpr_model_compiled* program,
                                              cxpr_model_session* session) {
    if (!program || !session) return;
    for (size_t i = 0u; i < session->pending_count; ++i) {
        size_t binding_index = session->pending_binding_indices[i];
        if (binding_index >= program->binding_count) continue;
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
    }
    cxpr_model_session_clear_pending(session);
}

static const cxpr_expr_ast* cxpr_model_child_call_source_arg(const cxpr_model_child_program* child_ref,
                                                        const cxpr_model_compiled* child,
                                                        const cxpr_expr_ast* ast) {
    size_t call_param_count = 0u;
    if (!child_ref || !child || !ast ||
        child_ref->source_input_index == (size_t)-1 ||
        !child_ref->source_arg) {
        return NULL;
    }
    if (cxpr_model_child_call_has_named_args(ast)) {
        for (size_t i = 0u; i < cxpr_model_child_call_argc(ast); ++i) {
            const char* name = cxpr_model_child_call_arg_name(ast, i);
            if (name && cxpr_model_names_match(name, child_ref->source_arg)) {
                return cxpr_model_child_call_arg(ast, i);
            }
        }
        return NULL;
    }
    for (size_t i = 0u; i < child->constant_count; ++i) {
        if (child->constants[i].is_call_param) ++call_param_count;
    }
    if (call_param_count == 0u) call_param_count = child->constant_count;
    return cxpr_model_child_call_argc(ast) == call_param_count + 1u
               ? cxpr_model_child_call_arg(ast, 0u)
               : NULL;
}

static const cxpr_expr_ast* cxpr_model_child_call_param_arg(const cxpr_model_child_program* child_ref,
                                                       const cxpr_model_compiled* child,
                                                       const cxpr_expr_ast* ast,
                                                       size_t param_index) {
    size_t explicit_count = 0u;
    size_t exposed_index = 0u;
    if (!child || !ast || param_index >= child->constant_count) return NULL;
    for (size_t i = 0u; i < child->constant_count; ++i) {
        if (child->constants[i].is_call_param) ++explicit_count;
    }
    if (explicit_count > 0u && !child->constants[param_index].is_call_param) return NULL;
    for (size_t i = 0u; i < param_index; ++i) {
        if (explicit_count == 0u || child->constants[i].is_call_param) ++exposed_index;
    }
    if (cxpr_model_child_call_has_named_args(ast)) {
        const char* param_name = child->constants[param_index].name;
        for (size_t i = 0u; i < cxpr_model_child_call_argc(ast); ++i) {
            const char* name = cxpr_model_child_call_arg_name(ast, i);
            if (name && param_name && cxpr_model_names_match(name, param_name)) {
                return cxpr_model_child_call_arg(ast, i);
            }
        }
        return NULL;
    }
    {
        size_t offset =
            (child_ref && child_ref->source_input_index != (size_t)-1 &&
             cxpr_model_child_call_argc(ast) ==
                 (explicit_count > 0u ? explicit_count : child->constant_count) + 1u)
                ? 1u
                : 0u;
        return exposed_index + offset < cxpr_model_child_call_argc(ast)
                   ? cxpr_model_child_call_arg(ast, exposed_index + offset)
                   : NULL;
    }
}

cxpr_value cxpr_model_eval_child_producer(const cxpr_expr_ast* ast,
                                          const cxpr_context* ctx,
                                          const cxpr_registry* reg,
                                          void* userdata,
                                          cxpr_error* err) {
    cxpr_model_child_program* child_ref = (cxpr_model_child_program*)userdata;
    cxpr_model_session* parent = cxpr_model_active_session();
    cxpr_model_session* child_session;
    const cxpr_model_compiled* child;
    cxpr_value* fields = NULL;
    cxpr_struct_value* record = NULL;
    cxpr_value result = cxpr_num(NAN);
    char* cache_key = NULL;
    bool found = false;
    bool transient_session = false;

    if (!ast || !ctx || !child_ref || !child_ref->program || !parent ||
        child_ref->registry_index >= parent->child_session_count) {
        return cxpr_eval_error(err, CXPR_ERR_SYNTAX, "Invalid model producer call");
    }
    child = child_ref->program;
    cache_key = cxpr_model_child_cache_key(ast);
    if (!cache_key) {
        return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
    }
    {
        const cxpr_struct_value* cached = cache_key
            ? cxpr_context_get_cached_struct(ctx, cache_key)
            : NULL;
        if (cached && cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL) {
            cxpr_struct_value* copy = cxpr_struct_value_new(
                (const char* const*)cached->field_names,
                cached->field_values,
                cached->field_count);
            free(cache_key);
            if (!copy) return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
            return cxpr_struct(copy);
        }
        if (cached) {
            result = cxpr_struct_get_field(cached, cxpr_expr_ast_producer_field(ast), &found);
            free(cache_key);
            if (found) return cxpr_value_clone(&result);
            return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown child model field");
        }
    }
    if (!cxpr_registry_find(reg, child_ref->name)) {
        free(cache_key);
        return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_FUNCTION, "Unknown child model producer");
    }
    if (child->lifetime == CXPR_MODEL_LIFETIME_SINGLETON) {
        child_session = parent->child_sessions[child_ref->registry_index];
    } else if (child->lifetime == CXPR_MODEL_LIFETIME_TRANSIENT) {
        child_session = cxpr_model_session_new(child, reg, err);
        transient_session = true;
    } else {
        child_session = cxpr_model_session_child_instance(
            parent->program, parent, child_ref->registry_index, cache_key, reg, err);
    }
    if (!child_session || !child_session->ctx) {
        if (transient_session) cxpr_model_session_free(child_session);
        free(cache_key);
        return cxpr_eval_error(err, CXPR_ERR_SYNTAX, "Missing child model session");
    }
    for (size_t i = 0u; i < child->input_count; ++i) {
        bool input_found = false;
        bool owns_value = false;
        const cxpr_expr_ast* named_input_arg = cxpr_model_child_call_named_arg(ast, child->inputs[i]);
        const cxpr_expr_ast* source_arg =
            (!named_input_arg && i == child_ref->source_input_index)
                ? cxpr_model_child_call_source_arg(child_ref, child, ast)
                : NULL;
        cxpr_value value = (named_input_arg || source_arg)
                               ? cxpr_eval_node(named_input_arg ? named_input_arg : source_arg,
                                                ctx,
                                                reg,
                                                err)
                               : cxpr_context_get_typed(ctx, child->inputs[i], &input_found);
        if ((named_input_arg || source_arg) && err && err->code != CXPR_OK) {
            if (transient_session) cxpr_model_session_free(child_session);
            free(cache_key);
            return cxpr_num(NAN);
        }
        if (named_input_arg || source_arg) {
            input_found = true;
            owns_value = true;
        }
        if (!input_found) {
            if (transient_session) cxpr_model_session_free(child_session);
            free(cache_key);
            return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Missing child model input");
        }
        if (value.type == CXPR_VALUE_NUMBER) {
            cxpr_context_set(child_session->ctx, child->inputs[i], value.d);
        } else {
            cxpr_context_set_value(child_session->ctx, child->inputs[i], &value);
        }
        if (owns_value) cxpr_value_free(&value);
    }
    for (size_t i = 0u; i < child->constant_count; ++i) {
        const cxpr_expr_ast* arg = cxpr_model_child_call_param_arg(child_ref, child, ast, i);
        cxpr_value value;
        if (!arg) {
            continue;
        }
        value = cxpr_eval_node(arg, ctx, reg, err);
        if (err && err->code != CXPR_OK) {
            if (transient_session) cxpr_model_session_free(child_session);
            free(cache_key);
            return cxpr_num(NAN);
        }
        cxpr_context_set_param_value(child_session->ctx, child->constants[i].name, &value);
        cxpr_value_free(&value);
    }
    if (!cxpr_model_session_tick(child, child_session, reg, err)) {
        if (transient_session) cxpr_model_session_free(child_session);
        free(cache_key);
        return cxpr_num(NAN);
    }

    fields = (cxpr_value*)calloc(child->output_count, sizeof(cxpr_value));
    if (!fields) {
        if (transient_session) cxpr_model_session_free(child_session);
        free(cache_key);
        return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
    }
    for (size_t i = 0u; i < child->output_count; ++i) {
        double number = 0.0;
        bool boolean = false;
        if (cxpr_model_session_get_number(child_session, child->outputs[i], &number)) {
            fields[i] = cxpr_num(number);
        } else if (cxpr_model_session_get_bool(child_session, child->outputs[i], &boolean)) {
            fields[i] = cxpr_bool(boolean);
        } else {
            fields[i] = cxpr_context_get_typed(child_session->ctx, child->outputs[i], &found);
            if (!found) fields[i] = cxpr_num(NAN);
        }
    }
    record = cxpr_struct_value_new((const char* const*)child->outputs,
                                   fields,
                                   child->output_count);
    for (size_t i = 0u; i < child->output_count; ++i) cxpr_value_free(&fields[i]);
    free(fields);
    if (!record) {
        if (transient_session) cxpr_model_session_free(child_session);
        free(cache_key);
        return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
    }
    if (transient_session) {
        cxpr_model_session_free(child_session);
        child_session = NULL;
    }
    cxpr_context_set_cached_struct((cxpr_context*)ctx, cache_key, record);
    free(cache_key);
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL) {
        result = cxpr_struct(record);
        record = NULL;
        found = true;
    } else {
        result = cxpr_struct_get_field(record, cxpr_expr_ast_producer_field(ast), &found);
        if (found) result = cxpr_value_clone(&result);
    }
    cxpr_struct_value_free(record);
    if (!found) return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown child model field");
    return result;
}

cxpr_context* cxpr_model_session_context(cxpr_model_session* session) {
    return session ? session->ctx : NULL;
}

static bool cxpr_model_session_tick_fused(const cxpr_model_compiled* program,
                                          cxpr_model_session* session,
                                          const cxpr_registry* eval_reg,
                                          bool materialize_context,
                                          cxpr_error* err) {
    double ignored;

    if (!program->has_fused_ir || !session->fused_slots) return false;
    for (size_t i = 0; i < program->fused_commit_count; ++i) {
        if (i >= session->fused_pending_count || !session->fused_pending_bound[i]) continue;
        session->fused_slots[program->fused_commits[i].state_slot] =
            session->fused_pending_values[i];
        session->fused_pending_bound[i] = false;
        if (materialize_context) {
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
    for (size_t i = 0; i < program->fused_input_count; ++i) {
        bool found = false;
        double value;
        if (i < session->fused_input_slot_count &&
            session->fused_input_slot_bound[i] &&
            cxpr_context_slot_valid(session->ctx, &session->fused_input_slots[i])) {
            value = cxpr_context_slot_get(&session->fused_input_slots[i]);
            found = true;
        } else {
            cxpr_value typed = cxpr_model_context_get_history_value(
                session->ctx, program->fused_inputs[i].name, &found);
            if (found && typed.type == CXPR_VALUE_BOOL) {
                value = typed.b ? 1.0 : 0.0;
            } else {
                value = typed.d;
            }
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
        if (i >= session->fused_pending_count) continue;
        session->fused_pending_values[i] =
            session->fused_slots[program->fused_commits[i].update_slot];
        session->fused_pending_bound[i] = true;
    }

    if (materialize_context) {
        for (size_t i = 0; i < program->fused_export_count; ++i) {
            double value = session->fused_slots[program->fused_exports[i].slot];
            if (program->fused_exports[i].result_kind == CXPR_MODEL_RESULT_BOOL) {
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

bool cxpr_model_session_tick(const cxpr_model_compiled* program,
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
    eval_reg = program->registry ? program->registry : reg;
    for (size_t i = 0u; i < program->constant_count; ++i) {
        const cxpr_model_compiled_binding* param = &program->constants[i];
        bool found = false;
        double value;
        if (!param->has_min_value && !param->has_max_value) continue;
        value = cxpr_context_get(session->ctx, param->name, &found);
        if (!found) continue;
        if (param->has_min_value && value < param->min_value) value = param->min_value;
        if (param->has_max_value && value > param->max_value) value = param->max_value;
        cxpr_context_set_prehashed(session->ctx, param->name, param->name_hash, value);
    }
    previous_active_session = g_model_active_session;
    g_model_active_session = session;
    cxpr_context_clear_cached_structs(session->ctx);
    if (program->has_fused_ir && session->fused_slots) {
        bool ok = cxpr_model_session_tick_fused(program, session, eval_reg, true, err);
        g_model_active_session = previous_active_session;
        return ok;
    }
    cxpr_model_session_commit_pending(program, session);

    for (size_t i = 0; i < program->binding_count; ++i) {
        cxpr_value value = {0};
        if (program->bindings[i].result_kind == CXPR_MODEL_RESULT_NUMBER) {
            double number = 0.0;
            if (!cxpr_eval_ast_number(program->bindings[i].ast,
                                      session->ctx, eval_reg, &number, err)) {
                cxpr_model_session_clear_pending(session);
                g_model_active_session = previous_active_session;
                return false;
            }
            value = cxpr_num(number);
        } else if (program->bindings[i].result_kind == CXPR_MODEL_RESULT_BOOL) {
            bool boolean = false;
            if (!cxpr_model_eval_ast_bool_result(program->bindings[i].ast,
                                                 session->ctx, eval_reg, &boolean, err)) {
                cxpr_model_session_clear_pending(session);
                g_model_active_session = previous_active_session;
                return false;
            }
            value = cxpr_bool(boolean);
        } else if (!cxpr_eval_ast(program->bindings[i].ast,
                                  session->ctx, eval_reg, &value, err)) {
            cxpr_model_session_clear_pending(session);
            g_model_active_session = previous_active_session;
            return false;
        }
        if (program->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) {
            if (session->pending_count >= session->pending_capacity) {
                cxpr_value_free(&value);
                cxpr_model_session_clear_pending(session);
                g_model_active_session = previous_active_session;
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            session->pending_binding_indices[session->pending_count] = i;
            session->pending_values[session->pending_count] = value;
            session->pending_count++;
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

bool cxpr_model_session_tick_fast(const cxpr_model_compiled* program,
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
