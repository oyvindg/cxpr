#include "model/codegen/ast/internal.h"
#include "model/window/plan.h"
#include "model/window/window.h"
#include "registry/internal.h"
#include <cxpr/codegen.h>
#include <cxpr/resample.h>
#include "eval/internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t cxpr_model_resample_slot(const cxpr_model_compiled* program,
                                       const cxpr_expr_ast* ast) {
    cxpr_resample_call call = {0};
    cxpr_error ignored = {0};
    if (!program || !cxpr_resample_call_parse(ast, &call, &ignored) || !call.source ||
        cxpr_expr_ast_kind_of(call.source) != CXPR_NODE_IDENTIFIER) return (size_t)-1;
    for (size_t i = 0; i < program->resample_requirement_count; ++i)
        if (program->resample_requirements[i].duration_ns == call.every.duration_ns &&
            cxpr_model_names_match(program->resample_requirements[i].source_name,
                                   cxpr_expr_ast_identifier_name(call.source))) return i;
    return (size_t)-1;
}

static bool cxpr_model_resample_cse_add(cxpr_model_ast_c_target* target,
                                        size_t slot, unsigned lookback) {
    for (size_t i = 0; i < target->resample_cse_count; ++i) {
        if (target->resample_cse[i].slot == slot && target->resample_cse[i].lookback == lookback) {
            target->resample_cse[i].uses++;
            return true;
        }
    }
    cxpr_model_resample_cse* grown = realloc(
        target->resample_cse, (target->resample_cse_count + 1u) * sizeof(*grown));
    if (!grown) return false;
    target->resample_cse = grown;
    target->resample_cse[target->resample_cse_count++] =
        (cxpr_model_resample_cse){slot, lookback, 1u};
    return true;
}

bool cxpr_model_collect_resample_cse(cxpr_model_ast_c_target* target,
                                            const cxpr_expr_ast* ast,
                                            unsigned offset) {
    if (!ast) return true;
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_INDEX) {
        const cxpr_expr_ast* index = cxpr_expr_ast_index_expression(ast);
        if (index && cxpr_expr_ast_kind_of(index) == CXPR_NODE_NUMBER) {
            double raw = cxpr_expr_ast_number_value(index);
            unsigned add = (unsigned)raw;
            if (raw == (double)add && add <= (unsigned)-1 - offset)
                return cxpr_model_collect_resample_cse(
                    target, cxpr_expr_ast_index_target(ast), offset + add);
        }
    }
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL) {
        size_t slot = cxpr_model_resample_slot(target->program, ast);
        if (slot != (size_t)-1) return cxpr_model_resample_cse_add(target, slot, offset);
        for (size_t i = 0; i < cxpr_expr_ast_call_arg_count(ast); ++i)
            if (!cxpr_model_collect_resample_cse(target, cxpr_expr_ast_call_arg(ast, i), offset)) return false;
    } else if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_BINARY_OP) {
        return cxpr_model_collect_resample_cse(target, cxpr_expr_ast_binary_left(ast), offset) &&
               cxpr_model_collect_resample_cse(target, cxpr_expr_ast_binary_right(ast), offset);
    } else if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_UNARY_OP) {
        return cxpr_model_collect_resample_cse(target, cxpr_expr_ast_unary_operand(ast), offset);
    } else if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_TERNARY) {
        return cxpr_model_collect_resample_cse(target, cxpr_expr_ast_ternary_condition(ast), offset) &&
               cxpr_model_collect_resample_cse(target, cxpr_expr_ast_ternary_true(ast), offset) &&
               cxpr_model_collect_resample_cse(target, cxpr_expr_ast_ternary_false(ast), offset);
    }
    return true;
}

bool cxpr_model_c_symbol_is_input(const cxpr_model_compiled* program,
                                         const char* name,
                                         size_t* out_index) {
    if (!program || !name) return false;
    for (size_t i = 0u; i < program->fused_input_count; ++i) {
        if (cxpr_model_names_match(program->fused_inputs[i].name, name)) {
            if (out_index) *out_index = i;
            return true;
        }
    }
    return false;
}

bool cxpr_model_c_symbol_is_state(const cxpr_model_compiled* program,
                                         const char* name,
                                         size_t* out_slot) {
    if (!program || !name) return false;
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        if (cxpr_model_names_match(program->state_defaults[i].name, name)) {
            size_t slot = cxpr_model_fused_slot_find(
                program->fused_slot_names, program->fused_slot_count, name);
            if (out_slot) *out_slot = slot;
            return slot != (size_t)-1;
        }
    }
    return false;
}

bool cxpr_model_c_symbol_is_binding(const cxpr_model_compiled* program,
                                           const char* name) {
    if (!program || !name) return false;
    for (size_t i = 0u; i < program->binding_count; ++i) {
        if (program->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) continue;
        if (cxpr_model_names_match(program->bindings[i].name, name)) return true;
    }
    return false;
}

const cxpr_model_compiled_binding* cxpr_model_c_binding_for_name(
    const cxpr_model_compiled* program,
    const char* name) {
    if (!program || !name) return NULL;
    for (size_t i = 0u; i < program->binding_count; ++i) {
        if (program->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) continue;
        if (cxpr_model_names_match(program->bindings[i].name, name)) return &program->bindings[i];
    }
    return NULL;
}

const cxpr_model_compiled_binding* cxpr_model_c_constant_for_name(
    const cxpr_model_compiled* program,
    const char* name) {
    if (!program || !name) return NULL;
    for (size_t i = 0u; i < program->constant_count; ++i) {
        if (cxpr_model_names_match(program->constants[i].name, name)) return &program->constants[i];
    }
    return NULL;
}

const char* cxpr_model_c_source_for_name(const cxpr_model_compiled* program,
                                                const char* name) {
    const cxpr_model_compiled_binding* binding;
    if (!program || !name) return NULL;
    binding = cxpr_model_c_binding_for_name(program, name);
    if (binding && binding->source) return binding->source;
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        if (cxpr_model_names_match(program->state_defaults[i].name, name)) {
            return program->state_defaults[i].source;
        }
    }
    for (size_t i = 0u; i < program->constant_count; ++i) {
        if (cxpr_model_names_match(program->constants[i].name, name)) {
            return program->constants[i].source;
        }
    }
    return NULL;
}

void cxpr_model_c_emit_source_comment(cxpr_model_c_buf* b,
                                             const char* label,
                                             const char* source) {
    if (!b || !source || !source[0]) return;
    cxpr_model_c_printf(b, "    // %s: ", label ? label : ".cxpr");
    for (const char* p = source; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\n' || ch == '\r' || ch == '\t' || ch < 32u) {
            cxpr_model_c_puts(b, " ");
        } else {
            char s[2] = { (char)ch, '\0' };
            cxpr_model_c_puts(b, s);
        }
    }
    cxpr_model_c_puts(b, "\n");
}

const cxpr_expr_ast* cxpr_model_producer_arg_for_param(const cxpr_expr_ast* ast,
                                                         const char* param_name,
                                                         size_t param_index) {
    if (!ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_PRODUCER_ACCESS) return NULL;
    if (cxpr_expr_ast_producer_has_named_args(ast)) {
        for (size_t i = 0u; i < cxpr_expr_ast_producer_arg_count(ast); ++i) {
            const char* arg_name = cxpr_expr_ast_producer_arg_name(ast, i);
            if (arg_name && param_name && cxpr_model_names_match(arg_name, param_name)) {
                return cxpr_expr_ast_producer_arg(ast, i);
            }
        }
        return NULL;
    }
    return param_index < cxpr_expr_ast_producer_arg_count(ast)
               ? cxpr_expr_ast_producer_arg(ast, param_index)
               : NULL;
}

const cxpr_expr_ast* cxpr_model_child_call_source_arg(const cxpr_model_child_program* child_ref,
                                                        const cxpr_model_compiled* child,
                                                        const cxpr_expr_ast* ast) {
    if (!child_ref || !child || !ast ||
        child_ref->source_input_index == (size_t)-1 ||
        !child_ref->source_arg) {
        return NULL;
    }
    if (cxpr_expr_ast_producer_has_named_args(ast)) {
        for (size_t i = 0u; i < cxpr_expr_ast_producer_arg_count(ast); ++i) {
            const char* name = cxpr_expr_ast_producer_arg_name(ast, i);
            if (name && cxpr_model_names_match(name, child_ref->source_arg)) {
                return cxpr_expr_ast_producer_arg(ast, i);
            }
        }
        return NULL;
    }
    return cxpr_expr_ast_producer_arg_count(ast) == child->constant_count + 1u
               ? cxpr_expr_ast_producer_arg(ast, 0u)
               : NULL;
}

const cxpr_expr_ast* cxpr_model_child_call_param_arg(const cxpr_model_child_program* child_ref,
                                                       const cxpr_model_compiled* child,
                                                       const cxpr_expr_ast* ast,
                                                       size_t param_index) {
    if (!child || !ast || param_index >= child->constant_count) return NULL;
    if (cxpr_expr_ast_producer_has_named_args(ast)) {
        const char* param_name = child->constants[param_index].name;
        for (size_t i = 0u; i < cxpr_expr_ast_producer_arg_count(ast); ++i) {
            const char* name = cxpr_expr_ast_producer_arg_name(ast, i);
            if (name && param_name && cxpr_model_names_match(name, param_name)) {
                return cxpr_expr_ast_producer_arg(ast, i);
            }
        }
        return NULL;
    }
    {
        size_t offset =
            (child_ref && child_ref->source_input_index != (size_t)-1 &&
             cxpr_expr_ast_producer_arg_count(ast) == child->constant_count + 1u)
                ? 1u
                : 0u;
        return param_index + offset < cxpr_expr_ast_producer_arg_count(ast)
                   ? cxpr_expr_ast_producer_arg(ast, param_index + offset)
                   : NULL;
    }
}

static size_t CXPR_MODEL_MAYBE_UNUSED
cxpr_model_c_child_base_inline(const cxpr_model_compiled* program,
                               size_t child_index) {
    size_t base;
    if (!program || child_index >= program->child_count) return (size_t)-1;
    base = program->state_default_count;
    for (size_t i = 0u; i < child_index; ++i) {
        const cxpr_model_compiled* child = program->children[i].program;
        base += cxpr_model_compiled_c_slot_count(child) + 1u + child->output_count;
    }
    return base;
}

size_t cxpr_model_c_child_index_for_entry(const cxpr_model_compiled* program,
                                                 const cxpr_func_entry* entry) {
    if (!program || !entry || !entry->model_producer_userdata) return (size_t)-1;
    for (size_t i = 0u; i < program->child_count; ++i) {
        if (&program->children[i] == entry->model_producer_userdata) return i;
    }
    return (size_t)-1;
}

char* cxpr_model_c_child_call_key(const cxpr_expr_ast* ast) {
    const char* name;
    size_t argc;
    size_t len;
    size_t pos;
    char* out;
    if (!ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_PRODUCER_ACCESS) return NULL;
    name = cxpr_expr_ast_producer_name(ast);
    argc = cxpr_expr_ast_producer_arg_count(ast);
    if (!name) return NULL;
    len = strlen(name) + 3u;
    for (size_t i = 0u; i < argc; ++i) {
        const char* arg_name = cxpr_expr_ast_producer_arg_name(ast, i);
        char* arg = cxpr_expr_ast_to_string(cxpr_expr_ast_producer_arg(ast, i));
        len += arg_name ? strlen(arg_name) + 1u : 0u;
        len += arg ? strlen(arg) : 0u;
        len += 2u;
        free(arg);
    }
    out = (char*)malloc(len + 1u);
    if (!out) return NULL;
    pos = (size_t)snprintf(out, len + 1u, "%s(", name);
    for (size_t i = 0u; i < argc; ++i) {
        const char* arg_name = cxpr_expr_ast_producer_arg_name(ast, i);
        char* arg = cxpr_expr_ast_to_string(cxpr_expr_ast_producer_arg(ast, i));
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

size_t cxpr_model_c_child_call_index_for_key(
    char* const* keys,
    const size_t* child_indices,
    size_t count,
    size_t child_index,
    const char* key) {
    if (!keys || !child_indices || !key) return (size_t)-1;
    for (size_t i = 0u; i < count; ++i) {
        if (child_indices[i] == child_index && keys[i] && strcmp(keys[i], key) == 0) {
            return i;
        }
    }
    return (size_t)-1;
}

static bool cxpr_model_c_child_call_collect_append(char*** keys,
                                                   size_t** child_indices,
                                                   size_t* count,
                                                   size_t* capacity,
                                                   size_t child_index,
                                                   char* key,
                                                   cxpr_error* err) {
    char** grown_keys;
    size_t* grown_indices;
    if (!keys || !child_indices || !count || !capacity || !key) return true;
    if (cxpr_model_c_child_call_index_for_key(
            *keys, *child_indices, *count, child_index, key) != (size_t)-1) {
        free(key);
        return true;
    }
    if (*count >= *capacity) {
        size_t next_capacity = *capacity == 0u ? 4u : *capacity * 2u;
        grown_keys = (char**)realloc(*keys, next_capacity * sizeof(**keys));
        if (!grown_keys) {
            free(key);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        grown_indices = (size_t*)realloc(*child_indices, next_capacity * sizeof(**child_indices));
        if (!grown_indices) {
            free(key);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        *keys = grown_keys;
        *child_indices = grown_indices;
        *capacity = next_capacity;
    }
    (*keys)[*count] = key;
    (*child_indices)[*count] = child_index;
    (*count)++;
    return true;
}

bool cxpr_model_c_collect_child_calls_from_ast(const cxpr_model_compiled* program,
                                                      const cxpr_expr_ast* ast,
                                                      char*** keys,
                                                      size_t** child_indices,
                                                      size_t* count,
                                                      size_t* capacity,
                                                      cxpr_error* err) {
    if (!program || !ast) return true;
    switch (cxpr_expr_ast_kind_of(ast)) {
    case CXPR_NODE_ARRAY:
        for (size_t i = 0u; i < ast->data.array.count; ++i) {
            if (!cxpr_model_c_collect_child_calls_from_ast(
                    program, ast->data.array.elements[i], keys, child_indices, count, capacity, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_RECORD:
        for (size_t i = 0u; i < ast->data.record.field_count; ++i) {
            if (!cxpr_model_c_collect_child_calls_from_ast(
                    program, ast->data.record.field_values[i], keys, child_indices, count, capacity, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_FIELD_ACCESS: {
        const cxpr_model_compiled_binding* binding = NULL;
        if (ast->data.field_access.base) {
            return cxpr_model_c_collect_child_calls_from_ast(
                program, ast->data.field_access.base,
                keys, child_indices, count, capacity, err);
        }
        if (ast->data.field_access.object) {
            binding = cxpr_model_c_binding_for_name(
                program, ast->data.field_access.object);
        }
        if (binding &&
            cxpr_expr_ast_kind_of(binding->ast) == CXPR_NODE_FUNCTION_CALL) {
            cxpr_expr_ast producer = {0};
            producer.type = CXPR_NODE_PRODUCER_ACCESS;
            producer.data.producer_access.name = binding->ast->data.function_call.name;
            producer.data.producer_access.args = binding->ast->data.function_call.args;
            producer.data.producer_access.arg_names = binding->ast->data.function_call.arg_names;
            producer.data.producer_access.argc = binding->ast->data.function_call.argc;
            producer.data.producer_access.field = ast->data.field_access.field;
            return cxpr_model_c_collect_child_calls_from_ast(
                program, &producer, keys, child_indices, count, capacity, err);
        }
        return !binding || cxpr_model_c_collect_child_calls_from_ast(
            program, binding->ast, keys, child_indices, count, capacity, err);
    }
    case CXPR_NODE_PRODUCER_ACCESS: {
        cxpr_func_entry* entry = cxpr_registry_find(program->registry, ast->data.producer_access.name);
        if (entry && entry->model_producer) {
            size_t child_index = cxpr_model_c_child_index_for_entry(program, entry);
            char* key = cxpr_model_c_child_call_key(ast);
            if (!key) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            if (child_index != (size_t)-1) {
                if (!cxpr_model_c_child_call_collect_append(
                        keys, child_indices, count, capacity, child_index, key, err)) {
                    return false;
                }
            } else {
                free(key);
            }
        }
        for (size_t i = 0u; i < ast->data.producer_access.argc; ++i) {
            if (!cxpr_model_c_collect_child_calls_from_ast(
                    program, ast->data.producer_access.args[i], keys, child_indices, count, capacity, err)) {
                return false;
            }
        }
        return true;
    }
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_c_collect_child_calls_from_ast(
                   program, ast->data.binary_op.left, keys, child_indices, count, capacity, err) &&
               cxpr_model_c_collect_child_calls_from_ast(
                   program, ast->data.binary_op.right, keys, child_indices, count, capacity, err);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_c_collect_child_calls_from_ast(
            program, ast->data.unary_op.operand, keys, child_indices, count, capacity, err);
    case CXPR_NODE_FUNCTION_CALL:
        {
            cxpr_func_entry* entry = program->registry
                ? cxpr_registry_find(program->registry, ast->data.function_call.name)
                : NULL;
            if (entry && entry->model_producer) {
                cxpr_expr_ast producer = {0};
                size_t child_index = cxpr_model_c_child_index_for_entry(program, entry);
                char* key;
                producer.type = CXPR_NODE_PRODUCER_ACCESS;
                producer.data.producer_access.name = ast->data.function_call.name;
                producer.data.producer_access.args = ast->data.function_call.args;
                producer.data.producer_access.arg_names = ast->data.function_call.arg_names;
                producer.data.producer_access.argc = ast->data.function_call.argc;
                key = cxpr_model_c_child_call_key(&producer);
                if (!key) {
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                    return false;
                }
                if (child_index != (size_t)-1 &&
                    !cxpr_model_c_child_call_collect_append(
                        keys, child_indices, count, capacity, child_index, key, err)) {
                    return false;
                }
                if (child_index == (size_t)-1) free(key);
            }
        }
        for (size_t i = 0u; i < ast->data.function_call.argc; ++i) {
            if (!cxpr_model_c_collect_child_calls_from_ast(
                    program, ast->data.function_call.args[i], keys, child_indices, count, capacity, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_INDEX:
        return cxpr_model_c_collect_child_calls_from_ast(
                   program, ast->data.index.target, keys, child_indices, count, capacity, err) &&
               cxpr_model_c_collect_child_calls_from_ast(
                   program, ast->data.index.index, keys, child_indices, count, capacity, err);
    case CXPR_NODE_TERNARY:
        return cxpr_model_c_collect_child_calls_from_ast(
                   program, ast->data.ternary.condition, keys, child_indices, count, capacity, err) &&
               cxpr_model_c_collect_child_calls_from_ast(
                   program, ast->data.ternary.true_branch, keys, child_indices, count, capacity, err) &&
               cxpr_model_c_collect_child_calls_from_ast(
                   program, ast->data.ternary.false_branch, keys, child_indices, count, capacity, err);
    default:
        return true;
    }
}
