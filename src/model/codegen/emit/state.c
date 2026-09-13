#include "model/codegen/ast/internal.h"
#include "model/window/window.h"
#include "registry/internal.h"

#include <cxpr/resample.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* cxpr_model_c_history_counter_type(size_t capacity) {
    return capacity > 255u ? "size_t" : "uint8_t";
}

bool cxpr_model_c_emit_runtime_state_typedef(
    cxpr_model_c_buf* b,
    const cxpr_model_compiled* program,
    const cxpr_model_window_plan* window_plan,
    const char* safe_name,
    const size_t* child_call_child_indices,
    size_t child_call_count,
    cxpr_error* err) {
    if (!b || !program || !safe_name) return false;
    cxpr_model_c_printf(b, "typedef struct %s_state {\n", safe_name);
    cxpr_model_c_puts(b, "    uint8_t init;\n");
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        char* field_name = cxpr_model_c_prefixed_name("state_", program->state_defaults[i].name);
        if (!field_name) {
            free(field_name);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        if (program->state_defaults[i].declared_type == CXPR_MODEL_DECL_BUFFER) {
            cxpr_model_c_printf(b,
                "    struct { double values[%zu]; size_t next; size_t count; } %s;\n",
                program->state_defaults[i].buffer_samples, field_name);
        } else {
            cxpr_model_c_printf(
                b, "    %s %s;\n",
                program->state_defaults[i].result_kind == CXPR_MODEL_RESULT_BOOL
                    ? "uint8_t" : "double", field_name);
        }
        free(field_name);
    }
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        size_t capacity;
        if (program->history_specs[i].depth == 0u) continue;
        capacity = cxpr_model_c_history_capacity(program->history_specs[i].depth);
        cxpr_model_c_printf(b, "    cxpr_history%zu history_%zu;\n", capacity, i);
    }
    if (window_plan) {
        for (size_t i = 0u; i < window_plan->node_count; ++i) {
            const cxpr_model_window_plan_node* node = &window_plan->nodes[i];
            if (node->slot_count < 4u) continue;
            cxpr_model_c_printf(b, "    cxpr_window%zu window_%zu;\n",
                                node->slot_count - 4u, i);
        }
    }
    for (size_t i = 0u; i < child_call_count; ++i) {
        size_t child_index = child_call_child_indices ? child_call_child_indices[i] : (size_t)-1;
        char* child_tick_name;
        if (child_index >= program->child_count) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid child model callsite", 0, 0);
            return false;
        }
        child_tick_name = cxpr_model_c_child_tick_name(safe_name, child_index);
        if (!child_tick_name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        cxpr_model_c_printf(b, "    %s_state child_call_%zu_state;\n", child_tick_name, i);
        cxpr_model_c_printf(b, "    uint8_t child_call_%zu_initialized;\n", i);
        cxpr_model_c_printf(b, "    double child_call_%zu_outputs[%zu];\n",
                            i,
                            program->children[child_index].program &&
                                    program->children[child_index].program->output_count
                                ? program->children[child_index].program->output_count
                                : 1u);
        free(child_tick_name);
    }
    cxpr_model_c_printf(b, "} %s_state;\n\n", safe_name);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

bool cxpr_model_c_emit_state_typedefs(cxpr_model_c_buf* b,
                                             const cxpr_model_compiled* program,
                                             const cxpr_model_window_plan* window_plan,
                                             const char* safe_name,
                                             cxpr_error* err) {
    if (!b || !program || !safe_name) return false;
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        size_t depth = program->history_specs[i].depth;
        size_t capacity = cxpr_model_c_history_capacity(depth);
        const char* counter_type = cxpr_model_c_history_counter_type(capacity);
        bool already_emitted = false;
        if (depth == 0u) continue;
        for (size_t j = 0u; j < i; ++j) {
            if (program->history_specs[j].depth > 0u &&
                cxpr_model_c_history_capacity(program->history_specs[j].depth) == capacity) {
                already_emitted = true;
                break;
            }
        }
        if (already_emitted) continue;
        if (cxpr_model_c_history_use_shift(depth)) {
            cxpr_model_c_printf(
                b,
                "#ifndef CXPR_HISTORY%zu_DEFINED\n"
                "#define CXPR_HISTORY%zu_DEFINED\n"
                "typedef struct { double values[%zu]; } cxpr_history%zu;\n"
                "#endif\n",
                capacity,
                capacity,
                capacity,
                capacity);
        } else {
            cxpr_model_c_printf(
                b,
                "#ifndef CXPR_HISTORY%zu_DEFINED\n"
                "#define CXPR_HISTORY%zu_DEFINED\n"
                "typedef struct { %s next; double values[%zu]; } cxpr_history%zu;\n"
                "#endif\n",
                capacity,
                capacity,
                counter_type,
                capacity,
                capacity);
        }
    }
    if (window_plan) {
        for (size_t i = 0u; i < window_plan->node_count; ++i) {
            const cxpr_model_window_plan_node* node = &window_plan->nodes[i];
            const char* counter_type = cxpr_model_c_window_counter_type(node);
            size_t capacity = node->slot_count >= 4u ? node->slot_count - 4u : 0u;
            bool already_emitted = false;
            if (node->slot_count < 4u) continue;
            for (size_t j = 0u; j < i; ++j) {
                const cxpr_model_window_plan_node* prior = &window_plan->nodes[j];
                if (prior->slot_count >= 4u && prior->slot_count - 4u == capacity) {
                    already_emitted = true;
                    break;
                }
            }
            if (already_emitted) continue;
            cxpr_model_c_printf(
                b,
                "#ifndef CXPR_WINDOW%zu_DEFINED\n"
                "#define CXPR_WINDOW%zu_DEFINED\n"
                "typedef struct { uint8_t init; %s next; %s count; double sum; double values[%zu]; } cxpr_window%zu;\n"
                "#endif\n",
                capacity,
                capacity,
                counter_type,
                counter_type,
                capacity,
                capacity);
        }
    }
    if ((program->history_spec_count > 0u || (window_plan && window_plan->node_count > 0u))) {
        cxpr_model_c_puts(b, "\n");
    }
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

bool cxpr_model_c_init_sentinel_slot(const cxpr_model_compiled* program,
                                            const cxpr_model_window_plan* window_plan,
                                            size_t* out_slot) {
    (void)out_slot;
    if (!program) return false;
    if (program->state_default_count > 0u) return true;
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        if (program->history_specs[i].depth > 0u) return true;
    }
    return window_plan && window_plan->node_count > 0u;
}

bool cxpr_model_c_emit_slot_init_function(cxpr_model_c_buf* b,
                                                 const cxpr_model_compiled* program,
                                                 const cxpr_model_window_plan* window_plan,
                                                 const char* qualifiers,
                                                 const char* safe_name,
                                                 cxpr_error* err) {
    size_t sentinel = 0u;
    if (!b || !program || !safe_name) return false;
    if (!cxpr_model_c_init_sentinel_slot(program, window_plan, &sentinel)) return true;
    (void)sentinel;
    cxpr_model_c_printf(b, "/* Source model slot init: %s */\n", safe_name);
    if (qualifiers && qualifiers[0]) cxpr_model_c_printf(b, "%s ", qualifiers);
    cxpr_model_c_printf(
        b,
        "void %s_init_state(%s_state* restrict _cx_state) {\n",
        safe_name,
        safe_name);
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        size_t depth = program->history_specs[i].depth;
        size_t capacity = cxpr_model_c_history_capacity(depth);
        if (depth == 0u) continue;
        cxpr_model_c_printf(
            b,
            "    for (size_t _cx_init_i = 0u; _cx_init_i < %zuu; ++_cx_init_i) _cx_state->history_%zu.values[_cx_init_i] = NAN;\n",
            capacity,
            i);
        if (!cxpr_model_c_history_use_shift(depth)) {
            cxpr_model_c_printf(b, "    _cx_state->history_%zu.next = 0u;\n", i);
        }
    }
    if (window_plan) {
        for (size_t i = 0u; i < window_plan->node_count; ++i) {
            const cxpr_model_window_plan_node* node = &window_plan->nodes[i];
            size_t base = cxpr_model_c_window_plan_base(program, node);
            if (base == (size_t)-1 || node->slot_count < 4u) continue;
            (void)base;
            cxpr_model_c_printf(
                b,
                "    for (size_t _cx_init_i = 0u; _cx_init_i < %zuu; ++_cx_init_i) _cx_state->window_%zu.values[_cx_init_i] = NAN;\n"
                "    _cx_state->window_%zu.next = 0u;\n"
                "    _cx_state->window_%zu.count = 0u;\n"
                "    _cx_state->window_%zu.sum = 0.0;\n"
                "    _cx_state->window_%zu.init = 1u;\n",
                node->slot_count - 4u,
                i,
                i,
                i,
                i,
                i);
        }
    }
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        char* field_name = cxpr_model_c_prefixed_name(
            "state_", program->state_defaults[i].name);
        if (!field_name) {
            free(field_name);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        if (program->state_defaults[i].declared_type == CXPR_MODEL_DECL_BUFFER) {
            cxpr_model_c_printf(b,
                "    for (size_t _cx_init_i = 0u; _cx_init_i < %zuu; ++_cx_init_i) _cx_state->%s.values[_cx_init_i] = NAN;\n"
                "    _cx_state->%s.next = 0u;\n"
                "    _cx_state->%s.count = 0u;\n",
                program->state_defaults[i].buffer_samples, field_name,
                field_name, field_name);
        } else {
            cxpr_model_c_printf(
                b, "    _cx_state->%s = %s;\n", field_name,
                program->state_defaults[i].result_kind == CXPR_MODEL_RESULT_BOOL
                ? (cxpr_expr_ast_kind_of(program->state_defaults[i].ast) == CXPR_NODE_BOOL &&
                           cxpr_expr_ast_bool_value(program->state_defaults[i].ast)
                       ? "1u"
                       : "0u")
                : "0.0");
        }
        free(field_name);
    }
    cxpr_model_c_puts(b, "    _cx_state->init = 1u;\n");
    cxpr_model_c_puts(b, "}\n\n");
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}
