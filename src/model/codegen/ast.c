#include "model/codegen/codegen_ast_internal.h"
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

static char* cxpr_model_ast_field_expr_to_c(const cxpr_model_compiled* program,
                                            const cxpr_expr_ast* ast,
                                            const char* field,
                                            const cxpr_c_target* target,
                                            cxpr_error* err,
                                            unsigned depth);

static bool cxpr_model_c_symbol_is_input(const cxpr_model_compiled* program,
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

static bool cxpr_model_c_symbol_is_binding(const cxpr_model_compiled* program,
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

static const cxpr_expr_ast* cxpr_model_producer_arg_for_param(const cxpr_expr_ast* ast,
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

static const cxpr_expr_ast* cxpr_model_child_call_source_arg(const cxpr_model_child_program* child_ref,
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

static const cxpr_expr_ast* cxpr_model_child_call_param_arg(const cxpr_model_child_program* child_ref,
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

static size_t cxpr_model_c_child_index_for_entry(const cxpr_model_compiled* program,
                                                 const cxpr_func_entry* entry) {
    if (!program || !entry || !entry->model_producer_userdata) return (size_t)-1;
    for (size_t i = 0u; i < program->child_count; ++i) {
        if (&program->children[i] == entry->model_producer_userdata) return i;
    }
    return (size_t)-1;
}

static char* cxpr_model_c_child_call_key(const cxpr_expr_ast* ast) {
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

static size_t cxpr_model_c_child_call_index_for_key(
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

char* cxpr_model_ast_producer_access_to_c(const cxpr_model_compiled* program,
                                                 const cxpr_expr_ast* ast,
                                                 const char* function_prefix,
                                                 const double* literal_param_values,
                                                 size_t literal_param_count,
                                                 char** child_call_keys,
                                                 size_t* child_call_child_indices,
                                                 size_t child_call_count,
                                                 cxpr_error* err) {
    cxpr_func_entry* entry;
    const char* producer;
    const char* field;
    size_t selected_field = (size_t)-1;
    char** arg_exprs = NULL;
    char** field_exprs = NULL;
    cxpr_model_ast_c_target target_data;
    cxpr_c_target target;

    if (!program || !program->registry || !ast ||
        cxpr_expr_ast_kind_of(ast) != CXPR_NODE_PRODUCER_ACCESS) {
        return NULL;
    }
    producer = cxpr_expr_ast_producer_name(ast);
    field = cxpr_expr_ast_producer_field(ast);
    entry = cxpr_registry_find(program->registry, producer);
    if (!entry || (!entry->model_producer && entry->defined_return_field_count == 0u &&
                   !entry->struct_codegen) ||
        (!entry->model_producer && !entry->struct_codegen &&
         entry->defined_param_count != cxpr_expr_ast_producer_arg_count(ast)) ||
        (entry->model_producer &&
         (cxpr_expr_ast_producer_arg_count(ast) < entry->min_args ||
          cxpr_expr_ast_producer_arg_count(ast) >
              entry->max_args +
                  (((const cxpr_model_child_program*)entry->model_producer_userdata)->program
                       ? ((const cxpr_model_child_program*)entry->model_producer_userdata)->program->input_count
                       : 0u)))) {
        if (err) {
            static CXPR_THREAD_LOCAL char message[256];
            const cxpr_model_child_program* child_ref =
                entry && entry->model_producer_userdata
                    ? (const cxpr_model_child_program*)entry->model_producer_userdata
                    : NULL;
            err->code = CXPR_ERR_UNKNOWN_FUNCTION;
            snprintf(message, sizeof(message),
                     "Unsupported model C producer access '%s.%s' (child=%s argc=%lu min=%lu max=%lu)",
                     producer ? producer : "?", field ? field : "?",
                     child_ref && child_ref->name ? child_ref->name : "none",
                     (unsigned long)cxpr_expr_ast_producer_arg_count(ast),
                     (unsigned long)(entry ? entry->min_args : 0u),
                     (unsigned long)(entry ? entry->max_args : 0u));
            err->message = message;
        }
        return NULL;
    }
    for (size_t i = 0u; i < entry->defined_return_field_count; ++i) {
        if (entry->defined_return_field_names[i] &&
            field &&
            strcmp(entry->defined_return_field_names[i], field) == 0) {
            selected_field = i;
            break;
        }
    }
    if (selected_field == (size_t)-1 && entry->struct_codegen) {
        for (size_t i = 0u; i < entry->fields_per_arg; ++i) {
            if (entry->struct_fields[i] && field &&
                cxpr_model_names_match(entry->struct_fields[i], field)) {
                selected_field = i;
                break;
            }
        }
    }
    if (selected_field == (size_t)-1 && field && producer &&
        cxpr_model_names_match(producer, "swing_pivots")) {
        const char* canonical = cxpr_model_names_match(field, "high")
            ? "pivot_high"
            : (cxpr_model_names_match(field, "low")
                   ? "pivot_low"
                   : (cxpr_model_names_match(field, "line") ? "swing_line" : NULL));
        for (size_t i = 0u; canonical && i < entry->defined_return_field_count; ++i) {
            if (entry->defined_return_field_names[i] &&
                cxpr_model_names_match(entry->defined_return_field_names[i], canonical)) {
                selected_field = i;
                break;
            }
        }
    }
    if (selected_field == (size_t)-1) {
        if (err) {
            static CXPR_THREAD_LOCAL char message[256];
            err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
            snprintf(message, sizeof(message), "Unknown producer field '%s.%s'",
                     producer ? producer : "?", field ? field : "?");
            err->message = message;
        }
        return NULL;
    }

    if (entry->struct_codegen) {
        const size_t argc = cxpr_expr_ast_producer_arg_count(ast);
        char* expression;
        arg_exprs = (char**)calloc(argc ? argc : 1u, sizeof(char*));
        if (!arg_exprs) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        target_data = (cxpr_model_ast_c_target){
            .program = program,
            .function_prefix = function_prefix,
            .literal_param_values = literal_param_values,
            .literal_param_count = literal_param_count,
            .child_call_keys = child_call_keys,
            .child_call_child_indices = child_call_child_indices,
            .child_call_count = child_call_count,
        };
        target = (cxpr_c_target){
            .api_version = CXPR_C_TARGET_API_VERSION,
            .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
            .emit_call_at_offset = cxpr_model_ast_c_emit_call,
            .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
            .userdata = &target_data,
        };
        for (size_t i = 0u; i < argc; ++i) {
            arg_exprs[i] = cxpr_expr_ast_to_c_at_offset(
                cxpr_expr_ast_producer_arg(ast, i), 0u, &target, err);
            if (!arg_exprs[i]) {
                for (size_t j = 0u; j < i; ++j) free(arg_exprs[j]);
                free(arg_exprs);
                return NULL;
            }
        }
        expression = entry->struct_codegen(
            field, (const char* const*)arg_exprs, argc,
            entry->struct_codegen_userdata, err);
        for (size_t i = 0u; i < argc; ++i) free(arg_exprs[i]);
        free(arg_exprs);
        return expression;
    }

    if (entry->model_producer) {
        size_t child_index = cxpr_model_c_child_index_for_entry(program, entry);
        const cxpr_model_compiled* child =
            child_index == (size_t)-1 ? NULL : program->children[child_index].program;
        size_t child_call_index;
        char* child_call_key;
        char* helper_name;
        cxpr_model_c_buf call = {0};
        if (!child) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Unknown child model producer", 0, 0);
            return NULL;
        }
        child_call_key = cxpr_model_c_child_call_key(ast);
        if (!child_call_key) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        child_call_index = cxpr_model_c_child_call_index_for_key(
            child_call_keys,
            child_call_child_indices,
            child_call_count,
            child_index,
            child_call_key);
        free(child_call_key);
        if (child_call_index == (size_t)-1) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Unknown child model callsite", 0, 0);
            return NULL;
        }
        (void)producer;
        helper_name = cxpr_model_c_child_field_name(function_prefix, child_index, selected_field);
        if (!helper_name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        cxpr_model_c_printf(&call,
                            "%s(&_cx_state->child_call_%zu_initialized, _cx_state->child_call_%zu_outputs, &_cx_state->child_call_%zu_state",
                            helper_name,
                            child_call_index,
                            child_call_index,
                            child_call_index);
        free(helper_name);
        for (size_t i = 0u; i < child->input_count; ++i) {
            size_t input_index = 0u;
            const cxpr_expr_ast* source_arg =
                (i == program->children[child_index].source_input_index)
                    ? cxpr_model_child_call_source_arg(&program->children[child_index], child, ast)
                    : NULL;
            char* source_expr = NULL;
            target_data = (cxpr_model_ast_c_target){
                .program = program,
                .function_prefix = function_prefix,
                .literal_param_values = literal_param_values,
                .literal_param_count = literal_param_count,
                .child_call_keys = child_call_keys,
                .child_call_child_indices = child_call_child_indices,
                .child_call_count = child_call_count,
            };
            target = (cxpr_c_target){
                .api_version = CXPR_C_TARGET_API_VERSION,
                .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
                .emit_call_at_offset = cxpr_model_ast_c_emit_call,
                .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
                .userdata = &target_data,
            };
            if (source_arg) {
                source_expr = cxpr_expr_ast_to_c_at_offset(source_arg, 0u, &target, err);
                if (!source_expr) {
                    free(call.data);
                    return NULL;
                }
                cxpr_model_c_printf(&call, ", %s", source_expr);
                free(source_expr);
                continue;
            }
            if (!cxpr_model_c_symbol_is_input(program, child->inputs[i], &input_index)) {
                free(call.data);
                cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                     "Child model input is not a parent input", 0, 0);
                return NULL;
            }
            cxpr_model_c_printf(&call, ", _cx_input_%zu", input_index);
        }
        target_data = (cxpr_model_ast_c_target){
            .program = program,
            .function_prefix = function_prefix,
            .literal_param_values = literal_param_values,
            .literal_param_count = literal_param_count,
            .child_call_keys = child_call_keys,
            .child_call_child_indices = child_call_child_indices,
            .child_call_count = child_call_count,
        };
        target = (cxpr_c_target){
            .api_version = CXPR_C_TARGET_API_VERSION,
            .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
            .emit_call_at_offset = cxpr_model_ast_c_emit_call,
            .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
            .userdata = &target_data,
        };
        for (size_t i = 0u; i < child->constant_count; ++i) {
            const cxpr_expr_ast* arg = cxpr_model_child_call_param_arg(
                &program->children[child_index], child, ast, i);
            char* arg_expr;
            if (!arg && child->constants[i].ast) {
                arg = child->constants[i].ast;
            }
            if (!arg) {
                free(call.data);
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Missing producer argument", 0, 0);
                return NULL;
            }
            arg_expr = cxpr_expr_ast_to_c_at_offset(arg, 0u, &target, err);
            if (!arg_expr) {
                free(call.data);
                return NULL;
            }
            cxpr_model_c_printf(&call, ", %s", arg_expr);
            free(arg_expr);
        }
        cxpr_model_c_puts(&call, ")");
        if (call.oom) {
            free(call.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        return call.data;
    }

    arg_exprs = (char**)calloc(entry->defined_param_count ? entry->defined_param_count : 1u,
                              sizeof(char*));
    if (!arg_exprs) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    target_data = (cxpr_model_ast_c_target){
            .program = program,
            .function_prefix = function_prefix,
            .literal_param_values = literal_param_values,
            .literal_param_count = literal_param_count,
            .child_call_keys = child_call_keys,
            .child_call_child_indices = child_call_child_indices,
            .child_call_count = child_call_count,
        };
    target = (cxpr_c_target){
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
        .userdata = &target_data,
    };
    for (size_t i = 0u; i < entry->defined_param_count; ++i) {
        const cxpr_expr_ast* arg = cxpr_model_producer_arg_for_param(
            ast, entry->defined_param_names ? entry->defined_param_names[i] : NULL, i);
        if (!arg) {
            if (err) {
                err->code = CXPR_ERR_SYNTAX;
                err->message = "Missing producer argument";
            }
            goto fail;
        }
        arg_exprs[i] = cxpr_expr_ast_to_c_at_offset(arg, 0u, &target, err);
        if (!arg_exprs[i]) goto fail;
    }

    field_exprs = (char**)calloc(entry->defined_return_field_count, sizeof(char*));
    if (!field_exprs) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        goto fail;
    }
    for (size_t field_i = 0u; field_i <= selected_field; ++field_i) {
        const size_t name_count = entry->defined_param_count + field_i;
        char** names = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        char** exprs = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        if (!names || !exprs) {
            free(names);
            free(exprs);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            goto fail;
        }
        for (size_t i = 0u; i < entry->defined_param_count; ++i) {
            names[i] = entry->defined_param_names[i];
            exprs[i] = arg_exprs[i];
        }
        for (size_t i = 0u; i < field_i; ++i) {
            names[entry->defined_param_count + i] = entry->defined_return_field_names[i];
            exprs[entry->defined_param_count + i] = field_exprs[i];
        }
        target_data.param_names = names;
        target_data.param_exprs = exprs;
        target_data.param_count = name_count;
        field_exprs[field_i] = cxpr_expr_ast_to_c_at_offset(
            entry->defined_return_field_bodies[field_i], 0u, &target, err);
        free(names);
        free(exprs);
        if (!field_exprs[field_i]) goto fail;
    }
    {
        char* out = field_exprs[selected_field];
        field_exprs[selected_field] = NULL;
        for (size_t i = 0u; i < entry->defined_return_field_count; ++i) free(field_exprs[i]);
        free(field_exprs);
        for (size_t i = 0u; i < entry->defined_param_count; ++i) free(arg_exprs[i]);
        free(arg_exprs);
        return out;
    }

fail:
    if (field_exprs) {
        for (size_t i = 0u; i < entry->defined_return_field_count; ++i) free(field_exprs[i]);
        free(field_exprs);
    }
    for (size_t i = 0u; i < entry->defined_param_count; ++i) free(arg_exprs[i]);
    free(arg_exprs);
    return NULL;
}

static size_t cxpr_model_c_state_slot_for_name(const cxpr_model_compiled* program,
                                               const char* name) {
    if (!program || !name) return (size_t)-1;
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        if (cxpr_model_names_match(program->state_defaults[i].name, name)) return i;
    }
    return (size_t)-1;
}

static size_t cxpr_model_c_state_slot_for_fused_slot(const cxpr_model_compiled* program,
                                                     size_t fused_slot) {
    if (!program || fused_slot >= program->fused_slot_count) return (size_t)-1;
    return cxpr_model_c_state_slot_for_name(program, program->fused_slot_names[fused_slot]);
}

static size_t CXPR_MODEL_MAYBE_UNUSED
cxpr_model_c_history_base(const cxpr_model_compiled* program,
                          size_t history_index) {
    (void)program;
    (void)history_index;
    return (size_t)-1;
}

static size_t cxpr_model_c_history_find(const cxpr_model_compiled* program,
                                        const char* name) {
    const char* dot;
    if (!program || !name) return (size_t)-1;
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        if (cxpr_model_names_match(program->history_specs[i].name, name)) return i;
    }
    dot = strchr(name, '.');
    if (dot && dot != name) {
        char root[128];
        size_t root_len = (size_t)(dot - name);
        if (root_len < sizeof(root)) {
            const cxpr_model_compiled_binding* binding;
            char* expression;
            char* expanded;
            size_t expression_len;
            memcpy(root, name, root_len);
            root[root_len] = '\0';
            binding = cxpr_model_c_binding_for_name(program, root);
            expression = binding && binding->ast
                ? cxpr_expr_ast_to_string(binding->ast) : NULL;
            expression_len = expression ? strlen(expression) : 0u;
            expanded = expression
                ? (char*)malloc(expression_len + strlen(dot) + 1u) : NULL;
            if (expanded) {
                memcpy(expanded, expression, expression_len);
                strcpy(expanded + expression_len, dot);
                for (size_t i = 0u; i < program->history_spec_count; ++i) {
                    if (cxpr_model_names_match(
                            program->history_specs[i].name, expanded)) {
                        free(expanded);
                        free(expression);
                        return i;
                    }
                }
                free(expanded);
            }
            free(expression);
        }
    }
    return (size_t)-1;
}

char* cxpr_model_c_current_symbol_expr(const cxpr_model_compiled* program,
                                              char** state_next_names,
                                              const char* name,
                                              cxpr_error* err) {
    size_t index = 0u;
    size_t slot = 0u;
    if (!program || !name) return NULL;
    if (cxpr_model_c_symbol_is_input(program, name, &index)) {
        char raw[64];
        snprintf(raw, sizeof(raw), "_cx_input_%zu", index);
        return cxpr_strdup(raw);
    }
    if (cxpr_model_c_symbol_is_state(program, name, &slot)) {
        size_t c_slot = cxpr_model_c_state_slot_for_fused_slot(program, slot);
        if (c_slot == (size_t)-1) return NULL;
        (void)state_next_names;
        (void)c_slot;
        return cxpr_model_c_prefixed_name("_cx_state_", name);
    }
    if (cxpr_model_c_symbol_is_binding(program, name)) {
        return cxpr_model_c_safe_name(name);
    }
    if (err) {
        static CXPR_THREAD_LOCAL char message[256];
        err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
        snprintf(message, sizeof(message),
                 "Unknown model C history target '%s'", name);
        err->message = message;
    }
    return NULL;
}

bool cxpr_model_c_emit_dynamic_history_value(cxpr_model_c_buf* b,
                                             const char* value_name,
                                             const cxpr_expr_ast* ast,
                                             const char* offset_expr,
                                             const cxpr_c_target* target,
                                             const cxpr_model_compiled* program,
                                             cxpr_error* err) {
    char* current_expr = NULL;
    char* key = NULL;
    size_t hist_index;
    size_t depth;
    size_t capacity;

    if (!b || !value_name || !ast || !offset_expr || !target || !program) return false;
    if (!cxpr_model_lookback_target_key(ast, &key, err)) return false;
    hist_index = cxpr_model_c_history_find(program, key);
    free(key);
    if (hist_index == (size_t)-1) return false;
    current_expr = cxpr_expr_ast_to_c_at_offset(ast, 0u, target, err);
    if (!current_expr) return false;
    depth = program->history_specs[hist_index].depth;
    capacity = cxpr_model_c_history_capacity(depth);
    cxpr_model_c_printf(
        b,
        "            double %s; if ((%s) == 0u) { %s = %s; } else if ((%s) <= %zuu) { ",
        value_name,
        offset_expr,
        value_name,
        current_expr,
        offset_expr,
        depth);
    free(current_expr);
    if (cxpr_model_c_history_use_shift(depth)) {
        cxpr_model_c_printf(
            b,
            "%s = _cx_state->history_%zu.values[(%s) - 1u];",
            value_name,
            hist_index,
            offset_expr);
    } else if (cxpr_model_c_is_power_of_two(capacity)) {
        cxpr_model_c_printf(
            b,
            "%s = _cx_state->history_%zu.values[(_cx_history_next_%zu + %zuu - (%s)) & %zuu];",
            value_name,
            hist_index,
            hist_index,
            capacity,
            offset_expr,
            capacity - 1u);
    } else {
        cxpr_model_c_printf(
            b,
            "%s = _cx_state->history_%zu.values[(_cx_history_next_%zu + %zuu - (%s)) %% %zuu];",
            value_name,
            hist_index,
            hist_index,
            capacity,
            offset_expr,
            capacity);
    }
    cxpr_model_c_printf(b, " } else { %s = NAN; }\n", value_name);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

static const char* cxpr_model_c_ast_binary_op(int op) {
    switch (op) {
    case CXPR_TOK_PLUS: return "+";
    case CXPR_TOK_MINUS: return "-";
    case CXPR_TOK_STAR: return "*";
    case CXPR_TOK_SLASH: return "/";
    case CXPR_TOK_EQ: return "==";
    case CXPR_TOK_NEQ: return "!=";
    case CXPR_TOK_LT: return "<";
    case CXPR_TOK_GT: return ">";
    case CXPR_TOK_LTE: return "<=";
    case CXPR_TOK_GTE: return ">=";
    case CXPR_TOK_AND: return "&&";
    case CXPR_TOK_OR: return "||";
    default: return NULL;
    }
}

bool cxpr_model_ast_is_record_like(const cxpr_model_compiled* program,
                                          const cxpr_expr_ast* ast,
                                          unsigned depth) {
    cxpr_func_entry* entry;
    const cxpr_model_compiled_binding* binding;

    if (!program || !ast || depth > 32u) return false;
    switch (cxpr_expr_ast_kind_of(ast)) {
    case CXPR_NODE_RECORD:
        return true;
    case CXPR_NODE_IDENTIFIER:
        binding = cxpr_model_c_binding_for_name(program, cxpr_expr_ast_identifier_name(ast));
        return binding ? cxpr_model_ast_is_record_like(program, binding->ast, depth + 1u) : false;
    case CXPR_NODE_FUNCTION_CALL:
        entry = program->registry
                    ? cxpr_registry_find(program->registry, cxpr_expr_ast_call_name(ast))
                    : NULL;
        return entry && entry->defined_return_field_count > 1u;
    default:
        return false;
    }
}

static char* cxpr_model_ast_field_expr_try(const cxpr_model_compiled* program,
                                           const cxpr_expr_ast* ast,
                                           const char* field,
                                           const cxpr_c_target* target,
                                           unsigned depth) {
    cxpr_error ignored = {0};
    return cxpr_model_ast_field_expr_to_c(program, ast, field, target, &ignored, depth);
}

static char* cxpr_model_ast_record_function_field_to_c(
    const cxpr_model_compiled* program,
    const cxpr_expr_ast* ast,
    const char* field,
    const cxpr_c_target* target,
    cxpr_error* err,
    unsigned depth) {
    cxpr_func_entry* entry;
    size_t selected_field = (size_t)-1;
    char** arg_exprs = NULL;
    char** field_exprs = NULL;
    cxpr_model_ast_c_target* target_data = target ? (cxpr_model_ast_c_target*)target->userdata : NULL;
    cxpr_model_ast_c_target inline_data;
    cxpr_c_target inline_target;

    if (!program || !program->registry || !ast ||
        cxpr_expr_ast_kind_of(ast) != CXPR_NODE_FUNCTION_CALL || !field || depth > 32u) {
        return NULL;
    }
    entry = cxpr_registry_find(program->registry, cxpr_expr_ast_call_name(ast));
    if (!entry || entry->defined_return_field_count == 0u ||
        (!entry->model_producer &&
         entry->defined_param_count != cxpr_expr_ast_call_arg_count(ast))) {
        return NULL;
    }
    for (size_t i = 0u; i < entry->defined_return_field_count; ++i) {
        if (entry->defined_return_field_names[i] &&
            cxpr_model_names_match(entry->defined_return_field_names[i], field)) {
            selected_field = i;
            break;
        }
    }
    if (selected_field == (size_t)-1) return NULL;

    if (entry->model_producer) {
        cxpr_expr_ast producer = {0};
        producer.type = CXPR_NODE_PRODUCER_ACCESS;
        producer.data.producer_access.name = ast->data.function_call.name;
        producer.data.producer_access.args = ast->data.function_call.args;
        producer.data.producer_access.arg_names = ast->data.function_call.arg_names;
        producer.data.producer_access.argc = ast->data.function_call.argc;
        producer.data.producer_access.field = (char*)field;
        return cxpr_model_ast_producer_access_to_c(
            program,
            &producer,
            target_data ? target_data->function_prefix : NULL,
            target_data ? target_data->literal_param_values : NULL,
            target_data ? target_data->literal_param_count : 0u,
            target_data ? target_data->child_call_keys : NULL,
            target_data ? target_data->child_call_child_indices : NULL,
            target_data ? target_data->child_call_count : 0u,
            err);
    }

    arg_exprs = (char**)calloc(entry->defined_param_count ? entry->defined_param_count : 1u,
                               sizeof(char*));
    field_exprs = (char**)calloc(entry->defined_return_field_count, sizeof(char*));
    if (!arg_exprs || !field_exprs) {
        free(arg_exprs);
        free(field_exprs);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }

    for (size_t i = 0u; i < entry->defined_param_count; ++i) {
        arg_exprs[i] = cxpr_expr_ast_to_c(cxpr_expr_ast_call_arg(ast, i), target, err);
        if (!arg_exprs[i]) goto fail;
    }

    inline_data = target_data ? *target_data : (cxpr_model_ast_c_target){0};
    inline_target = *target;
    inline_target.userdata = &inline_data;
    for (size_t field_i = 0u; field_i <= selected_field; ++field_i) {
        const size_t name_count = entry->defined_param_count + field_i;
        char** names = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        char** exprs = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        if (!names || !exprs) {
            free(names);
            free(exprs);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            goto fail;
        }
        for (size_t i = 0u; i < entry->defined_param_count; ++i) {
            names[i] = entry->defined_param_names[i];
            exprs[i] = arg_exprs[i];
        }
        for (size_t i = 0u; i < field_i; ++i) {
            names[entry->defined_param_count + i] = entry->defined_return_field_names[i];
            exprs[entry->defined_param_count + i] = field_exprs[i];
        }
        inline_data.param_names = names;
        inline_data.param_exprs = exprs;
        inline_data.param_count = name_count;
        field_exprs[field_i] = cxpr_expr_ast_to_c(
            entry->defined_return_field_bodies[field_i], &inline_target, err);
        free(names);
        free(exprs);
        if (!field_exprs[field_i]) goto fail;
    }

    {
        char* out = field_exprs[selected_field];
        field_exprs[selected_field] = NULL;
        for (size_t i = 0u; i < entry->defined_return_field_count; ++i) free(field_exprs[i]);
        for (size_t i = 0u; i < entry->defined_param_count; ++i) free(arg_exprs[i]);
        free(field_exprs);
        free(arg_exprs);
        return out;
    }

fail:
    if (field_exprs) {
        for (size_t i = 0u; i < entry->defined_return_field_count; ++i) free(field_exprs[i]);
    }
    if (arg_exprs) {
        for (size_t i = 0u; i < entry->defined_param_count; ++i) free(arg_exprs[i]);
    }
    free(field_exprs);
    free(arg_exprs);
    return NULL;
}

static char* cxpr_model_ast_record_field_to_c(
    const cxpr_model_compiled* program,
    const cxpr_expr_ast* ast,
    const char* field,
    const cxpr_c_target* target,
    cxpr_error* err,
    unsigned depth) {
    size_t selected_field = (size_t)-1;
    const size_t field_count = cxpr_expr_ast_record_field_count(ast);
    const char* dot = strchr(field, '.');
    const size_t segment_len = dot ? (size_t)(dot - field) : strlen(field);
    const char* rest = dot ? dot + 1 : NULL;
    char** field_exprs = NULL;
    cxpr_model_ast_c_target* target_data = target ? (cxpr_model_ast_c_target*)target->userdata : NULL;
    cxpr_model_ast_c_target inline_data;
    cxpr_c_target inline_target;

    if (!program || !ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_RECORD ||
        !field || !target || depth > 32u) {
        return NULL;
    }
    if (segment_len == 0u || (rest && rest[0] == '\0')) return NULL;
    for (size_t i = 0u; i < field_count; ++i) {
        const char* field_name = cxpr_expr_ast_record_field_name(ast, i);
        if (field_name &&
            strlen(field_name) == segment_len &&
            strncmp(field_name, field, segment_len) == 0) {
            selected_field = i;
            break;
        }
    }
    if (selected_field == (size_t)-1) return NULL;

    field_exprs = (char**)calloc(field_count ? field_count : 1u, sizeof(char*));
    if (!field_exprs) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }

    inline_data = target_data ? *target_data : (cxpr_model_ast_c_target){0};
    inline_target = *target;
    inline_target.userdata = &inline_data;
    for (size_t field_i = 0u; field_i <= selected_field; ++field_i) {
        const size_t base_count = target_data ? target_data->param_count : 0u;
        const size_t name_count = base_count + field_i;
        char** names = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        char** exprs = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        if (!names || !exprs) {
            free(names);
            free(exprs);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            goto fail;
        }
        for (size_t i = 0u; i < base_count; ++i) {
            names[i] = target_data->param_names[i];
            exprs[i] = target_data->param_exprs ? target_data->param_exprs[i] : NULL;
        }
        for (size_t i = 0u; i < field_i; ++i) {
            names[base_count + i] = (char*)cxpr_expr_ast_record_field_name(ast, i);
            exprs[base_count + i] = field_exprs[i];
        }
        inline_data.param_names = names;
        inline_data.param_exprs = exprs;
        inline_data.param_count = name_count;
        if (field_i == selected_field && rest) {
            field_exprs[field_i] = cxpr_model_ast_field_expr_to_c(
                program,
                cxpr_expr_ast_record_field_value(ast, field_i),
                rest,
                &inline_target,
                err,
                depth + 1u);
        } else {
            field_exprs[field_i] = cxpr_expr_ast_to_c(
                cxpr_expr_ast_record_field_value(ast, field_i), &inline_target, err);
        }
        free(names);
        free(exprs);
        if (!field_exprs[field_i]) goto fail;
    }

    {
        char* out = field_exprs[selected_field];
        field_exprs[selected_field] = NULL;
        for (size_t i = 0u; i < field_count; ++i) free(field_exprs[i]);
        free(field_exprs);
        return out;
    }

fail:
    for (size_t i = 0u; i < field_count; ++i) free(field_exprs[i]);
    free(field_exprs);
    return NULL;
}

static char* cxpr_model_ast_field_expr_to_c(const cxpr_model_compiled* program,
                                            const cxpr_expr_ast* ast,
                                            const char* field,
                                            const cxpr_c_target* target,
                                            cxpr_error* err,
                                            unsigned depth) {
    if (!program || !ast || !field || !target || depth > 32u) return NULL;

    switch (cxpr_expr_ast_kind_of(ast)) {
    case CXPR_NODE_RECORD:
        return cxpr_model_ast_record_field_to_c(program, ast, field, target, err, depth + 1u);
    case CXPR_NODE_IDENTIFIER: {
        const cxpr_model_compiled_binding* binding =
            cxpr_model_c_binding_for_name(program, cxpr_expr_ast_identifier_name(ast));
        return binding
                   ? cxpr_model_ast_field_expr_to_c(
                         program, binding->ast, field, target, err, depth + 1u)
                   : NULL;
    }
    case CXPR_NODE_VARIABLE: {
        const char* variable_name = cxpr_expr_ast_param_name(ast);
        const char* dot = variable_name ? strchr(variable_name, '.') : NULL;
        if (dot && dot > variable_name && dot[1] != '\0') {
            size_t root_len = (size_t)(dot - variable_name);
            char* root = (char*)malloc(root_len + 1u);
            if (!root) return NULL;
            memcpy(root, variable_name, root_len);
            root[root_len] = '\0';
            const cxpr_model_compiled_binding* dotted_constant =
                cxpr_model_c_constant_for_name(program, root);
            free(root);
            if (dotted_constant) {
                return cxpr_model_ast_field_expr_to_c(
                    program, dotted_constant->ast, dot + 1, target, err, depth + 1u);
            }
        }
        const cxpr_model_compiled_binding* constant =
            cxpr_model_c_constant_for_name(program, variable_name);
        return constant
                   ? cxpr_model_ast_field_expr_to_c(
                         program, constant->ast, field, target, err, depth + 1u)
                   : NULL;
    }
    case CXPR_NODE_FUNCTION_CALL: {
        char* expression = cxpr_model_ast_record_function_field_to_c(
            program, ast, field, target, err, depth + 1u);
        cxpr_model_ast_c_target* target_data;
        cxpr_expr_ast producer = {0};
        if (expression) return expression;
        target_data = (cxpr_model_ast_c_target*)target->userdata;
        producer.type = CXPR_NODE_PRODUCER_ACCESS;
        producer.data.producer_access.name = ast->data.function_call.name;
        producer.data.producer_access.args = ast->data.function_call.args;
        producer.data.producer_access.arg_names = ast->data.function_call.arg_names;
        producer.data.producer_access.argc = ast->data.function_call.argc;
        producer.data.producer_access.field = (char*)field;
        return cxpr_model_ast_producer_access_to_c(
            program, &producer,
            target_data ? target_data->function_prefix : NULL,
            target_data ? target_data->literal_param_values : NULL,
            target_data ? target_data->literal_param_count : 0u,
            target_data ? target_data->child_call_keys : NULL,
            target_data ? target_data->child_call_child_indices : NULL,
            target_data ? target_data->child_call_count : 0u,
            err);
    }
    case CXPR_NODE_BINARY_OP: {
        const char* op = cxpr_model_c_ast_binary_op(cxpr_expr_ast_operator(ast));
        char* left_field;
        char* right_field;
        char* left;
        char* right;
        cxpr_model_c_buf b = {0};
        if (!op) return NULL;
        left_field = cxpr_model_ast_field_expr_try(
            program, cxpr_expr_ast_binary_left(ast), field, target, depth + 1u);
        right_field = cxpr_model_ast_field_expr_try(
            program, cxpr_expr_ast_binary_right(ast), field, target, depth + 1u);
        if (left_field) {
            left = left_field;
        } else {
            left = cxpr_expr_ast_to_c(cxpr_expr_ast_binary_left(ast), target, err);
        }
        if (right_field) {
            right = right_field;
        } else {
            right = cxpr_expr_ast_to_c(cxpr_expr_ast_binary_right(ast), target, err);
        }
        if (!left || !right) {
            free(left);
            free(right);
            return NULL;
        }
        cxpr_model_c_printf(&b, "(%s %s %s)", left, op, right);
        free(left);
        free(right);
        if (b.oom) {
            free(b.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        return b.data;
    }
    default:
        return NULL;
    }
}

char* cxpr_model_ast_c_emit_leaf(const cxpr_expr_ast* ast,
                                        unsigned lookback_offset,
                                        void* userdata,
                                        cxpr_error* err) {
    cxpr_model_ast_c_target* target = (cxpr_model_ast_c_target*)userdata;
    const cxpr_model_compiled* program = target ? target->program : NULL;
    const char* name = NULL;
    size_t index = 0u;
    size_t slot = 0u;

    if (!program || !ast) return NULL;
    if (lookback_offset != 0u) {
        size_t hist_index;
        size_t depth;
        size_t capacity;
        char* key = NULL;
        if (!cxpr_model_lookback_target_key(ast, &key, err)) return NULL;
        name = key;
        hist_index = cxpr_model_c_history_find(program, name);
        if (hist_index == (size_t)-1) {
            if (err) {
                static CXPR_THREAD_LOCAL char message[256];
                size_t used;
                err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                used = (size_t)snprintf(message, sizeof(message),
                    "Unknown model C history target '%s' (available:", name);
                for (size_t i = 0u; i < program->history_spec_count &&
                     used + 4u < sizeof(message); ++i) {
                    int n = snprintf(message + used, sizeof(message) - used,
                        "%s%s", i ? "," : "", program->history_specs[i].name);
                    if (n < 0 || (size_t)n >= sizeof(message) - used) break;
                    used += (size_t)n;
                }
                if (used + 2u < sizeof(message)) strcpy(message + used, ")");
                err->message = message;
            }
            free(key);
            return NULL;
        }
        depth = program->history_specs[hist_index].depth;
        if (depth == 0u || lookback_offset == 0u || lookback_offset > depth) {
            char raw[32];
            free(key);
            snprintf(raw, sizeof(raw), "NAN");
            return cxpr_strdup(raw);
        }
        capacity = cxpr_model_c_history_capacity(depth);
        free(key);
        {
            char raw[256];
            char index_expr[128];
            size_t delta = capacity - (size_t)lookback_offset;
            if (cxpr_model_c_history_use_shift(depth)) {
                snprintf(raw, sizeof(raw),
                         "_cx_state->history_%zu.values[%zu]",
                         hist_index,
                         (size_t)lookback_offset - 1u);
                return cxpr_strdup(raw);
            }
            if (capacity == 1u) {
                snprintf(index_expr, sizeof(index_expr), "0u");
            } else if (delta == 0u) {
                snprintf(index_expr, sizeof(index_expr), "_cx_history_next_%zu", hist_index);
            } else if (cxpr_model_c_is_power_of_two(capacity)) {
                snprintf(index_expr, sizeof(index_expr),
                         "((_cx_history_next_%zu + %zuu) & %zuu)",
                         hist_index, delta, capacity - 1u);
            } else {
                snprintf(index_expr, sizeof(index_expr),
                         "((_cx_history_next_%zu + %zuu) %% %zuu)",
                         hist_index, delta, capacity);
            }
            snprintf(raw, sizeof(raw),
                     "_cx_state->history_%zu.values[%s]",
                     hist_index,
                     index_expr);
            return cxpr_strdup(raw);
        }
    }
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_IDENTIFIER) {
        name = cxpr_expr_ast_identifier_name(ast);
        for (size_t i = 0u; target && i < target->param_count; ++i) {
            if (cxpr_model_names_match(target->param_names[i], name)) {
                if (target->param_exprs && target->param_exprs[i]) {
                    return cxpr_strdup(target->param_exprs[i]);
                }
                return cxpr_model_c_safe_name(name);
            }
        }
        if (cxpr_model_c_symbol_is_input(program, name, &index)) {
            char raw[64];
            snprintf(raw, sizeof(raw), "_cx_input_%zu", index);
            return cxpr_strdup(raw);
        }
        if (cxpr_model_c_symbol_is_state(program, name, &slot)) {
            return cxpr_model_c_prefixed_name("_cx_state_", name);
        }
        if (cxpr_model_c_symbol_is_binding(program, name)) {
            return cxpr_model_c_safe_name(name);
        }
        if (err) {
            static CXPR_THREAD_LOCAL char message[256];
            err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
            snprintf(message, sizeof(message),
                     "Unknown model C identifier '%s'", name ? name : "?");
            err->message = message;
        }
        return NULL;
    }
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS) {
        return cxpr_model_ast_producer_access_to_c(
            program,
            ast,
            target ? target->function_prefix : NULL,
            target ? target->literal_param_values : NULL,
            target ? target->literal_param_count : 0u,
            target ? target->child_call_keys : NULL,
            target ? target->child_call_child_indices : NULL,
            target ? target->child_call_count : 0u,
            err);
    }
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FIELD_ACCESS) {
        cxpr_c_target field_target = {
            .api_version = CXPR_C_TARGET_API_VERSION,
            .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
            .emit_call_at_offset = cxpr_model_ast_c_emit_call,
            .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
            .userdata = target,
        };
        if (ast->data.field_access.base) {
            char* expr = cxpr_model_ast_field_expr_to_c(
                program, ast->data.field_access.base, ast->data.field_access.field, &field_target, err, 0u);
            if (expr) return expr;
        }
        const cxpr_model_compiled_binding* binding =
            cxpr_model_c_binding_for_name(program, ast->data.field_access.object);
        if (binding) {
            char* expr = cxpr_model_ast_field_expr_to_c(
                program, binding->ast, ast->data.field_access.field, &field_target, err, 0u);
            if (expr) return expr;
        }
        const cxpr_model_compiled_binding* constant =
            cxpr_model_c_constant_for_name(program, ast->data.field_access.object);
        if (constant) {
            char* expr = cxpr_model_ast_field_expr_to_c(
                program, constant->ast, ast->data.field_access.field, &field_target, err, 0u);
            if (expr) return expr;
        }
        if (!ast->data.field_access.base && ast->data.field_access.object &&
            ast->data.field_access.field) {
            char qualified[256];
            int written = snprintf(qualified, sizeof(qualified), "%s.%s",
                                   ast->data.field_access.object,
                                   ast->data.field_access.field);
            if (written > 0 && (size_t)written < sizeof(qualified) &&
                cxpr_model_c_symbol_is_input(program, qualified, &index)) {
                char raw[64];
                snprintf(raw, sizeof(raw), "_cx_input_%zu", index);
                return cxpr_strdup(raw);
            }
        }
    }
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_VARIABLE) {
        name = cxpr_expr_ast_param_name(ast);
        const char* dot = name ? strchr(name, '.') : NULL;
        if (dot && dot > name && dot[1] != '\0') {
            size_t root_len = (size_t)(dot - name);
            char* root = (char*)malloc(root_len + 1u);
            if (!root) return NULL;
            memcpy(root, name, root_len);
            root[root_len] = '\0';
            const cxpr_model_compiled_binding* constant =
                cxpr_model_c_constant_for_name(program, root);
            free(root);
            if (constant) {
                cxpr_c_target field_target = {
                    .api_version = CXPR_C_TARGET_API_VERSION,
                    .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
                    .emit_call_at_offset = cxpr_model_ast_c_emit_call,
                    .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
                    .userdata = target,
                };
                char* expr = cxpr_model_ast_field_expr_to_c(
                    program, constant->ast, dot + 1, &field_target, err, 0u);
                if (expr) return expr;
            }
        }
        index = cxpr_model_compiled_param_index(program, name);
        if (index == (size_t)-1) {
            if (err) {
                err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                err->message = "Unknown model C parameter";
            }
            return NULL;
        }
        if (target &&
            target->literal_param_values &&
            index < target->literal_param_count) {
            char raw[64];
            cxpr_model_c_format_double(raw, sizeof(raw),
                                       target->literal_param_values[index]);
            return cxpr_strdup(raw);
        }
        {
            char raw[64];
            snprintf(raw, sizeof(raw), "_cx_param_%zu", index);
            return cxpr_strdup(raw);
        }
    }
    if (err) {
        static CXPR_THREAD_LOCAL char message[256];
        err->code = CXPR_ERR_SYNTAX;
        if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FIELD_ACCESS) {
            snprintf(message, sizeof(message),
                     "Unsupported model C field '%s.%s'",
                     ast->data.field_access.object ? ast->data.field_access.object : "?",
                     ast->data.field_access.field ? ast->data.field_access.field : "?");
        } else {
            snprintf(message, sizeof(message), "Unsupported model C leaf kind %d",
                     (int)cxpr_expr_ast_kind_of(ast));
        }
        err->message = message;
    }
    return NULL;
}

char* cxpr_model_ast_c_emit_lookback(const cxpr_expr_ast* ast,
                                            unsigned lookback_offset,
                                            void* userdata,
                                            cxpr_error* err) {
    cxpr_model_ast_c_target* data = (cxpr_model_ast_c_target*)userdata;
    const cxpr_model_compiled* program = data ? data->program : NULL;
    const cxpr_expr_ast* target_ast;
    const cxpr_expr_ast* index_ast;
    cxpr_c_target target;
    char* key = NULL;
    char* index_expr = NULL;
    char* current_expr = NULL;
    size_t hist_index;
    size_t depth;
    size_t capacity;
    char out[1024];

    if (!data || !program || !ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_INDEX) return NULL;
    target_ast = cxpr_expr_ast_index_target(ast);
    index_ast = cxpr_expr_ast_index_expression(ast);
    if (!cxpr_model_lookback_target_key(target_ast, &key, err)) return NULL;
    hist_index = cxpr_model_c_history_find(program, key);
    free(key);
    if (hist_index == (size_t)-1) {
        cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                             "Unknown model C history target", 0, 0);
        return NULL;
    }
    target = (cxpr_c_target){
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
        .userdata = data,
    };
    index_expr = cxpr_expr_ast_to_c_at_offset(index_ast, 0u, &target, err);
    current_expr = cxpr_expr_ast_to_c_at_offset(target_ast, lookback_offset, &target, err);
    if (!index_expr || !current_expr) {
        free(index_expr); free(current_expr);
        return NULL;
    }
    depth = program->history_specs[hist_index].depth;
    capacity = cxpr_model_c_history_capacity(depth);
    if (lookback_offset != 0u) {
        /* Nested dynamic lookbacks are conservatively bounded. */
        snprintf(out, sizeof(out), "NAN");
    } else if (cxpr_model_c_history_use_shift(depth)) {
        snprintf(out, sizeof(out), "(((%s) <= 0.0) ? %s : ((%s) <= %zuu ? _cx_state->history_%zu.values[(size_t)(%s) - 1u] : NAN))",
                 index_expr, current_expr, index_expr, depth, hist_index, index_expr);
    } else if (cxpr_model_c_is_power_of_two(capacity)) {
        snprintf(out, sizeof(out), "(((%s) <= 0.0) ? %s : ((%s) <= %zuu ? _cx_state->history_%zu.values[(_cx_history_next_%zu + %zuu - (size_t)(%s)) & %zuu] : NAN))",
                 index_expr, current_expr, index_expr, depth, hist_index, hist_index, capacity, index_expr, capacity - 1u);
    } else {
        snprintf(out, sizeof(out), "(((%s) <= 0.0) ? %s : ((%s) <= %zuu ? _cx_state->history_%zu.values[(_cx_history_next_%zu + %zuu - (size_t)(%s)) %% %zuu] : NAN))",
                 index_expr, current_expr, index_expr, depth, hist_index, hist_index, capacity, index_expr, capacity);
    }
    free(index_expr); free(current_expr);
    return cxpr_strdup(out);
}

const char* cxpr_model_c_window_op(const char* name) {
    static const char* const codes[] = {"0", "1", "2", "3", "4", "5"};
    const cxpr_window_ir* window = cxpr_window_ir_find(name);
    return window && window->reduction >= CXPR_WINDOW_REDUCE_SUM &&
                   window->reduction <= CXPR_WINDOW_REDUCE_WEIGHTED_MEAN
               ? codes[window->reduction]
               : NULL;
}

bool cxpr_model_c_constant_param_expr(const cxpr_model_compiled* program,
                                             const cxpr_expr_ast* ast,
                                             double* out) {
    double left = 0.0;
    double right = 0.0;
    int op;
    if (!ast || !out) return false;
    if (cxpr_eval_constant_double(ast, out)) return true;
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_VARIABLE) {
        const char* name = cxpr_expr_ast_param_name(ast);
        size_t index = cxpr_model_compiled_param_index(program, name);
        return index != (size_t)-1 &&
               program->constants[index].ast &&
               cxpr_eval_constant_double(program->constants[index].ast, out);
    }
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL) {
        const char* name = cxpr_expr_ast_call_name(ast);
        size_t argc = cxpr_expr_ast_call_arg_count(ast);
        if ((!cxpr_model_names_match(name, "min") &&
             !cxpr_model_names_match(name, "max")) || argc == 0u ||
            !cxpr_model_c_constant_param_expr(
                program, cxpr_expr_ast_call_arg(ast, 0u), out)) {
            return false;
        }
        for (size_t i = 1u; i < argc; ++i) {
            double value = 0.0;
            if (!cxpr_model_c_constant_param_expr(
                    program, cxpr_expr_ast_call_arg(ast, i), &value)) {
                return false;
            }
            *out = cxpr_model_names_match(name, "min")
                       ? fmin(*out, value)
                       : fmax(*out, value);
        }
        return true;
    }
    if (cxpr_expr_ast_kind_of(ast) != CXPR_NODE_BINARY_OP) return false;
    if (!cxpr_model_c_constant_param_expr(program, cxpr_expr_ast_binary_left(ast), &left) ||
        !cxpr_model_c_constant_param_expr(program, cxpr_expr_ast_binary_right(ast), &right)) {
        return false;
    }
    op = cxpr_expr_ast_operator(ast);
    if (op == CXPR_TOK_PLUS) *out = left + right;
    else if (op == CXPR_TOK_MINUS) *out = left - right;
    else if (op == CXPR_TOK_STAR) *out = left * right;
    else if (op == CXPR_TOK_SLASH && fabs(right) > 1e-12) *out = left / right;
    else return false;
    return true;
}

bool cxpr_model_c_window_period_capacity(const cxpr_model_compiled* program,
                                                const cxpr_expr_ast* period_ast,
                                                size_t* out_capacity,
                                                cxpr_error* err) {
    double raw = 0.0;
    long period;
    (void)err;
    if (!period_ast || !out_capacity) return false;
    if (cxpr_expr_ast_kind_of(period_ast) == CXPR_NODE_VARIABLE) {
        const char* name = cxpr_expr_ast_param_name(period_ast);
        const cxpr_model_compiled_binding* binding =
            cxpr_model_c_constant_for_name(program, name);
        if (binding && binding->has_max_value && isfinite(binding->max_value)) {
            raw = binding->max_value;
            goto resolved;
        }
    }
    if (!cxpr_model_c_constant_param_expr(program, period_ast, &raw)) raw = 512.0;
resolved:
    if (!isfinite(raw) || raw < 1.0) raw = 1.0;
    period = lround(raw);
    if (period < 1) period = 1;
    *out_capacity = (size_t)period;
    return true;
}

static bool cxpr_model_c_period_ast_same(const cxpr_expr_ast* left, const cxpr_expr_ast* right) {
    if (!left || !right || cxpr_expr_ast_kind_of(left) != cxpr_expr_ast_kind_of(right)) return false;
    if (cxpr_expr_ast_kind_of(left) == CXPR_NODE_VARIABLE) {
        return cxpr_model_names_match(cxpr_expr_ast_param_name(left), cxpr_expr_ast_param_name(right));
    }
    if (cxpr_expr_ast_kind_of(left) == CXPR_NODE_NUMBER) {
        return fabs(cxpr_expr_ast_number_value(left) - cxpr_expr_ast_number_value(right)) < 1e-12;
    }
    return false;
}

static bool cxpr_model_c_ast_same_simple(const cxpr_expr_ast* left, const cxpr_expr_ast* right) {
    if (!left || !right || cxpr_expr_ast_kind_of(left) != cxpr_expr_ast_kind_of(right)) return false;
    switch (cxpr_expr_ast_kind_of(left)) {
    case CXPR_NODE_IDENTIFIER:
        return cxpr_model_names_match(cxpr_expr_ast_identifier_name(left),
                                      cxpr_expr_ast_identifier_name(right));
    case CXPR_NODE_VARIABLE:
        return cxpr_model_names_match(cxpr_expr_ast_param_name(left),
                                      cxpr_expr_ast_param_name(right));
    case CXPR_NODE_NUMBER:
        return fabs(cxpr_expr_ast_number_value(left) - cxpr_expr_ast_number_value(right)) < 1e-12;
    default:
        return false;
    }
}

const char* cxpr_model_c_find_common_binding_expr(const cxpr_model_compiled* program,
                                                         size_t binding_index,
                                                         const bool* needed_bindings,
                                                         const bool* skip_bindings,
                                                         char* const* emitted_names) {
    const cxpr_expr_ast* ast;
    if (!program || binding_index >= program->binding_count || !emitted_names) return NULL;
    if (program->bindings[binding_index].kind == CXPR_MODEL_BINDING_STATE_UPDATE) return NULL;
    ast = program->bindings[binding_index].ast;
    if (!ast) return NULL;
    (void)skip_bindings;
    for (size_t i = 0u; i < binding_index; ++i) {
        if (!needed_bindings[i] || !emitted_names[i]) continue;
        if (program->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) continue;
        if (program->bindings[i].ast &&
            cxpr_model_ast_equal(program->bindings[i].ast, ast)) {
            return emitted_names[i];
        }
    }
    return NULL;
}

static bool cxpr_model_c_ast_is_number(const cxpr_expr_ast* ast, double value) {
    return ast &&
           cxpr_expr_ast_kind_of(ast) == CXPR_NODE_NUMBER &&
           fabs(cxpr_expr_ast_number_value(ast) - value) < 1e-12;
}

static bool cxpr_model_c_match_high_low_midpoint(const cxpr_expr_ast* ast,
                                                 const cxpr_expr_ast** out_high_ast,
                                                 const cxpr_expr_ast** out_low_ast,
                                                 const cxpr_expr_ast** out_period_ast) {
    const cxpr_expr_ast* left;
    const cxpr_expr_ast* right;
    const cxpr_expr_ast* high_call = NULL;
    const cxpr_expr_ast* low_call = NULL;
    const cxpr_expr_ast* high_period;
    const cxpr_expr_ast* low_period;

    if (out_high_ast) *out_high_ast = NULL;
    if (out_low_ast) *out_low_ast = NULL;
    if (out_period_ast) *out_period_ast = NULL;
    if (!ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_BINARY_OP ||
        cxpr_expr_ast_operator(ast) != CXPR_TOK_PLUS) {
        return false;
    }
    left = cxpr_expr_ast_binary_left(ast);
    right = cxpr_expr_ast_binary_right(ast);
    if (left && cxpr_expr_ast_kind_of(left) == CXPR_NODE_FUNCTION_CALL &&
        cxpr_model_names_match(cxpr_expr_ast_call_name(left), "__cxpr_window_highest")) {
        high_call = left;
    } else if (left && cxpr_expr_ast_kind_of(left) == CXPR_NODE_FUNCTION_CALL &&
               cxpr_model_names_match(cxpr_expr_ast_call_name(left), "__cxpr_window_lowest")) {
        low_call = left;
    }
    if (right && cxpr_expr_ast_kind_of(right) == CXPR_NODE_FUNCTION_CALL &&
        cxpr_model_names_match(cxpr_expr_ast_call_name(right), "__cxpr_window_highest")) {
        high_call = right;
    } else if (right && cxpr_expr_ast_kind_of(right) == CXPR_NODE_FUNCTION_CALL &&
               cxpr_model_names_match(cxpr_expr_ast_call_name(right), "__cxpr_window_lowest")) {
        low_call = right;
    }
    if (!high_call || !low_call ||
        cxpr_expr_ast_call_arg_count(high_call) != 2u ||
        cxpr_expr_ast_call_arg_count(low_call) != 2u) {
        return false;
    }
    high_period = cxpr_expr_ast_call_arg(high_call, 1u);
    low_period = cxpr_expr_ast_call_arg(low_call, 1u);
    if (!cxpr_model_c_period_ast_same(high_period, low_period)) return false;
    if (out_high_ast) *out_high_ast = cxpr_expr_ast_call_arg(high_call, 0u);
    if (out_low_ast) *out_low_ast = cxpr_expr_ast_call_arg(low_call, 0u);
    if (out_period_ast) *out_period_ast = high_period;
    return true;
}

bool cxpr_model_c_match_scaled_high_low_midpoint(const cxpr_expr_ast* ast,
                                                        const cxpr_expr_ast** out_high_ast,
                                                        const cxpr_expr_ast** out_low_ast,
                                                        const cxpr_expr_ast** out_period_ast) {
    const cxpr_expr_ast* sum = NULL;
    if (out_high_ast) *out_high_ast = NULL;
    if (out_low_ast) *out_low_ast = NULL;
    if (out_period_ast) *out_period_ast = NULL;
    if (!ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_BINARY_OP ||
        cxpr_expr_ast_operator(ast) != CXPR_TOK_STAR) {
        return false;
    }
    if (cxpr_model_c_ast_is_number(cxpr_expr_ast_binary_left(ast), 0.5)) {
        sum = cxpr_expr_ast_binary_right(ast);
    } else if (cxpr_model_c_ast_is_number(cxpr_expr_ast_binary_right(ast), 0.5)) {
        sum = cxpr_expr_ast_binary_left(ast);
    }
    return sum && cxpr_model_c_match_high_low_midpoint(
                      sum, out_high_ast, out_low_ast, out_period_ast);
}

bool cxpr_model_c_emit_midpoint_binding(cxpr_model_c_buf* b,
                                               const char* name,
                                               const cxpr_expr_ast* high_ast,
                                               const cxpr_expr_ast* low_ast,
                                               const cxpr_expr_ast* period_ast,
                                               const cxpr_c_target* target,
                                               const cxpr_model_compiled* program,
                                               cxpr_error* err) {
    size_t capacity = 0u;
    char* period_expr = NULL;
    char* period_limit_expr = NULL;

    if (!b || !name || !high_ast || !low_ast || !period_ast || !target || !program) return false;
    if (!cxpr_model_c_window_period_capacity(program, period_ast, &capacity, err)) return false;
    period_expr = cxpr_expr_ast_to_c_at_offset(period_ast, 0u, target, err);
    if (!period_expr) return false;
    {
        cxpr_model_c_buf pb = {0};
        cxpr_model_c_printf(
            &pb,
            "(int)fmax(1.0, fmin((double)%zuu, round(%s)))",
            capacity,
            period_expr);
        if (pb.oom) {
            free(period_expr);
            free(pb.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        period_limit_expr = pb.data;
    }

    cxpr_model_c_printf(
        b,
        "    double %s; { const size_t _cx_limit = (size_t)(%s); double _cx_highest = 0.0; double _cx_lowest = 0.0; size_t _cx_count = 0u;\n",
        name,
        period_limit_expr);
    {
        cxpr_model_c_buf loop = {0};
        cxpr_model_c_puts(&loop, "        for (size_t _cx_i = 0u; _cx_i < _cx_limit; ++_cx_i) {\n");
        if (cxpr_model_c_emit_dynamic_history_value(
                &loop, "_cx_hi", high_ast, "_cx_i", target, program, err) &&
            cxpr_model_c_emit_dynamic_history_value(
                &loop, "_cx_lo", low_ast, "_cx_i", target, program, err)) {
            cxpr_model_c_puts(
                &loop,
                "            if (!isnan(_cx_hi) && !isnan(_cx_lo)) { if (_cx_count == 0u) { _cx_highest = _cx_hi; _cx_lowest = _cx_lo; } if (_cx_hi > _cx_highest) _cx_highest = _cx_hi; if (_cx_lo < _cx_lowest) _cx_lowest = _cx_lo; _cx_count++; }\n"
                "        }\n");
            if (loop.oom) {
                free(period_expr);
                free(period_limit_expr);
                free(loop.data);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            cxpr_model_c_puts(b, loop.data);
            free(loop.data);
            cxpr_model_c_printf(
                b,
                "        %s = _cx_count == 0u ? 0.0 : (_cx_highest + _cx_lowest) * 0.5; }\n",
                name);
            free(period_expr);
            free(period_limit_expr);
            if (b->oom) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            return true;
        }
        if (err && err->code != CXPR_OK) {
            free(period_expr);
            free(period_limit_expr);
            free(loop.data);
            return false;
        }
        free(loop.data);
    }
    for (size_t i = 0u; i < capacity; ++i) {
        char* high_expr = cxpr_expr_ast_to_c_at_offset(high_ast, (unsigned)i, target, err);
        char* low_expr = high_expr
                             ? cxpr_expr_ast_to_c_at_offset(low_ast, (unsigned)i, target, err)
                             : NULL;
        if (!high_expr || !low_expr) {
            free(high_expr);
            free(low_expr);
            free(period_expr);
            free(period_limit_expr);
            return false;
        }
        cxpr_model_c_printf(
            b,
            "        if (%zuu < _cx_limit) { double _cx_hi = %s; double _cx_lo = %s; if (!isnan(_cx_hi) && !isnan(_cx_lo)) { if (_cx_count == 0u) { _cx_highest = _cx_hi; _cx_lowest = _cx_lo; } if (_cx_hi > _cx_highest) _cx_highest = _cx_hi; if (_cx_lo < _cx_lowest) _cx_lowest = _cx_lo; _cx_count++; } }\n",
            i,
            high_expr,
            low_expr);
        free(high_expr);
        free(low_expr);
    }
    cxpr_model_c_printf(
        b,
        "        %s = _cx_count == 0u ? 0.0 : (_cx_highest + _cx_lowest) * 0.5; }\n",
        name);
    free(period_expr);
    free(period_limit_expr);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

static int cxpr_model_c_window_op_code(const char* name) {
    const cxpr_window_ir* window = cxpr_window_ir_find(name);
    return window ? (int)window->reduction : -1;
}

bool cxpr_model_c_emit_simple_window_binding(cxpr_model_c_buf* b,
                                                    const char* name,
                                                    const cxpr_expr_ast* ast,
                                                    const cxpr_c_target* target,
                                                    const cxpr_model_compiled* program,
                                                    cxpr_error* err) {
    const char* fn_name;
    const cxpr_expr_ast* value_ast;
    const cxpr_expr_ast* period_ast;
    int op;
    size_t capacity = 0u;
    char* period_expr = NULL;
    char* period_limit_expr = NULL;

    if (!b || !name || !ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_FUNCTION_CALL ||
        cxpr_expr_ast_call_arg_count(ast) != 2u || !target || !program) {
        return false;
    }
    fn_name = cxpr_expr_ast_call_name(ast);
    op = cxpr_model_c_window_op_code(fn_name);
    if (op < 0) return false;
    value_ast = cxpr_expr_ast_call_arg(ast, 0u);
    if (cxpr_expr_ast_kind_of(value_ast) == CXPR_NODE_FUNCTION_CALL &&
        cxpr_model_window_is_function(cxpr_expr_ast_call_name(value_ast))) {
        return false;
    }
    period_ast = cxpr_expr_ast_call_arg(ast, 1u);
    if (!cxpr_model_c_window_period_capacity(program, period_ast, &capacity, err)) return false;
    period_expr = cxpr_expr_ast_to_c_at_offset(period_ast, 0u, target, err);
    if (!period_expr) return false;
    {
        cxpr_model_c_buf pb = {0};
        cxpr_model_c_printf(
            &pb,
            "(int)fmax(1.0, fmin((double)%zuu, round(%s)))",
            capacity,
            period_expr);
        if (pb.oom) {
            free(period_expr);
            free(pb.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        period_limit_expr = pb.data;
    }

    cxpr_model_c_printf(
        b,
        "    double %s; { const size_t _cx_limit = (size_t)(%s); double _cx_sum = 0.0; double _cx_sumsq = 0.0; double _cx_weighted_sum = 0.0; double _cx_weight_sum = 0.0; double _cx_extreme = 0.0; size_t _cx_count = 0u;\n",
        name,
        period_limit_expr);
    {
        cxpr_model_c_buf loop = {0};
        cxpr_model_c_puts(&loop, "        for (size_t _cx_i = 0u; _cx_i < _cx_limit; ++_cx_i) {\n");
        if (cxpr_model_c_emit_dynamic_history_value(
                &loop, "_cx_x", value_ast, "_cx_i", target, program, err)) {
            cxpr_model_c_printf(
                &loop,
                "            if (!isnan(_cx_x)) { double _cx_weight = (double)(_cx_limit - _cx_i); if (_cx_count == 0u) _cx_extreme = _cx_x; if (%d == 2 && _cx_x > _cx_extreme) _cx_extreme = _cx_x; if (%d == 3 && _cx_x < _cx_extreme) _cx_extreme = _cx_x; _cx_sum += _cx_x; _cx_sumsq += _cx_x * _cx_x; _cx_weighted_sum += _cx_x * _cx_weight; _cx_weight_sum += _cx_weight; _cx_count++; }\n"
                "        }\n",
                op,
                op);
            if (loop.oom) {
                free(period_expr);
                free(period_limit_expr);
                free(loop.data);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            cxpr_model_c_puts(b, loop.data);
            free(loop.data);
            if (op == 2 || op == 3) {
                cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_extreme; }\n", name);
            } else if (op == 1) {
                cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_sum / (double)_cx_count; }\n", name);
            } else if (op == 4) {
                cxpr_model_c_printf(
                    b,
                    "        if (_cx_count == 0u) %s = 0.0; else { double _cx_mean = _cx_sum / (double)_cx_count; double _cx_var = (_cx_sumsq / (double)_cx_count) - _cx_mean * _cx_mean; %s = sqrt(_cx_var > 0.0 ? _cx_var : 0.0); } }\n",
                    name,
                    name);
            } else if (op == 5) {
                cxpr_model_c_printf(
                    b,
                    "        %s = _cx_weight_sum > 0.0 ? _cx_weighted_sum / _cx_weight_sum : 0.0; }\n",
                    name);
            } else {
                cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_sum; }\n", name);
            }
            free(period_expr);
            free(period_limit_expr);
            if (b->oom) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            return true;
        }
        if (err && err->code != CXPR_OK) {
            free(period_expr);
            free(period_limit_expr);
            free(loop.data);
            return false;
        }
        free(loop.data);
    }
    for (size_t i = 0u; i < capacity; ++i) {
        char* value_expr = cxpr_expr_ast_to_c_at_offset(value_ast, (unsigned)i, target, err);
        if (!value_expr) {
            free(period_expr);
            free(period_limit_expr);
            return false;
        }
        cxpr_model_c_printf(
            b,
            "        if (%zuu < _cx_limit) { double _cx_x = %s; if (!isnan(_cx_x)) { double _cx_weight = (double)(_cx_limit - %zuu); if (_cx_count == 0u) _cx_extreme = _cx_x; if (%d == 2 && _cx_x > _cx_extreme) _cx_extreme = _cx_x; if (%d == 3 && _cx_x < _cx_extreme) _cx_extreme = _cx_x; _cx_sum += _cx_x; _cx_sumsq += _cx_x * _cx_x; _cx_weighted_sum += _cx_x * _cx_weight; _cx_weight_sum += _cx_weight; _cx_count++; } }\n",
            i,
            value_expr,
            i,
            op,
            op);
        free(value_expr);
    }
    if (op == 2 || op == 3) {
        cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_extreme; }\n", name);
    } else if (op == 1) {
        cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_sum / (double)_cx_count; }\n", name);
    } else if (op == 4) {
        cxpr_model_c_printf(
            b,
            "        if (_cx_count == 0u) %s = 0.0; else { double _cx_mean = _cx_sum / (double)_cx_count; double _cx_var = (_cx_sumsq / (double)_cx_count) - _cx_mean * _cx_mean; %s = sqrt(_cx_var > 0.0 ? _cx_var : 0.0); } }\n",
            name,
            name);
    } else if (op == 5) {
        cxpr_model_c_printf(
            b,
            "        %s = _cx_weight_sum > 0.0 ? _cx_weighted_sum / _cx_weight_sum : 0.0; }\n",
            name);
    } else {
        cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_sum; }\n", name);
    }
    free(period_expr);
    free(period_limit_expr);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

bool cxpr_model_c_match_mean_stddev_pair(const cxpr_expr_ast* mean_ast,
                                                const cxpr_expr_ast* stddev_ast) {
    if (!mean_ast || !stddev_ast ||
        cxpr_expr_ast_kind_of(mean_ast) != CXPR_NODE_FUNCTION_CALL ||
        cxpr_expr_ast_kind_of(stddev_ast) != CXPR_NODE_FUNCTION_CALL ||
        cxpr_expr_ast_call_arg_count(mean_ast) != 2u ||
        cxpr_expr_ast_call_arg_count(stddev_ast) != 2u ||
        !cxpr_model_names_match(cxpr_expr_ast_call_name(mean_ast), "__cxpr_window_mean") ||
        !cxpr_model_names_match(cxpr_expr_ast_call_name(stddev_ast), "__cxpr_window_stddev")) {
        return false;
    }
    return cxpr_model_c_ast_same_simple(cxpr_expr_ast_call_arg(mean_ast, 0u),
                                        cxpr_expr_ast_call_arg(stddev_ast, 0u)) &&
           cxpr_model_c_period_ast_same(cxpr_expr_ast_call_arg(mean_ast, 1u),
                                        cxpr_expr_ast_call_arg(stddev_ast, 1u));
}

bool cxpr_model_c_emit_mean_stddev_bindings(cxpr_model_c_buf* b,
                                                   const char* mean_name,
                                                   const char* stddev_name,
                                                   const cxpr_expr_ast* mean_ast,
                                                   const cxpr_c_target* target,
                                                   const cxpr_model_compiled* program,
                                                   cxpr_error* err) {
    const cxpr_expr_ast* value_ast;
    const cxpr_expr_ast* period_ast;
    size_t capacity = 0u;
    char* period_expr = NULL;
    char* period_limit_expr = NULL;

    if (!b || !mean_name || !stddev_name || !mean_ast ||
        cxpr_expr_ast_kind_of(mean_ast) != CXPR_NODE_FUNCTION_CALL ||
        cxpr_expr_ast_call_arg_count(mean_ast) != 2u || !target || !program) {
        return false;
    }
    value_ast = cxpr_expr_ast_call_arg(mean_ast, 0u);
    period_ast = cxpr_expr_ast_call_arg(mean_ast, 1u);
    if (!cxpr_model_c_window_period_capacity(program, period_ast, &capacity, err)) return false;
    period_expr = cxpr_expr_ast_to_c_at_offset(period_ast, 0u, target, err);
    if (!period_expr) return false;
    {
        cxpr_model_c_buf pb = {0};
        cxpr_model_c_printf(
            &pb,
            "(int)fmax(1.0, fmin((double)%zuu, round(%s)))",
            capacity,
            period_expr);
        if (pb.oom) {
            free(period_expr);
            free(pb.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        period_limit_expr = pb.data;
    }

    cxpr_model_c_printf(
        b,
        "    double %s; double %s; { const size_t _cx_limit = (size_t)(%s); double _cx_sum = 0.0; double _cx_sumsq = 0.0; size_t _cx_count = 0u;\n",
        mean_name,
        stddev_name,
        period_limit_expr);
    {
        cxpr_model_c_buf loop = {0};
        cxpr_model_c_puts(&loop, "        for (size_t _cx_i = 0u; _cx_i < _cx_limit; ++_cx_i) {\n");
        if (cxpr_model_c_emit_dynamic_history_value(
                &loop, "_cx_x", value_ast, "_cx_i", target, program, err)) {
            cxpr_model_c_puts(
                &loop,
                "            if (!isnan(_cx_x)) { _cx_sum += _cx_x; _cx_sumsq += _cx_x * _cx_x; _cx_count++; }\n"
                "        }\n");
            if (loop.oom) {
                free(period_expr);
                free(period_limit_expr);
                free(loop.data);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            cxpr_model_c_puts(b, loop.data);
            free(loop.data);
            cxpr_model_c_printf(
                b,
                "        if (_cx_count == 0u) { %s = 0.0; %s = 0.0; } else { double _cx_mean = _cx_sum / (double)_cx_count; double _cx_var = (_cx_sumsq / (double)_cx_count) - _cx_mean * _cx_mean; %s = _cx_mean; %s = sqrt(_cx_var > 0.0 ? _cx_var : 0.0); } }\n",
                mean_name,
                stddev_name,
                mean_name,
                stddev_name);
            free(period_expr);
            free(period_limit_expr);
            if (b->oom) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            return true;
        }
        if (err && err->code != CXPR_OK) {
            free(period_expr);
            free(period_limit_expr);
            free(loop.data);
            return false;
        }
        free(loop.data);
    }
    for (size_t i = 0u; i < capacity; ++i) {
        char* value_expr = cxpr_expr_ast_to_c_at_offset(value_ast, (unsigned)i, target, err);
        if (!value_expr) {
            free(period_expr);
            free(period_limit_expr);
            return false;
        }
        cxpr_model_c_printf(
            b,
            "        if (%zuu < _cx_limit) { double _cx_x = %s; if (!isnan(_cx_x)) { _cx_sum += _cx_x; _cx_sumsq += _cx_x * _cx_x; _cx_count++; } }\n",
            i,
            value_expr);
        free(value_expr);
    }
    cxpr_model_c_printf(
        b,
        "        if (_cx_count == 0u) { %s = 0.0; %s = 0.0; } else { double _cx_mean = _cx_sum / (double)_cx_count; double _cx_var = (_cx_sumsq / (double)_cx_count) - _cx_mean * _cx_mean; %s = _cx_mean; %s = sqrt(_cx_var > 0.0 ? _cx_var : 0.0); } }\n",
        mean_name,
        stddev_name,
        mean_name,
        stddev_name);
    free(period_expr);
    free(period_limit_expr);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

static size_t cxpr_model_c_standard_slot_count_inline(const cxpr_model_compiled* program) {
    (void)program;
    return 0u;
}

size_t cxpr_model_c_window_plan_base(const cxpr_model_compiled* program,
                                     const cxpr_model_window_plan_node* node) {
    if (!program || !node || node->slot_count == 0u) return (size_t)-1;
    return cxpr_model_c_standard_slot_count_inline(program) + node->slot_offset;
}

const char* cxpr_model_c_window_counter_type(
    const cxpr_model_window_plan_node* node) {
    if (!node || node->period_capacity > 255u) return "size_t";
    return "uint8_t";
}
