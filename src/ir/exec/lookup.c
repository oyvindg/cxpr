/**
 * @file lookup.c
 * @brief IR runtime lookup helpers for fields, chains, and typed variables.
 */

#include "internal.h"
#include "context/internal.h"
#include "expression/internal.h"
#include <math.h>
#include <stdio.h>

static const char* cxpr_ir_unknown_lookup_message(const char* kind, const char* name) {
    static CXPR_THREAD_LOCAL char message[512];
    if (!kind || kind[0] == '\0') kind = "identifier";
    if (!name || name[0] == '\0') {
        snprintf(message, sizeof(message), "Unknown %s", kind);
    } else {
        snprintf(message, sizeof(message), "Unknown %s '%s'", kind, name);
    }
    return message;
}

double cxpr_ir_context_get_prehashed(const cxpr_context* ctx, const char* name,
                                     unsigned long hash, bool* found) {
    double value;

    if (!ctx) {
        if (found) *found = false;
        return 0.0;
    }

    {
        bool bool_found = false;
        bool bool_value = cxpr_context_get_local_bool(ctx, name, &bool_found);
        if (bool_found) {
            if (found) *found = true;
            return bool_value ? 1.0 : 0.0;
        }
    }
    value = cxpr_hashmap_get_prehashed(&ctx->variables, name, hash, found);
    if (found && *found) return value;
    if (ctx->parent) return cxpr_ir_context_get_prehashed(ctx->parent, name, hash, found);
    if (found) *found = false;
    return 0.0;
}

cxpr_value cxpr_ir_struct_get_field(const cxpr_struct_value* value,
                                    const char* field, bool* found) {
    if (found) *found = false;
    if (!value || !field) return cxpr_num(NAN);

    for (size_t i = 0; i < value->field_count; ++i) {
        if (strcmp(value->field_names[i], field) == 0) {
            if (found) *found = true;
            return cxpr_value_clone(&value->field_values[i]);
        }
    }

    return cxpr_num(NAN);
}

cxpr_value cxpr_ir_load_field_value(const cxpr_context* ctx, const cxpr_registry* reg,
                                    const cxpr_ir_instr* instr, cxpr_error* err) {
    const char* dot;
    bool found = false;
    cxpr_value value;
    char root[128];
    size_t root_len;

    if (!ctx || !instr || !instr->name) {
        return cxpr_ir_runtime_error(err, "Invalid field access");
    }

    dot = strchr(instr->name, '.');
    if (!dot) {
        return cxpr_ir_runtime_error(err, "Malformed field access");
    }

    root_len = (size_t)(dot - instr->name);
    if (root_len == 0 || root_len >= sizeof(root)) {
        return cxpr_ir_runtime_error(err, "Field access root too long");
    }

    memcpy(root, instr->name, root_len);
    root[root_len] = '\0';

    if (ctx->expression_scope) {
        cxpr_value scoped = cxpr_expression_lookup_typed_result(
            ctx->expression_scope,
            instr->name,
            &found);
        if (found) return scoped;

        scoped = cxpr_expression_lookup_typed_result(
            ctx->expression_scope,
            root,
            &found);
        if (found && scoped.type != CXPR_VALUE_STRUCT) {
            return cxpr_ir_make_not_found(
                err,
                "Expression-scope chain has no struct prefix");
        }
    }

    value = cxpr_context_get_field(ctx, root, dot + 1, &found);
    if (found) return value;

    if (reg) {
        cxpr_func_entry* producer = cxpr_registry_find(reg, root);
        if (producer && producer->struct_producer &&
            producer->min_args == 0 && producer->max_args == 0) {
            cxpr_value produced = cxpr_ir_call_producer(producer, root, ctx, NULL, 0, err);
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            if (produced.type == CXPR_VALUE_STRUCT) {
                value = cxpr_ir_struct_get_field(produced.s, dot + 1, &found);
            } else {
                value = cxpr_context_get_field(ctx, root, dot + 1, &found);
            }
            if (found) return value;
        }
    }

    value = cxpr_num(cxpr_ir_context_get_prehashed(ctx, instr->name, instr->hash, &found));
    if (!found) {
        return cxpr_ir_make_not_found(err, cxpr_ir_unknown_lookup_message("field access", instr->name));
    }
    return value;
}

cxpr_value cxpr_ir_load_named_field_value(const cxpr_context* ctx,
                                          const cxpr_ir_instr* instr,
                                          cxpr_error* err) {
    bool found = false;
    cxpr_value value;
    char flat_key[256];
    int written;

    if (!ctx || !instr || !instr->name || !instr->aux_name) {
        return cxpr_ir_runtime_error(err, "Invalid field access");
    }

    value = cxpr_context_get_field(ctx, instr->name, instr->aux_name, &found);
    if (found) return value;

    written = snprintf(flat_key, sizeof(flat_key), "%s.%s", instr->name, instr->aux_name);
    if (written <= 0 || (size_t)written >= sizeof(flat_key)) {
        return cxpr_ir_runtime_error(err, "Field access root too long");
    }

    value = cxpr_num(cxpr_ir_context_get_prehashed(ctx, flat_key, instr->hash, &found));
    if (!found) {
        return cxpr_ir_make_not_found(err, cxpr_ir_unknown_lookup_message("field access", instr->name));
    }
    return value;
}

cxpr_value cxpr_ir_load_chain_value(const cxpr_context* ctx, const cxpr_ir_instr* instr,
                                    cxpr_error* err) {
    char* path;
    char* segment;
    char* saveptr = NULL;
    const cxpr_struct_value* current;
    cxpr_value value = cxpr_num(NAN);
    bool found = false;
    bool scoped_prefix_found = false;
    size_t prefix_len = 0u;

    if (!ctx || !instr || !instr->name) {
        return cxpr_ir_runtime_error(err, "Invalid chain access");
    }

    path = cxpr_strdup(instr->name);
    if (!path) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return cxpr_num(NAN);
    }

    if (ctx->expression_scope) {
        char* dot;
        for (dot = strchr(path, '.'); dot; dot = strchr(dot + 1, '.')) {
            cxpr_value scoped;
            *dot = '\0';
            scoped = cxpr_expression_lookup_typed_result(ctx->expression_scope, path, &found);
            if (found) scoped_prefix_found = true;
            if (found && scoped.type == CXPR_VALUE_STRUCT) {
                prefix_len = (size_t)(dot - path);
                value = scoped;
            }
            *dot = '.';
        }
        if (prefix_len != 0u && value.type == CXPR_VALUE_STRUCT) {
            current = value.s;
            segment = path + prefix_len + 1u;
            while (segment && *segment) {
                char* next = strchr(segment, '.');
                size_t segment_len = next ? (size_t)(next - segment) : strlen(segment);
                found = false;
                for (size_t i = 0; i < current->field_count; ++i) {
                    if (strlen(current->field_names[i]) == segment_len &&
                        strncmp(current->field_names[i], segment, segment_len) == 0) {
                        value = current->field_values[i];
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    free(path);
                    return cxpr_ir_make_not_found(err, cxpr_ir_unknown_lookup_message("identifier", segment));
                }
                if (!next) {
                    free(path);
                    return cxpr_value_clone(&value);
                }
                if (!cxpr_ir_require_type(value, CXPR_VALUE_STRUCT, err,
                                          "Chained access requires struct intermediate")) {
                    free(path);
                    return cxpr_num(NAN);
                }
                current = value.s;
                segment = next + 1u;
            }
            free(path);
            return cxpr_ir_runtime_error(err, "Malformed chain access");
        }
        if (scoped_prefix_found) {
            free(path);
            return cxpr_ir_make_not_found(
                err,
                "Expression-scope chain has no struct prefix");
        }
    }

    segment = cxpr_strtok_r(path, ".", &saveptr);
    if (!segment) {
        free(path);
        return cxpr_ir_runtime_error(err, "Malformed chain access");
    }

    current = cxpr_context_get_struct(ctx, segment);
    if (!current) {
        cxpr_value root = cxpr_context_get_typed(ctx, segment, &found);
        if (found && root.type == CXPR_VALUE_STRUCT) {
            current = root.s;
        } else {
            free(path);
            return cxpr_ir_make_not_found(err, cxpr_ir_unknown_lookup_message("identifier", segment));
        }
    }

    segment = cxpr_strtok_r(NULL, ".", &saveptr);
    while (segment) {
        char* next = cxpr_strtok_r(NULL, ".", &saveptr);
        found = false;
        for (size_t i = 0; i < current->field_count; ++i) {
            if (strcmp(current->field_names[i], segment) == 0) {
                value = current->field_values[i];
                found = true;
                break;
            }
        }
        if (!found) {
            free(path);
            return cxpr_ir_make_not_found(err, cxpr_ir_unknown_lookup_message("identifier", segment));
        }
        if (!next) {
            free(path);
            return cxpr_value_clone(&value);
        }
        if (!cxpr_ir_require_type(value, CXPR_VALUE_STRUCT, err,
                                  "Chained access requires struct intermediate")) {
            free(path);
            return cxpr_num(NAN);
        }
        current = value.s;
        segment = next;
    }

    free(path);
    return cxpr_ir_runtime_error(err, "Malformed chain access");
}

bool cxpr_ir_push_squared(cxpr_value* stack, size_t* sp, cxpr_value value,
                          cxpr_error* err) {
    if (!cxpr_ir_require_type(value, CXPR_VALUE_NUMBER, err,
                              "Square operation requires double operand")) {
        return false;
    }
    value.d *= value.d;
    return cxpr_ir_stack_push(stack, sp, value, 64, err);
}

bool cxpr_ir_pop1(cxpr_value* stack, size_t* sp, cxpr_value* out,
                  cxpr_error* err) {
    if (!cxpr_ir_require_stack(*sp, 1, err)) return false;
    *out = stack[--(*sp)];
    return true;
}

bool cxpr_ir_pop2(cxpr_value* stack, size_t* sp, cxpr_value* left,
                  cxpr_value* right, cxpr_error* err) {
    if (!cxpr_ir_require_stack(*sp, 2, err)) return false;
    *right = stack[--(*sp)];
    *left = stack[--(*sp)];
    return true;
}

cxpr_value cxpr_ir_load_variable_typed(const cxpr_context* ctx,
                                       const cxpr_ir_program* program,
                                       size_t ip,
                                       const cxpr_ir_instr* instr,
                                       bool* found) {
    bool scope_found = false;
    bool scalar_found = false;
    double scalar = 0.0;

    if (found) *found = false;
    if (!ctx || !instr || !instr->name) return cxpr_num(0.0);

    if (ctx->expression_scope) {
        cxpr_value scoped =
            cxpr_expression_lookup_typed_result(ctx->expression_scope, instr->name, &scope_found);
        if (scope_found) {
            if (found) *found = true;
            return scoped;
        }
    }

    {
        cxpr_value typed = cxpr_context_get_typed(ctx, instr->name, &scope_found);
        if (scope_found) {
            if (found) *found = true;
            return typed;
        }
    }

    scalar = cxpr_ir_lookup_cached_scalar(
        ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, false,
        &scalar_found);
    if (scalar_found) {
        if (found) *found = true;
        return cxpr_num(scalar);
    }

    if (found) *found = false;
    return cxpr_num(0.0);
}
