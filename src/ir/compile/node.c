/**
 * @file node.c
 * @brief AST-to-IR node lowering.
 */

#include "call/args.h"
#include "internal.h"
#include "core.h"
#include "lookback.h"

#include <stdio.h>

static const char* cxpr_ir_unknown_function_message(const char* name) {
    static CXPR_THREAD_LOCAL char message[256];
    if (!name || name[0] == '\0') return "Unknown function";
    snprintf(message, sizeof(message), "Unknown function '%s'", name);
    return message;
}

static bool cxpr_ir_emit_defined_direct_field_call(cxpr_func_entry* entry,
                                                   const cxpr_ast* call_ast,
                                                   cxpr_ir_program* program,
                                                   cxpr_error* err) {
    const cxpr_ast* body;
    const char* body_param;
    const char* body_field;
    char flat_key[256];
    int written;

    if (!entry || !entry->defined_body || !call_ast ||
        call_ast->type != CXPR_NODE_FUNCTION_CALL) {
        return false;
    }

    body = entry->defined_body;
    if (body->type == CXPR_NODE_FIELD_ACCESS && !body->data.field_access.base) {
        body_param = body->data.field_access.object;
        body_field = body->data.field_access.field;
    } else if (body->type == CXPR_NODE_CHAIN_ACCESS && body->data.chain_access.depth == 2) {
        body_param = body->data.chain_access.path[0];
        body_field = body->data.chain_access.path[1];
    } else {
        return false;
    }

    for (size_t i = 0; i < entry->defined_param_count; ++i) {
        const cxpr_ast* arg;

        if (strcmp(entry->defined_param_names[i], body_param) != 0) continue;
        if (i >= call_ast->data.function_call.argc) return false;

        arg = call_ast->data.function_call.args[i];
        if (!arg || arg->type != CXPR_NODE_IDENTIFIER) return false;

        written = snprintf(flat_key, sizeof(flat_key), "%s.%s",
                           arg->data.identifier.name, body_field);
        if (written <= 0 || (size_t)written >= sizeof(flat_key)) return false;

        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_LOAD_NAMED_FIELD,
                                .name = arg->data.identifier.name,
                                .aux_name = body_field,
                                .hash = cxpr_hash_string(flat_key),
                            },
                            err);
    }

    return false;
}

static bool cxpr_ir_defined_body_needs_ast_eval(const cxpr_ast* ast,
                                                const cxpr_registry* reg,
                                                size_t depth) {
    size_t i;
    if (!ast || depth > CXPR_IR_INLINE_DEPTH_LIMIT) return false;
    switch (ast->type) {
    case CXPR_NODE_FUNCTION_CALL: {
        cxpr_func_entry* entry = cxpr_registry_find(reg, ast->data.function_call.name);
        if (entry && (entry->ast_func || entry->ast_func_handler)) return true;
        if (entry && entry->defined_body &&
            cxpr_ir_defined_body_needs_ast_eval(entry->defined_body, reg, depth + 1u)) {
            return true;
        }
        for (i = 0u; i < ast->data.function_call.argc; ++i) {
            if (cxpr_ir_defined_body_needs_ast_eval(
                    ast->data.function_call.args[i], reg, depth)) {
                return true;
            }
        }
        return false;
    }
    case CXPR_NODE_PRODUCER_ACCESS:
        for (i = 0u; i < ast->data.producer_access.argc; ++i) {
            if (cxpr_ir_defined_body_needs_ast_eval(
                    ast->data.producer_access.args[i], reg, depth)) {
                return true;
            }
        }
        return false;
    case CXPR_NODE_LOOKBACK:
        return cxpr_ir_defined_body_needs_ast_eval(ast->data.lookback.target, reg, depth) ||
               cxpr_ir_defined_body_needs_ast_eval(ast->data.lookback.index, reg, depth);
    case CXPR_NODE_BINARY_OP:
        return cxpr_ir_defined_body_needs_ast_eval(ast->data.binary_op.left, reg, depth) ||
               cxpr_ir_defined_body_needs_ast_eval(ast->data.binary_op.right, reg, depth);
    case CXPR_NODE_UNARY_OP:
        return cxpr_ir_defined_body_needs_ast_eval(ast->data.unary_op.operand, reg, depth);
    case CXPR_NODE_TERNARY:
        return cxpr_ir_defined_body_needs_ast_eval(ast->data.ternary.condition, reg, depth) ||
               cxpr_ir_defined_body_needs_ast_eval(ast->data.ternary.true_branch, reg, depth) ||
               cxpr_ir_defined_body_needs_ast_eval(ast->data.ternary.false_branch, reg, depth);
    case CXPR_NODE_FIELD_ACCESS:
        return ast->data.field_access.base &&
               cxpr_ir_defined_body_needs_ast_eval(ast->data.field_access.base, reg, depth);
    case CXPR_NODE_ARRAY:
        for (i = 0u; i < ast->data.array.count; ++i) {
            if (cxpr_ir_defined_body_needs_ast_eval(ast->data.array.elements[i], reg, depth)) {
                return true;
            }
        }
        return false;
    case CXPR_NODE_RECORD:
        for (i = 0u; i < ast->data.record.field_count; ++i) {
            if (cxpr_ir_defined_body_needs_ast_eval(
                    ast->data.record.field_values[i], reg, depth)) {
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

static bool cxpr_ir_defined_call_can_inline(const cxpr_func_entry* entry,
                                            const cxpr_ast* call_ast,
                                            const cxpr_registry* reg) {
    if (!entry || !entry->defined_body || !call_ast ||
        call_ast->type != CXPR_NODE_FUNCTION_CALL ||
        entry->defined_return_field_count > 0u ||
        entry->defined_param_count != call_ast->data.function_call.argc) {
        return false;
    }
    if (cxpr_ir_defined_body_needs_ast_eval(entry->defined_body, reg, 0u)) {
        return false;
    }
    for (size_t i = 0u; i < entry->defined_param_count; ++i) {
        if (entry->defined_param_fields[i] &&
            entry->defined_param_field_counts[i] > 0u) {
            const cxpr_ast* arg = call_ast->data.function_call.args[i];
            if (!arg || arg->type != CXPR_NODE_IDENTIFIER) return false;
        }
    }
    return true;
}

bool cxpr_ir_compile_node(const cxpr_ast* ast, cxpr_ir_program* program,
                          const cxpr_registry* reg,
                          const char* const* local_names, size_t local_count,
                          const cxpr_ir_subst_frame* subst,
                          size_t inline_depth,
                          cxpr_error* err) {
    cxpr_value constant;

    if (!ast) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "NULL AST node";
        }
        return false;
    }

    if (cxpr_ir_constant_typed_value(ast, reg, &constant)) {
        if (constant.type != CXPR_VALUE_NUMBER && constant.type != CXPR_VALUE_BOOL &&
            constant.type != CXPR_VALUE_STRING) {
            cxpr_value_free(&constant);
            constant = (cxpr_value){0};
        } else {
            if (constant.type == CXPR_VALUE_STRING) {
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = CXPR_OP_PUSH_STRING,
                                        .name = constant.str,
                                    },
                                    err);
            }
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = constant.type == CXPR_VALUE_BOOL
                                              ? CXPR_OP_PUSH_BOOL
                                              : CXPR_OP_PUSH_CONST,
                                    .value = constant.type == CXPR_VALUE_BOOL
                                                 ? (constant.b ? 1.0 : 0.0)
                                                 : constant.d,
                                },
                                err);
        }
    }

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_PUSH_CONST,
                                .value = ast->data.number.value,
                                .name = NULL,
                            },
                            err);

    case CXPR_NODE_BOOL:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_PUSH_BOOL,
                                .value = ast->data.boolean.value ? 1.0 : 0.0,
                                .name = NULL,
                            },
                            err);

    case CXPR_NODE_STRING:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_PUSH_STRING,
                                .name = ast->data.string.value,
                            },
                            err);

    case CXPR_NODE_ARRAY:
        for (size_t i = 0; i < ast->data.array.count; ++i) {
            if (!cxpr_ir_compile_node(ast->data.array.elements[i], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
        }
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_BUILD_ARRAY,
                                .index = ast->data.array.count,
                            },
                            err);

    case CXPR_NODE_RECORD:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_CALL_AST,
                                .ast = ast,
                            },
                            err);

    case CXPR_NODE_IDENTIFIER:
        {
            const cxpr_ir_subst_frame* owner = NULL;
            const cxpr_ast* mapped = cxpr_ir_subst_lookup(subst, ast->data.identifier.name, &owner);
            if (mapped) {
                return cxpr_ir_compile_node(mapped, program, reg,
                                            local_names, local_count,
                                            owner ? owner->parent : NULL,
                                            inline_depth, err);
            }
            const size_t local_index =
                cxpr_ir_local_index(ast->data.identifier.name, local_names, local_count);
            if (local_index != (size_t)-1) {
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = CXPR_OP_LOAD_LOCAL,
                                        .index = local_index,
                                    },
                                    err);
            }
        }
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_LOAD_VAR,
                                .name = ast->data.identifier.name,
                                .payload = ast,
                                .hash = cxpr_hash_string(ast->data.identifier.name),
                            },
                            err);

    case CXPR_NODE_VARIABLE:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_LOAD_PARAM,
                                .name = ast->data.variable.name,
                                .hash = cxpr_hash_string(ast->data.variable.name),
                            },
                            err);

    case CXPR_NODE_FIELD_ACCESS:
        if (ast->data.field_access.base) {
            const cxpr_ast* base = ast->data.field_access.base;
            if (base->type == CXPR_NODE_FUNCTION_CALL && reg) {
                cxpr_func_entry* entry =
                    cxpr_registry_find(reg, base->data.function_call.name);
                if (entry && entry->ast_func_handler) {
                    return cxpr_ir_emit(program,
                                        (cxpr_ir_instr){
                                            .op = CXPR_OP_CALL_AST,
                                            .ast = ast,
                                        },
                                        err);
                }
            }
            if (!cxpr_ir_compile_node(ast->data.field_access.base, program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = CXPR_OP_GET_FIELD,
                                    .name = ast->data.field_access.field,
                                },
                                err);
        }
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_LOAD_FIELD,
                                .name = ast->data.field_access.full_key,
                                .payload = ast,
                                .hash = cxpr_hash_string(ast->data.field_access.full_key),
                            },
                            err);

    case CXPR_NODE_CHAIN_ACCESS:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_LOAD_CHAIN,
                                .name = ast->data.chain_access.full_key,
                                .payload = ast,
                                .hash = cxpr_hash_string(ast->data.chain_access.full_key),
                            },
                            err);

    case CXPR_NODE_PRODUCER_ACCESS: {
        cxpr_func_entry* entry = cxpr_registry_find(reg, ast->data.producer_access.name);
        char* const_key = NULL;
        double* const_args = NULL;
        const cxpr_ast* ordered_args[CXPR_MAX_CALL_ARGS] = {0};
        cxpr_error_code bind_code = CXPR_OK;
        const char* bind_message = NULL;
        if (entry && entry->ast_func_handler) {
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = CXPR_OP_CALL_AST,
                                    .ast = ast,
                                },
                                err);
        }
        if (!entry || (!entry->struct_producer && !entry->model_producer &&
                       entry->defined_return_field_count == 0u)) {
            if (err) {
                err->code = CXPR_ERR_UNKNOWN_FUNCTION;
                err->message = cxpr_ir_unknown_function_message(ast->data.producer_access.name);
            }
            return false;
        }
        if (!cxpr_call_bind_args(ast, entry, ordered_args, &bind_code, &bind_message)) {
            if (err) {
                err->code = bind_code;
                err->message = bind_message;
            }
            return false;
        }
        if (entry->defined_return_field_count > 0u && !entry->model_producer) {
            for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
                if (!cxpr_ir_compile_node(ordered_args[i], program, reg,
                                          local_names, local_count, subst, inline_depth, err)) {
                    return false;
                }
            }
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){
                                  .op = CXPR_OP_CALL_DEFINED,
                                  .func = entry,
                                  .payload = ast,
                                  .index = ast->data.producer_access.argc,
                              },
                              err)) {
                return false;
            }
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = CXPR_OP_GET_FIELD,
                                    .name = ast->data.producer_access.field,
                                },
                                err);
        }
        const_key = cxpr_ir_build_constant_producer_key(ast->data.producer_access.name,
                                                        ordered_args,
                                                        ast->data.producer_access.argc,
                                                        reg);
        if (const_key) {
            const_args = (double*)calloc(ast->data.producer_access.argc ? ast->data.producer_access.argc : 1,
                                         sizeof(double));
            if (!const_args) {
                free(const_key);
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Out of memory";
                }
                return false;
            }
            for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
                if (!cxpr_ir_constant_value(ordered_args[i], reg, &const_args[i])) {
                    free(const_args);
                    free(const_key);
                    break;
                }
            }
        }
        if (const_key && const_args) {
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){
                                  .op = CXPR_OP_CALL_PRODUCER_CONST_FIELD,
                                  .func = entry,
                                  .name = const_key,
                                  .aux_name = ast->data.producer_access.field,
                                  .payload = const_args,
                                  .index = ast->data.producer_access.argc,
                              },
                              err)) {
                free(const_args);
                free(const_key);
                return false;
            }
            return true;
        }
        free(const_args);
        free(const_key);
        for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
            if (!cxpr_ir_compile_node(ordered_args[i], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
        }
        if (!cxpr_ir_emit(program,
                          (cxpr_ir_instr){
                              .op = CXPR_OP_CALL_PRODUCER,
                              .func = entry,
                              .name = ast->data.producer_access.name,
                              .index = ast->data.producer_access.argc,
                          },
                          err)) {
            return false;
        }
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_GET_FIELD,
                                .name = ast->data.producer_access.field,
                            },
                            err);
    }

    case CXPR_NODE_LOOKBACK:
    {
        unsigned offset;
        const cxpr_ast* target = ast->data.lookback.target;
        if (cxpr_lookback_literal_offset(
                ast->data.lookback.index, &offset, NULL, NULL)) {
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){
                                  .op = CXPR_OP_LOOKBACK_PUSH,
                                  .index = offset,
                              },
                              err)) {
                return false;
            }
            if (!cxpr_ir_compile_node(target, program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_LOOKBACK_POP },
                                err);
        }
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_CALL_AST,
                                .ast = ast,
                            },
                            err);
    }

    case CXPR_NODE_FUNCTION_CALL: {
        cxpr_func_entry* entry = cxpr_registry_find(reg, ast->data.function_call.name);
        const char* fname = ast->data.function_call.name;
        if (cxpr_ast_call_uses_named_args(ast)) {
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = CXPR_OP_CALL_AST,
                                    .ast = ast,
                                },
                                err);
        }
        if (strcmp(fname, "if") == 0 && ast->data.function_call.argc == 3) {
            size_t false_jump, end_jump;
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            false_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP_IF_FALSE }, err)) {
                return false;
            }

            if (!cxpr_ir_compile_node(ast->data.function_call.args[1], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            end_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP }, err)) {
                return false;
            }

            cxpr_ir_patch_target(program, false_jump, cxpr_ir_next_index(program));
            if (!cxpr_ir_compile_node(ast->data.function_call.args[2], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            cxpr_ir_patch_target(program, end_jump, cxpr_ir_next_index(program));
            return true;
        }
        if (!entry) {
            if (!cxpr_ir_is_special_builtin_name(fname)) {
                if (err) {
                    err->code = CXPR_ERR_UNKNOWN_FUNCTION;
                    err->message = cxpr_ir_unknown_function_message(fname);
                }
                return false;
            }
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = CXPR_OP_CALL_AST,
                                    .ast = ast,
                                },
                                err);
        }

        if (strcmp(fname, "if") == 0 && ast->data.function_call.argc == 3) {
            size_t false_jump, end_jump;
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            false_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP_IF_FALSE }, err)) {
                return false;
            }

            if (!cxpr_ir_compile_node(ast->data.function_call.args[1], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            end_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP }, err)) {
                return false;
            }

            cxpr_ir_patch_target(program, false_jump, cxpr_ir_next_index(program));
            if (!cxpr_ir_compile_node(ast->data.function_call.args[2], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }

            cxpr_ir_patch_target(program, end_jump, cxpr_ir_next_index(program));
            return true;
        }

        if (strcmp(fname, "sqrt") == 0 && ast->data.function_call.argc == 1) {
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_SQRT }, err);
        }

        if (strcmp(fname, "abs") == 0 && ast->data.function_call.argc == 1) {
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_ABS }, err);
        }

        if (strcmp(fname, "pow") == 0 && ast->data.function_call.argc == 2) {
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            if (!cxpr_ir_compile_node(ast->data.function_call.args[1], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_POW }, err);
        }

        if (strcmp(fname, "sign") == 0 && ast->data.function_call.argc == 1) {
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_SIGN }, err);
        }

        if (strcmp(fname, "floor") == 0 && ast->data.function_call.argc == 1) {
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_FLOOR }, err);
        }

        if (strcmp(fname, "ceil") == 0 && ast->data.function_call.argc == 1) {
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_CEIL }, err);
        }

        if (strcmp(fname, "round") == 0 && ast->data.function_call.argc == 1) {
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_ROUND }, err);
        }

        if (strcmp(fname, "clamp") == 0 && ast->data.function_call.argc == 3) {
            size_t i;
            for (i = 0; i < 3; ++i) {
                if (!cxpr_ir_compile_node(ast->data.function_call.args[i], program, reg,
                                          local_names, local_count, subst, inline_depth, err)) {
                    return false;
                }
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_CLAMP }, err);
        }

        if ((strcmp(fname, "min") == 0 || strcmp(fname, "max") == 0) &&
            ast->data.function_call.argc >= 1 &&
            ast->data.function_call.argc <= 8) {
            size_t i;
            for (i = 0; i < ast->data.function_call.argc; ++i) {
                if (!cxpr_ir_compile_node(ast->data.function_call.args[i], program, reg,
                                          local_names, local_count, subst, inline_depth, err)) {
                    return false;
                }
            }
            return cxpr_ir_emit(
                program,
                (cxpr_ir_instr){
                    .op = CXPR_OP_CALL_FUNC,
                    .func = entry,
                    .payload = ast,
                    .index = ast->data.function_call.argc,
                },
                err);
        }

        if (entry->ast_func) {
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = CXPR_OP_CALL_AST,
                                    .ast = ast,
                                },
                                err);
        }

        if (entry->ast_func_handler &&
            (cxpr_ir_ast_contains_string_literal(ast) ||
             cxpr_ir_runtime_call_needs_catchor_passthrough(ast))) {
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = CXPR_OP_CALL_AST,
                                    .ast = ast,
                                },
                                err);
        }

        if ((entry->sync_func || entry->value_func || entry->typed_func) &&
            !entry->struct_fields && !entry->defined_body) {
            size_t i;
            for (i = 0; i < ast->data.function_call.argc; ++i) {
                if (!cxpr_ir_compile_node(ast->data.function_call.args[i], program, reg,
                                          local_names, local_count, subst, inline_depth, err)) {
                    return false;
                }
            }
            if (entry->native_kind == CXPR_NATIVE_KIND_UNARY &&
                ast->data.function_call.argc == 1) {
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = CXPR_OP_CALL_UNARY,
                                        .func = entry,
                                        .payload = ast,
                                        .index = 1,
                                    },
                                    err);
            }
            if (entry->native_kind == CXPR_NATIVE_KIND_BINARY &&
                ast->data.function_call.argc == 2) {
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = CXPR_OP_CALL_BINARY,
                                        .func = entry,
                                        .payload = ast,
                                        .index = 2,
                                    },
                                    err);
            }
            if (entry->native_kind == CXPR_NATIVE_KIND_TERNARY &&
                ast->data.function_call.argc == 3) {
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = CXPR_OP_CALL_TERNARY,
                                        .func = entry,
                                        .payload = ast,
                                        .index = 3,
                                    },
                                    err);
            }
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = CXPR_OP_CALL_FUNC,
                                    .func = entry,
                                    .payload = ast,
                                    .index = ast->data.function_call.argc,
                                },
                                err);
        }

        if (entry->struct_producer && !entry->sync_func) {
            char* const_key = NULL;
            for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
                if (!cxpr_ir_compile_node(ast->data.function_call.args[i], program, reg,
                                          local_names, local_count, subst, inline_depth, err)) {
                    return false;
                }
            }
            const_key = cxpr_ir_build_constant_producer_key(ast->data.function_call.name,
                                                            (const cxpr_ast* const*)ast->data.function_call.args,
                                                            ast->data.function_call.argc,
                                                            reg);
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){
                                  .op = const_key ? CXPR_OP_CALL_PRODUCER_CONST
                                                  : CXPR_OP_CALL_PRODUCER,
                                  .func = entry,
                                  .name = const_key ? const_key : ast->data.function_call.name,
                                  .index = ast->data.function_call.argc,
                              },
                              err)) {
                free(const_key);
                return false;
            }
            return true;
        }

        if (entry->defined_body &&
            cxpr_ir_emit_defined_direct_field_call(entry, ast, program, err)) {
            return true;
        }

        if (entry->defined_return_field_count > 0u) {
            size_t i;
            for (i = 0; i < ast->data.function_call.argc; ++i) {
                if (!cxpr_ir_compile_node(ast->data.function_call.args[i], program, reg,
                                          local_names, local_count, subst, inline_depth, err)) {
                    return false;
                }
            }
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = CXPR_OP_CALL_DEFINED,
                                    .func = entry,
                                    .payload = ast,
                                    .index = ast->data.function_call.argc,
                                },
                                err);
        }

        if (entry->defined_body && cxpr_ir_defined_call_can_inline(entry, ast, reg)) {
            if (inline_depth < CXPR_IR_INLINE_DEPTH_LIMIT) {
                cxpr_ir_subst_frame frame = {
                    .names = (const char* const*)entry->defined_param_names,
                    .args = (const cxpr_ast* const*)ast->data.function_call.args,
                    .count = ast->data.function_call.argc,
                    .parent = subst,
                };
                return cxpr_ir_compile_node(entry->defined_body, program, reg,
                                            local_names, local_count,
                                            &frame, inline_depth + 1, err);
            }

            {
                size_t i;
                for (i = 0; i < ast->data.function_call.argc; ++i) {
                    if (!cxpr_ir_compile_node(ast->data.function_call.args[i], program, reg,
                                              local_names, local_count, subst, inline_depth,
                                              err)) {
                        return false;
                    }
                }
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = CXPR_OP_CALL_DEFINED,
                                        .func = entry,
                                        .payload = ast,
                                        .index = ast->data.function_call.argc,
                                    },
                                    err);
            }
        }

        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_CALL_AST,
                                .ast = ast,
                            },
                            err);
    }

    case CXPR_NODE_BINARY_OP:
        if (ast->data.binary_op.op == CXPR_TOK_AND) {
            size_t left_false_jump, right_false_jump, end_jump;
            cxpr_value left_const;

            if (cxpr_ir_constant_typed_value(ast->data.binary_op.left, reg, &left_const) &&
                left_const.type == CXPR_VALUE_BOOL) {
                if (!left_const.b) {
                    return cxpr_ir_emit(program,
                                        (cxpr_ir_instr){ .op = CXPR_OP_PUSH_BOOL, .value = 0.0 },
                                        err);
                }
                return cxpr_ir_compile_node(ast->data.binary_op.right, program, reg,
                                            local_names, local_count, subst, inline_depth,
                                            err);
            }

            if (!cxpr_ir_compile_node(ast->data.binary_op.left, program, reg,
                                      local_names, local_count, subst, inline_depth,
                                      err)) return false;
            left_false_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_JUMP_IF_FALSE },
                              err)) return false;

            if (!cxpr_ir_compile_node(ast->data.binary_op.right, program, reg,
                                      local_names, local_count, subst, inline_depth,
                                      err)) return false;
            right_false_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_JUMP_IF_FALSE },
                              err)) return false;

            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_PUSH_BOOL, .value = 1.0 },
                              err)) return false;
            end_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP }, err)) return false;

            cxpr_ir_patch_target(program, left_false_jump, cxpr_ir_next_index(program));
            cxpr_ir_patch_target(program, right_false_jump, cxpr_ir_next_index(program));
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_PUSH_BOOL, .value = 0.0 },
                              err)) return false;

            cxpr_ir_patch_target(program, end_jump, cxpr_ir_next_index(program));
            return true;
        }

        if (ast->data.binary_op.op == CXPR_TOK_OR) {
            size_t left_true_jump, right_false_jump, end_jump;
            cxpr_value left_const;

            if (cxpr_ir_constant_typed_value(ast->data.binary_op.left, reg, &left_const) &&
                left_const.type == CXPR_VALUE_BOOL) {
                if (left_const.b) {
                    return cxpr_ir_emit(program,
                                        (cxpr_ir_instr){ .op = CXPR_OP_PUSH_BOOL, .value = 1.0 },
                                        err);
                }
                return cxpr_ir_compile_node(ast->data.binary_op.right, program, reg,
                                            local_names, local_count, subst, inline_depth,
                                            err);
            }

            if (!cxpr_ir_compile_node(ast->data.binary_op.left, program, reg,
                                      local_names, local_count, subst, inline_depth,
                                      err)) return false;
            left_true_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_JUMP_IF_TRUE },
                              err)) return false;

            if (!cxpr_ir_compile_node(ast->data.binary_op.right, program, reg,
                                      local_names, local_count, subst, inline_depth,
                                      err)) return false;
            right_false_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_JUMP_IF_FALSE },
                              err)) return false;

            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_PUSH_BOOL, .value = 1.0 },
                              err)) return false;
            end_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP }, err)) return false;

            cxpr_ir_patch_target(program, left_true_jump, cxpr_ir_next_index(program));
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_PUSH_BOOL, .value = 1.0 },
                              err)) return false;
            size_t skip_false_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP }, err)) return false;

            cxpr_ir_patch_target(program, right_false_jump, cxpr_ir_next_index(program));
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_PUSH_BOOL, .value = 0.0 },
                              err)) return false;

            cxpr_ir_patch_target(program, end_jump, cxpr_ir_next_index(program));
            cxpr_ir_patch_target(program, skip_false_jump, cxpr_ir_next_index(program));
            return true;
        }

        if (ast->data.binary_op.op == CXPR_TOK_STAR &&
            cxpr_ir_ast_equal(ast->data.binary_op.left, ast->data.binary_op.right)) {
            if (cxpr_ir_emit_leaf_load(ast->data.binary_op.left, program,
                                       local_names, local_count, subst, true, err)) {
                return true;
            }
            if (!cxpr_ir_compile_node(ast->data.binary_op.left, program, reg,
                                      local_names, local_count, subst, inline_depth,
                                      err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_SQUARE }, err);
        }

        if (!cxpr_ir_compile_node(ast->data.binary_op.left, program, reg,
                                  local_names, local_count, subst, inline_depth,
                                  err)) {
            return false;
        }
        if (!cxpr_ir_compile_node(ast->data.binary_op.right, program, reg,
                                  local_names, local_count, subst, inline_depth,
                                  err)) {
            return false;
        }
        switch (ast->data.binary_op.op) {
        case CXPR_TOK_PLUS:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_ADD, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_MINUS:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_SUB, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_STAR:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_MUL, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_SLASH:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_DIV, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_PERCENT:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_MOD, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_POWER:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_POW, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_EQ:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_CMP_EQ, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_NEQ:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_CMP_NEQ, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_LT:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_CMP_LT, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_LTE:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_CMP_LTE, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_GT:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_CMP_GT, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_GTE:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_CMP_GTE, .value = 0.0, .name = NULL },
                                err);
        default:
            if (err) {
                err->code = CXPR_ERR_SYNTAX;
                err->message = "IR v1 currently supports arithmetic and comparison binary operators";
            }
            return false;
        }

    case CXPR_NODE_UNARY_OP:
        if (ast->data.unary_op.op == CXPR_TOK_NOT) {
            if (!cxpr_ir_compile_node(ast->data.unary_op.operand, program, reg,
                                      local_names, local_count, subst, inline_depth,
                                      err)) return false;
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_NOT }, err);
        }

        if (ast->data.unary_op.op != CXPR_TOK_MINUS) {
            if (err) {
                err->code = CXPR_ERR_SYNTAX;
                err->message = "IR currently supports only unary minus and not";
            }
            return false;
        }
        if (!cxpr_ir_compile_node(ast->data.unary_op.operand, program, reg,
                                  local_names, local_count, subst, inline_depth,
                                  err)) {
            return false;
        }
        return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_NEG }, err);

    case CXPR_NODE_TERNARY: {
        size_t false_jump, end_jump;

        if (!cxpr_ir_compile_node(ast->data.ternary.condition, program, reg,
                                  local_names, local_count, subst, inline_depth,
                                  err)) return false;
        false_jump = cxpr_ir_next_index(program);
        if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP_IF_FALSE }, err)) {
            return false;
        }

        if (!cxpr_ir_compile_node(ast->data.ternary.true_branch, program, reg,
                                  local_names, local_count, subst, inline_depth,
                                  err)) return false;
        end_jump = cxpr_ir_next_index(program);
        if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP }, err)) return false;

        cxpr_ir_patch_target(program, false_jump, cxpr_ir_next_index(program));
        if (!cxpr_ir_compile_node(ast->data.ternary.false_branch, program, reg,
                                  local_names, local_count, subst, inline_depth,
                                  err)) return false;

        cxpr_ir_patch_target(program, end_jump, cxpr_ir_next_index(program));
        return true;
    }

    default:
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message =
                "IR currently supports numeric literals, identifiers, parameters, field access, function-call fallback, unary -, not, arithmetic, comparisons, logical and/or, and ternary";
        }
        return false;
    }
}
