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

static bool cxpr_ir_index_has_literal_array_base(const cxpr_expr_ast* target) {
    while (target && target->type == CXPR_NODE_INDEX) {
        target = target->data.index.target;
    }
    return target && target->type == CXPR_NODE_ARRAY;
}

static bool cxpr_ir_index_has_scalar_literal_base(const cxpr_expr_ast* target) {
    while (target && target->type == CXPR_NODE_INDEX) {
        target = target->data.index.target;
    }
    if (!target) return false;
    return target->type == CXPR_NODE_NUMBER || target->type == CXPR_NODE_INT64 ||
           target->type == CXPR_NODE_BOOL || target->type == CXPR_NODE_STRING;
}

static const cxpr_expr_ast* cxpr_ir_static_record_field(
    const cxpr_expr_ast* record, const char* field) {
    if (!record || record->type != CXPR_NODE_RECORD || !field) return NULL;
    for (size_t i = 0u; i < record->data.record.field_count; ++i) {
        if (!strcmp(record->data.record.field_names[i], field))
            return record->data.record.field_values[i];
    }
    return NULL;
}

static bool cxpr_ir_is_reduce_acc(const cxpr_expr_ast* ast,
                                  const char* step_parameter) {
    if (!ast || !step_parameter) return false;
    if (ast->type == CXPR_NODE_FIELD_ACCESS) {
        if (strcmp(ast->data.field_access.field, "acc") != 0) return false;
        if (!ast->data.field_access.base)
            return ast->data.field_access.object &&
                   strcmp(ast->data.field_access.object, step_parameter) == 0;
        return ast->data.field_access.base->type == CXPR_NODE_IDENTIFIER &&
               strcmp(ast->data.field_access.base->data.identifier.name,
                      step_parameter) == 0;
    }
    return ast->type == CXPR_NODE_CHAIN_ACCESS &&
           ast->data.chain_access.depth == 2u &&
           strcmp(ast->data.chain_access.path[0], step_parameter) == 0 &&
           strcmp(ast->data.chain_access.path[1], "acc") == 0;
}

static bool cxpr_ir_contains_reduce_acc(const cxpr_expr_ast* ast,
                                        const char* step_parameter) {
    if (!ast) return false;
    if (cxpr_ir_is_reduce_acc(ast, step_parameter)) return true;
    switch (ast->type) {
    case CXPR_NODE_ARRAY:
        for (size_t i = 0u; i < ast->data.array.count; ++i)
            if (cxpr_ir_contains_reduce_acc(ast->data.array.elements[i],
                                            step_parameter)) return true;
        return false;
    case CXPR_NODE_RECORD:
        for (size_t i = 0u; i < ast->data.record.field_count; ++i)
            if (cxpr_ir_contains_reduce_acc(ast->data.record.field_values[i],
                                            step_parameter)) return true;
        return false;
    case CXPR_NODE_FUNCTION_CALL:
        for (size_t i = 0u; i < ast->data.function_call.argc; ++i)
            if (cxpr_ir_contains_reduce_acc(ast->data.function_call.args[i],
                                            step_parameter)) return true;
        return false;
    case CXPR_NODE_PRODUCER_ACCESS:
        for (size_t i = 0u; i < ast->data.producer_access.argc; ++i)
            if (cxpr_ir_contains_reduce_acc(ast->data.producer_access.args[i],
                                            step_parameter)) return true;
        return false;
    case CXPR_NODE_BINARY_OP:
        return cxpr_ir_contains_reduce_acc(ast->data.binary_op.left,
                                           step_parameter) ||
               cxpr_ir_contains_reduce_acc(ast->data.binary_op.right,
                                           step_parameter);
    case CXPR_NODE_UNARY_OP:
        return cxpr_ir_contains_reduce_acc(ast->data.unary_op.operand,
                                           step_parameter);
    case CXPR_NODE_TERNARY:
        return cxpr_ir_contains_reduce_acc(ast->data.ternary.condition,
                                           step_parameter) ||
               cxpr_ir_contains_reduce_acc(ast->data.ternary.true_branch,
                                           step_parameter) ||
               cxpr_ir_contains_reduce_acc(ast->data.ternary.false_branch,
                                           step_parameter);
    case CXPR_NODE_INDEX:
        return cxpr_ir_contains_reduce_acc(ast->data.index.target,
                                           step_parameter) ||
               cxpr_ir_contains_reduce_acc(ast->data.index.index,
                                           step_parameter);
    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_ir_contains_reduce_acc(ast->data.field_access.base,
                                           step_parameter);
    default:
        return false;
    }
}

static bool cxpr_ir_reduce_stack_opcode(int token, cxpr_opcode* opcode) {
    switch (token) {
    case CXPR_TOK_PLUS: *opcode = CXPR_OP_ADD; return true;
    case CXPR_TOK_MINUS: *opcode = CXPR_OP_SUB; return true;
    case CXPR_TOK_STAR: *opcode = CXPR_OP_MUL; return true;
    case CXPR_TOK_SLASH: *opcode = CXPR_OP_DIV; return true;
    case CXPR_TOK_PERCENT: *opcode = CXPR_OP_MOD; return true;
    case CXPR_TOK_POWER: *opcode = CXPR_OP_POW; return true;
    default: return false;
    }
}

static cxpr_ir_lookback_payload* cxpr_ir_lookback_payload_new(
    const cxpr_expr_ast* target, const cxpr_registry* reg) {
    cxpr_ir_lookback_payload* payload =
        (cxpr_ir_lookback_payload*)calloc(1u, sizeof(*payload));
    const cxpr_index_capability_entry* capability;
    cxpr_error ignored = {0};
    bool handled = false;

    if (!payload) return NULL;
    payload->target = target;
    payload->compile_registry = reg;
    if (target && target->type == CXPR_NODE_IDENTIFIER) {
        payload->kind = CXPR_IR_LOOKBACK_IDENT_ARRAY;
        payload->key = target->data.identifier.name;
    } else if (target && target->type == CXPR_NODE_FIELD_ACCESS) {
        payload->kind = CXPR_IR_LOOKBACK_PRODUCER_FIELD_CHAIN;
        payload->key = target->data.field_access.full_key;
    } else if (target && target->type == CXPR_NODE_CHAIN_ACCESS) {
        payload->kind = CXPR_IR_LOOKBACK_PRODUCER_FIELD_CHAIN;
        payload->key = target->data.chain_access.full_key;
        payload->segments = target->data.chain_access.path;
        payload->segment_count = target->data.chain_access.depth;
    } else {
        payload->kind = CXPR_IR_LOOKBACK_RESOLVER;
    }
    capability = cxpr_registry_select_index_capability(reg, target, &ignored, &handled);
    if (capability && ignored.code == CXPR_OK) {
        payload->capability = capability->resolve;
        payload->capability_userdata = capability->userdata;
    }
    payload->capability_handled = handled;
    if (reg) {
        payload->resolver = reg->lookback_resolver;
        payload->resolver_userdata = reg->lookback_userdata;
        if (payload->kind == CXPR_IR_LOOKBACK_IDENT_ARRAY && payload->key) {
            payload->column_bound = cxpr_registry_column_lookback_binding(
                reg, payload->key, &payload->column);
        }
    }
    return payload;
}

static bool cxpr_ir_emit_defined_direct_field_call(cxpr_func_entry* entry,
                                                   const cxpr_expr_ast* call_ast,
                                                   cxpr_ir_program* program,
                                                   cxpr_error* err) {
    const cxpr_expr_ast* body;
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
        const cxpr_expr_ast* arg;

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

static bool cxpr_ir_defined_body_needs_ast_eval(const cxpr_expr_ast* ast,
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
    case CXPR_NODE_INDEX:
        return cxpr_ir_defined_body_needs_ast_eval(ast->data.index.target, reg, depth) ||
               cxpr_ir_defined_body_needs_ast_eval(ast->data.index.index, reg, depth);
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
                                            const cxpr_expr_ast* call_ast,
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
            return false;
        }
    }
    return true;
}

static bool cxpr_ir_emit_field_binary_op(int op, cxpr_ir_program* program,
                                         cxpr_error* err) {
    switch (op) {
    case CXPR_TOK_PLUS:
        return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_ADD }, err);
    case CXPR_TOK_MINUS:
        return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_SUB }, err);
    case CXPR_TOK_STAR:
        return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_MUL }, err);
    case CXPR_TOK_SLASH:
        return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_DIV }, err);
    default:
        return false;
    }
}

static bool cxpr_ir_is_literal_scalar(const cxpr_expr_ast* ast) {
    return ast && (ast->type == CXPR_NODE_NUMBER || ast->type == CXPR_NODE_INT64);
}

static unsigned long cxpr_ir_named_field_hash(const char* root, const char* field) {
    char key[256];
    const int written = snprintf(key, sizeof(key), "%s.%s", root, field);
    return written > 0 && (size_t)written < sizeof(key) ? cxpr_hash_string(key) : 0u;
}

static bool cxpr_ir_can_compile_selected_field(const cxpr_expr_ast* ast,
                                               const char* field) {
    if (!ast || !field) return false;
    if (ast->type == CXPR_NODE_IDENTIFIER) return true;
    if (ast->type == CXPR_NODE_RECORD) {
        for (size_t i = 0u; i < ast->data.record.field_count; ++i) {
            if (strcmp(ast->data.record.field_names[i], field) == 0) return true;
        }
        return false;
    }
    if (ast->type == CXPR_NODE_BINARY_OP &&
        (ast->data.binary_op.op == CXPR_TOK_PLUS ||
         ast->data.binary_op.op == CXPR_TOK_MINUS ||
         ast->data.binary_op.op == CXPR_TOK_STAR ||
         ast->data.binary_op.op == CXPR_TOK_SLASH)) {
        const cxpr_expr_ast* left = ast->data.binary_op.left;
        const cxpr_expr_ast* right = ast->data.binary_op.right;
        const bool left_scalar = cxpr_ir_is_literal_scalar(left);
        const bool right_scalar = cxpr_ir_is_literal_scalar(right);
        if (left_scalar == right_scalar && left_scalar) return false;
        return (left_scalar || cxpr_ir_can_compile_selected_field(left, field)) &&
               (right_scalar || cxpr_ir_can_compile_selected_field(right, field));
    }
    return false;
}

static bool cxpr_ir_compile_selected_field(const cxpr_expr_ast* ast, const char* field,
                                           cxpr_ir_program* program,
                                           const cxpr_registry* reg,
                                           const char* const* local_names,
                                           size_t local_count,
                                           const cxpr_ir_subst_frame* subst,
                                           size_t inline_depth,
                                           cxpr_error* err) {
    if (!ast || !field) return false;

    if (ast->type == CXPR_NODE_IDENTIFIER) {
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_LOAD_NAMED_FIELD,
                                .name = ast->data.identifier.name,
                                .aux_name = field,
                                .hash = cxpr_ir_named_field_hash(ast->data.identifier.name,
                                                                 field),
                            },
                            err);
    }

    if (ast->type == CXPR_NODE_RECORD) {
        for (size_t i = 0u; i < ast->data.record.field_count; ++i) {
            if (strcmp(ast->data.record.field_names[i], field) == 0) {
                return cxpr_ir_compile_node(ast->data.record.field_values[i], program, reg,
                                            local_names, local_count, subst, inline_depth, err);
            }
        }
        return false;
    }

    if (ast->type == CXPR_NODE_BINARY_OP &&
        (ast->data.binary_op.op == CXPR_TOK_PLUS ||
         ast->data.binary_op.op == CXPR_TOK_MINUS ||
         ast->data.binary_op.op == CXPR_TOK_STAR ||
         ast->data.binary_op.op == CXPR_TOK_SLASH)) {
        const cxpr_expr_ast* left = ast->data.binary_op.left;
        const cxpr_expr_ast* right = ast->data.binary_op.right;
        const bool left_scalar = cxpr_ir_is_literal_scalar(left);
        const bool right_scalar = cxpr_ir_is_literal_scalar(right);

        if (left_scalar == right_scalar && left_scalar) return false;
        if (!(left_scalar ? cxpr_ir_compile_node(left, program, reg, local_names, local_count,
                                                 subst, inline_depth, err)
                          : cxpr_ir_compile_selected_field(left, field, program, reg, local_names,
                                                           local_count, subst, inline_depth, err))) {
            return false;
        }
        if (!(right_scalar ? cxpr_ir_compile_node(right, program, reg, local_names, local_count,
                                                   subst, inline_depth, err)
                           : cxpr_ir_compile_selected_field(right, field, program, reg, local_names,
                                                            local_count, subst, inline_depth, err))) {
            return false;
        }
        return cxpr_ir_emit_field_binary_op(ast->data.binary_op.op, program, err);
    }

    return false;
}

static bool cxpr_ir_compile_predicate_jumps(
    const cxpr_expr_ast* ast, int flatten_op, cxpr_opcode jump_op,
    cxpr_ir_program* program, const cxpr_registry* reg,
    const char* const* local_names, size_t local_count,
    const cxpr_ir_subst_frame* subst, size_t inline_depth,
    size_t** jumps, size_t* jump_count, size_t* jump_capacity,
    cxpr_error* err) {
    if (ast && ast->type == CXPR_NODE_BINARY_OP &&
        ast->data.binary_op.op == flatten_op) {
        return cxpr_ir_compile_predicate_jumps(
                   ast->data.binary_op.left, flatten_op, jump_op, program, reg,
                   local_names, local_count, subst, inline_depth,
                   jumps, jump_count, jump_capacity, err) &&
               cxpr_ir_compile_predicate_jumps(
                   ast->data.binary_op.right, flatten_op, jump_op, program, reg,
                   local_names, local_count, subst, inline_depth,
                   jumps, jump_count, jump_capacity, err);
    }
    if (!cxpr_ir_compile_node(ast, program, reg, local_names, local_count,
                              subst, inline_depth, err)) return false;
    if (*jump_count == *jump_capacity) {
        size_t next_capacity = *jump_capacity ? *jump_capacity * 2u : 8u;
        size_t* grown = realloc(*jumps, next_capacity * sizeof(**jumps));
        if (!grown) {
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return false;
        }
        *jumps = grown;
        *jump_capacity = next_capacity;
    }
    (*jumps)[(*jump_count)++] = cxpr_ir_next_index(program);
    return cxpr_ir_emit(program, (cxpr_ir_instr){.op = jump_op}, err);
}

bool cxpr_ir_compile_node(const cxpr_expr_ast* ast, cxpr_ir_program* program,
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

    case CXPR_NODE_INT64:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_PUSH_INT64,
                                .int64_value = ast->data.integer.value,
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
            const cxpr_expr_ast* mapped = cxpr_ir_subst_lookup(subst, ast->data.identifier.name, &owner);
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
        if (!ast->data.field_access.base && ast->data.field_access.object) {
            for (const cxpr_ir_subst_frame* frame = subst; frame; frame = frame->parent) {
                if (!frame->reduce_object ||
                    strcmp(ast->data.field_access.object, frame->reduce_object)) continue;
                if (!strcmp(ast->data.field_access.field, "acc"))
                    return cxpr_ir_emit(program, (cxpr_ir_instr){.op=CXPR_OP_LOAD_TEMP,.index=frame->reduce_temp_index}, err);
                if (!strcmp(ast->data.field_access.field, "value"))
                    return cxpr_ir_compile_node(frame->reduce_value, program, reg,
                                                local_names, local_count, frame->parent,
                                                inline_depth, err);
                if (!strcmp(ast->data.field_access.field, "index"))
                    return cxpr_ir_emit(program, (cxpr_ir_instr){.op=CXPR_OP_PUSH_CONST,.value=(double)frame->reduce_index}, err);
            }
        }
        if (ast->data.field_access.base) {
            const cxpr_expr_ast* base = ast->data.field_access.base;
            if (base->type == CXPR_NODE_IDENTIFIER) {
                for (const cxpr_ir_subst_frame* frame = subst; frame; frame = frame->parent) {
                    if (!frame->reduce_object ||
                        strcmp(base->data.identifier.name, frame->reduce_object)) continue;
                    if (!strcmp(ast->data.field_access.field, "acc")) {
                        return cxpr_ir_emit(program, (cxpr_ir_instr){
                            .op = CXPR_OP_LOAD_TEMP, .index = frame->reduce_temp_index,
                        }, err);
                    }
                    if (!strcmp(ast->data.field_access.field, "value")) {
                        return cxpr_ir_compile_node(frame->reduce_value, program, reg,
                                                    local_names, local_count,
                                                    frame->parent, inline_depth, err);
                    }
                    if (!strcmp(ast->data.field_access.field, "index")) {
                        return cxpr_ir_emit(program, (cxpr_ir_instr){
                            .op = CXPR_OP_PUSH_CONST, .value = (double)frame->reduce_index,
                        }, err);
                    }
                }
            }
            if (base->type == CXPR_NODE_IDENTIFIER) {
                char local_field[512];
                int written = snprintf(local_field, sizeof(local_field), "%s.%s",
                                       base->data.identifier.name,
                                       ast->data.field_access.field);
                if (written > 0 && (size_t)written < sizeof(local_field)) {
                    size_t local_index = cxpr_ir_local_index(
                        local_field, local_names, local_count);
                    if (local_index != (size_t)-1) {
                        return cxpr_ir_emit(program,
                                            (cxpr_ir_instr){
                                                .op = CXPR_OP_LOAD_LOCAL,
                                                .index = local_index,
                                            },
                                            err);
                    }
                }
            }
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
            if (cxpr_ir_can_compile_selected_field(base, ast->data.field_access.field)) {
                return cxpr_ir_compile_selected_field(base, ast->data.field_access.field,
                                                      program, reg, local_names, local_count,
                                                      subst, inline_depth, err);
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
        if (subst && ast->data.field_access.object) {
            const cxpr_ir_subst_frame* owner = NULL;
            const cxpr_expr_ast* mapped =
                cxpr_ir_subst_lookup(subst, ast->data.field_access.object, &owner);
            const cxpr_expr_ast* mapped_field = cxpr_ir_static_record_field(
                mapped, ast->data.field_access.field);
            if (mapped_field) {
                return cxpr_ir_compile_node(mapped_field, program, reg,
                                            local_names, local_count,
                                            owner ? owner->parent : subst,
                                            inline_depth, err);
            }
            if (mapped && mapped->type == CXPR_NODE_IDENTIFIER) {
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = CXPR_OP_LOAD_NAMED_FIELD,
                                        .name = mapped->data.identifier.name,
                                        .aux_name = ast->data.field_access.field,
                                        .hash = cxpr_ir_named_field_hash(
                                            mapped->data.identifier.name,
                                            ast->data.field_access.field),
                                    },
                                    err);
            }
            if (mapped) {
                if (!cxpr_ir_compile_node(mapped, program, reg,
                                          local_names, local_count,
                                          owner ? owner->parent : subst,
                                          inline_depth, err)) {
                    return false;
                }
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = CXPR_OP_GET_FIELD,
                                        .name = ast->data.field_access.field,
                                    }, err);
            }
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
        if (ast->data.chain_access.depth == 3u) {
            for (const cxpr_ir_subst_frame* frame = subst; frame; frame = frame->parent) {
                const cxpr_expr_ast* field;
                if (!frame->reduce_object ||
                    strcmp(ast->data.chain_access.path[0], frame->reduce_object) ||
                    strcmp(ast->data.chain_access.path[1], "value")) continue;
                field = cxpr_ir_static_record_field(
                    frame->reduce_value, ast->data.chain_access.path[2]);
                if (!field) {
                    if (err) {
                        err->code = CXPR_ERR_TYPE_MISMATCH;
                        err->message =
                            "reduce value field requires a static record element";
                    }
                    return false;
                }
                return cxpr_ir_compile_node(field, program, reg,
                                            local_names, local_count, frame->parent,
                                            inline_depth, err);
            }
        }
        if (ast->data.chain_access.depth == 2u) {
            for (const cxpr_ir_subst_frame* frame = subst; frame; frame = frame->parent) {
                if (!frame->reduce_object ||
                    strcmp(ast->data.chain_access.path[0], frame->reduce_object)) continue;
                if (!strcmp(ast->data.chain_access.path[1], "acc"))
                    return cxpr_ir_emit(program, (cxpr_ir_instr){.op=CXPR_OP_LOAD_TEMP,.index=frame->reduce_temp_index}, err);
                if (!strcmp(ast->data.chain_access.path[1], "value"))
                    return cxpr_ir_compile_node(frame->reduce_value, program, reg,
                                                local_names, local_count, frame->parent,
                                                inline_depth, err);
                if (!strcmp(ast->data.chain_access.path[1], "index"))
                    return cxpr_ir_emit(program, (cxpr_ir_instr){.op=CXPR_OP_PUSH_CONST,.value=(double)frame->reduce_index}, err);
            }
        }
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
        const cxpr_expr_ast* ordered_args[CXPR_MAX_CALL_ARGS] = {0};
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
        if (!entry || (!entry->struct_producer && !entry->typed_struct_producer && !entry->model_producer &&
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
            size_t selected = entry->defined_return_field_count;
            bool can_inline_selected = true;
            for (size_t i = 0u; i < entry->defined_return_field_count; ++i) {
                if (strcmp(entry->defined_return_field_names[i],
                           ast->data.producer_access.field) == 0) {
                    selected = i;
                    break;
                }
            }
            for (size_t i = 0u; i < entry->defined_param_count; ++i) {
                if (entry->defined_param_fields && entry->defined_param_fields[i] &&
                    entry->defined_param_field_counts[i] > 0u &&
                    (!ordered_args[i] || ordered_args[i]->type != CXPR_NODE_IDENTIFIER)) {
                    can_inline_selected = false;
                    break;
                }
            }
            if (selected < entry->defined_return_field_count &&
                can_inline_selected &&
                inline_depth < CXPR_IR_INLINE_DEPTH_LIMIT) {
                cxpr_ir_subst_frame frame = {
                    .names = (const char* const*)entry->defined_param_names,
                    .args = (const cxpr_expr_ast* const*)ordered_args,
                    .count = ast->data.producer_access.argc,
                    .parent = subst,
                };
                return cxpr_ir_compile_node(entry->defined_return_field_bodies[selected],
                                            program, reg, local_names, local_count,
                                            &frame, inline_depth + 1u, err);
            }
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
        if (!entry->typed_struct_producer)
            const_key = cxpr_ir_build_constant_producer_key(ast->data.producer_access.name,
                                                            ordered_args,
                                                            ast->data.producer_access.argc,
                                                            reg);
        if (const_key) {
            const_args = (double*)calloc(ast->data.producer_access.argc + 1u,
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
            if (const_key && const_args) {
                size_t field_index = entry->fields_per_arg;
                for (size_t i = 0u; i < entry->fields_per_arg; ++i) {
                    if (strcmp(entry->struct_fields[i], ast->data.producer_access.field) == 0) {
                        field_index = i;
                        break;
                    }
                }
                if (field_index == entry->fields_per_arg) {
                    free(const_args);
                    free(const_key);
                    const_args = NULL;
                    const_key = NULL;
                } else {
                    const_args[ast->data.producer_access.argc] = (double)field_index;
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

    case CXPR_NODE_INDEX:
    {
        unsigned offset;
        const cxpr_expr_ast* target = ast->data.index.target;
        if (cxpr_ir_index_has_literal_array_base(target)) {
            if (!cxpr_ir_compile_node(target, program, reg,
                                      local_names, local_count, subst, inline_depth, err) ||
                !cxpr_ir_compile_node(ast->data.index.index, program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_INDEX }, err);
        }
        if (!cxpr_ir_index_has_scalar_literal_base(target) &&
            cxpr_lookback_literal_offset(
                ast->data.index.index, &offset, NULL, NULL)) {
            cxpr_ir_lookback_payload* payload;
            while (target && target->type == CXPR_NODE_INDEX) {
                unsigned inner_offset;
                unsigned summed;
                if (!cxpr_lookback_literal_offset(target->data.index.index,
                                                  &inner_offset, NULL, NULL) ||
                    !cxpr_lookback_add_unsigned(offset, inner_offset, &summed,
                                                NULL, NULL)) {
                    break;
                }
                offset = summed;
                target = target->data.index.target;
            }
            payload = cxpr_ir_lookback_payload_new(target, reg);
            if (!payload) {
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Out of memory";
                }
                return false;
            }
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){
                                  .op = CXPR_OP_LOOKBACK_RESOLVE,
                                  .index = offset,
                                  .payload = payload,
                              }, err)) {
                free(payload);
                return false;
            }
            return true;
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
        if (!strcmp(fname, "reduce") && ast->data.function_call.argc == 3u) {
            const cxpr_expr_ast* source = ast->data.function_call.args[0];
            const cxpr_expr_ast* initial = ast->data.function_call.args[1];
            const cxpr_expr_ast* lambda = ast->data.function_call.args[2];
            const cxpr_expr_ast* mapped_selector = NULL;
            const char* mapped_parameter = NULL;
            const cxpr_expr_ast* array;
            const char* step_parameter;
            const cxpr_expr_ast* step_body;
            const cxpr_expr_ast* stack_rhs = NULL;
            cxpr_opcode stack_op = CXPR_OP_ADD;
            size_t temp_index;
            if (source && source->type == CXPR_NODE_FUNCTION_CALL &&
                !strcmp(source->data.function_call.name, "map") &&
                source->data.function_call.argc == 2u) {
                const cxpr_expr_ast* map_lambda = source->data.function_call.args[1];
                if (!map_lambda || map_lambda->type != CXPR_NODE_FUNCTION_CALL ||
                    strcmp(map_lambda->data.function_call.name, "__cxpr_lambda") ||
                    map_lambda->data.function_call.argc != 2u ||
                    map_lambda->data.function_call.args[0]->type != CXPR_NODE_IDENTIFIER)
                    goto reduce_invalid;
                mapped_parameter = map_lambda->data.function_call.args[0]->data.identifier.name;
                mapped_selector = map_lambda->data.function_call.args[1];
                source = source->data.function_call.args[0];
            }
            if (!source || source->type != CXPR_NODE_ARRAY || !lambda ||
                lambda->type != CXPR_NODE_FUNCTION_CALL ||
                strcmp(lambda->data.function_call.name, "__cxpr_lambda") ||
                lambda->data.function_call.argc != 2u ||
                lambda->data.function_call.args[0]->type != CXPR_NODE_IDENTIFIER)
                goto reduce_invalid;
            array = source;
            step_parameter = lambda->data.function_call.args[0]->data.identifier.name;
            step_body = lambda->data.function_call.args[1];
            if (step_body->type == CXPR_NODE_BINARY_OP &&
                cxpr_ir_is_reduce_acc(step_body->data.binary_op.left,
                                      step_parameter) &&
                !cxpr_ir_contains_reduce_acc(step_body->data.binary_op.right,
                                             step_parameter)) {
                if (cxpr_ir_reduce_stack_opcode(step_body->data.binary_op.op,
                                                &stack_op))
                    stack_rhs = step_body->data.binary_op.right;
            }
            if (stack_rhs) {
                if (!cxpr_ir_compile_node(initial, program, reg, local_names,
                                          local_count, subst, inline_depth, err))
                    return false;
                for (size_t i = 0u; i < array->data.array.count; ++i) {
                    const cxpr_expr_ast* element = array->data.array.elements[i];
                    const char* map_names[] = {mapped_parameter};
                    const cxpr_expr_ast* map_args[] = {element};
                    cxpr_ir_subst_frame map_frame = {
                        .names = map_names, .args = map_args,
                        .count = mapped_selector ? 1u : 0u, .parent = subst,
                    };
                    cxpr_ir_subst_frame reduce_frame = {
                        .reduce_object = step_parameter,
                        .reduce_value = mapped_selector ? mapped_selector : element,
                        .reduce_index = i,
                        .parent = mapped_selector ? &map_frame : subst,
                    };
                    if (!cxpr_ir_compile_node(stack_rhs, program, reg, local_names,
                                              local_count, &reduce_frame,
                                              inline_depth, err) ||
                        !cxpr_ir_emit(program,
                                      (cxpr_ir_instr){.op = stack_op}, err))
                        return false;
                }
                return true;
            }
            temp_index = program->temp_count++;
            if (program->temp_count > CXPR_IR_STACK_CAPACITY) {
                if (err) { err->code = CXPR_ERR_OUT_OF_MEMORY; err->message = "Too many compiler temporaries"; }
                return false;
            }
            if (!cxpr_ir_compile_node(initial, program, reg, local_names, local_count,
                                      subst, inline_depth, err) ||
                !cxpr_ir_emit(program, (cxpr_ir_instr){.op=CXPR_OP_STORE_TEMP,.index=temp_index}, err))
                return false;
            for (size_t i = 0u; i < array->data.array.count; ++i) {
                const cxpr_expr_ast* element = array->data.array.elements[i];
                const char* map_names[] = {mapped_parameter};
                const cxpr_expr_ast* map_args[] = {element};
                cxpr_ir_subst_frame map_frame = {
                    .names = map_names, .args = map_args,
                    .count = mapped_selector ? 1u : 0u, .parent = subst,
                };
                cxpr_ir_subst_frame reduce_frame = {
                    .reduce_object = step_parameter,
                    .reduce_value = mapped_selector ? mapped_selector : element,
                    .reduce_index = i,
                    .reduce_temp_index = temp_index,
                    .parent = mapped_selector ? &map_frame : subst,
                };
                if (!cxpr_ir_compile_node(step_body, program, reg, local_names, local_count,
                                          &reduce_frame, inline_depth, err) ||
                    !cxpr_ir_emit(program, (cxpr_ir_instr){.op=CXPR_OP_STORE_TEMP,.index=temp_index}, err))
                    return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){.op=CXPR_OP_LOAD_TEMP,.index=temp_index}, err);
reduce_invalid:
            if (err) { err->code = CXPR_ERR_TYPE_MISMATCH; err->message = "reduce requires a static collection and one-parameter step selector"; }
            return false;
        }
        if (ast->data.function_call.argc >= 1u &&
            ast->data.function_call.argc <= 2u &&
            ast->data.function_call.args[0] &&
            (!strcmp(fname, "sum") || !strcmp(fname, "mean") ||
             !strcmp(fname, "min") || !strcmp(fname, "max") ||
             !strcmp(fname, "any") || !strcmp(fname, "all"))) {
            const cxpr_expr_ast* array = ast->data.function_call.args[0];
            const cxpr_expr_ast* selector = NULL;
            const cxpr_expr_ast* mapped_selector = NULL;
            const char* parameter = NULL;
            const char* mapped_parameter = NULL;
            size_t count;
            if (array->type == CXPR_NODE_FUNCTION_CALL &&
                !strcmp(array->data.function_call.name, "map") &&
                array->data.function_call.argc == 2u &&
                ast->data.function_call.argc == 1u) {
                const cxpr_expr_ast* lambda = array->data.function_call.args[1];
                if (!lambda || lambda->type != CXPR_NODE_FUNCTION_CALL ||
                    strcmp(lambda->data.function_call.name, "__cxpr_lambda") ||
                    lambda->data.function_call.argc != 2u ||
                    lambda->data.function_call.args[0]->type != CXPR_NODE_IDENTIFIER)
                    return false;
                mapped_parameter = lambda->data.function_call.args[0]->data.identifier.name;
                mapped_selector = lambda->data.function_call.args[1];
                array = array->data.function_call.args[0];
            }
            if (!array || array->type != CXPR_NODE_ARRAY) goto static_reduction_not_handled;
            count = array->data.array.count;
            if (ast->data.function_call.argc == 2u) {
                const cxpr_expr_ast* lambda = ast->data.function_call.args[1];
                if (!lambda || lambda->type != CXPR_NODE_FUNCTION_CALL ||
                    strcmp(lambda->data.function_call.name, "__cxpr_lambda") ||
                    lambda->data.function_call.argc != 2u ||
                    lambda->data.function_call.args[0]->type != CXPR_NODE_IDENTIFIER) {
                    if (err) {
                        err->code = CXPR_ERR_TYPE_MISMATCH;
                        err->message = "Static reduction requires a one-parameter selector";
                    }
                    return false;
                }
                parameter = lambda->data.function_call.args[0]->data.identifier.name;
                selector = lambda->data.function_call.args[1];
            }
            if (!strcmp(fname, "any") || !strcmp(fname, "all")) {
                const bool is_all = !strcmp(fname, "all");
                size_t* predicate_jumps = NULL;
                size_t predicate_jump_count = 0u;
                size_t predicate_jump_capacity = 0u;
                size_t end_jump;
                if (count == 0u) {
                    return cxpr_ir_emit(program,
                                        (cxpr_ir_instr){
                                            .op = CXPR_OP_PUSH_BOOL,
                                            .value = is_all ? 1.0 : 0.0,
                                        }, err);
                }
                for (size_t i = 0u; i < count; ++i) {
                    const cxpr_expr_ast* element = array->data.array.elements[i];
                    bool compiled;
                    if (selector) {
                        const char* names[] = {parameter};
                        const cxpr_expr_ast* args[] = {element};
                        cxpr_ir_subst_frame frame = {
                            .names = names, .args = args, .count = 1u, .parent = subst,
                        };
                        compiled = cxpr_ir_compile_predicate_jumps(
                            selector, is_all ? CXPR_TOK_AND : CXPR_TOK_OR,
                            is_all ? CXPR_OP_JUMP_IF_FALSE : CXPR_OP_JUMP_IF_TRUE,
                            program, reg, local_names, local_count, &frame,
                            inline_depth, &predicate_jumps, &predicate_jump_count,
                            &predicate_jump_capacity, err);
                    } else if (mapped_selector) {
                        const char* names[] = {mapped_parameter};
                        const cxpr_expr_ast* args[] = {element};
                        cxpr_ir_subst_frame frame = {.names=names,.args=args,.count=1u,.parent=subst};
                        compiled = cxpr_ir_compile_predicate_jumps(
                            mapped_selector, is_all ? CXPR_TOK_AND : CXPR_TOK_OR,
                            is_all ? CXPR_OP_JUMP_IF_FALSE : CXPR_OP_JUMP_IF_TRUE,
                            program, reg, local_names, local_count, &frame,
                            inline_depth, &predicate_jumps, &predicate_jump_count,
                            &predicate_jump_capacity, err);
                    } else {
                        compiled = cxpr_ir_compile_predicate_jumps(
                            element, is_all ? CXPR_TOK_AND : CXPR_TOK_OR,
                            is_all ? CXPR_OP_JUMP_IF_FALSE : CXPR_OP_JUMP_IF_TRUE,
                            program, reg, local_names, local_count, subst,
                            inline_depth, &predicate_jumps, &predicate_jump_count,
                            &predicate_jump_capacity, err);
                    }
                    if (!compiled) {
                        free(predicate_jumps);
                        return false;
                    }
                }
                if (!cxpr_ir_emit(program,
                                  (cxpr_ir_instr){
                                      .op = CXPR_OP_PUSH_BOOL,
                                      .value = is_all ? 1.0 : 0.0,
                                  }, err)) {
                    free(predicate_jumps);
                    return false;
                }
                end_jump = cxpr_ir_next_index(program);
                if (!cxpr_ir_emit(program,
                                  (cxpr_ir_instr){.op = CXPR_OP_JUMP}, err)) {
                    free(predicate_jumps);
                    return false;
                }
                for (size_t i = 0u; i < predicate_jump_count; ++i) {
                    cxpr_ir_patch_target(program, predicate_jumps[i],
                                         cxpr_ir_next_index(program));
                }
                free(predicate_jumps);
                if (!cxpr_ir_emit(program,
                                  (cxpr_ir_instr){
                                      .op = CXPR_OP_PUSH_BOOL,
                                      .value = is_all ? 0.0 : 1.0,
                                  }, err)) return false;
                cxpr_ir_patch_target(program, end_jump, cxpr_ir_next_index(program));
                return true;
            }
            if (count == 0u) {
                if (strcmp(fname, "sum")) {
                    if (err) {
                        err->code = CXPR_ERR_WRONG_ARITY;
                        err->message = "Static reduction requires a non-empty array";
                    }
                    return false;
                }
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = CXPR_OP_PUSH_CONST,
                                        .value = 0.0,
                                    }, err);
            }
            for (size_t i = 0u; i < count; ++i) {
                const cxpr_expr_ast* element = array->data.array.elements[i];
                if (selector) {
                    const char* names[] = {parameter};
                    const cxpr_expr_ast* args[] = {element};
                    cxpr_ir_subst_frame frame = {
                        .names = names,
                        .args = args,
                        .count = 1u,
                        .parent = subst,
                    };
                    if (!cxpr_ir_compile_node(selector, program, reg,
                                              local_names, local_count, &frame,
                                              inline_depth, err)) return false;
                } else if (mapped_selector) {
                    const char* names[] = {mapped_parameter};
                    const cxpr_expr_ast* args[] = {element};
                    cxpr_ir_subst_frame frame = {.names=names,.args=args,.count=1u,.parent=subst};
                    if (!cxpr_ir_compile_node(mapped_selector, program, reg,
                                              local_names, local_count, &frame,
                                              inline_depth, err)) return false;
                } else if (!cxpr_ir_compile_node(
                               element, program, reg, local_names, local_count,
                               subst, inline_depth, err)) return false;
                if (i > 0u) {
                    if (!strcmp(fname, "sum") || !strcmp(fname, "mean")) {
                        if (!cxpr_ir_emit(program,
                                          (cxpr_ir_instr){.op = CXPR_OP_ADD},
                                          err)) return false;
                    } else if (!cxpr_ir_emit(
                                   program,
                                   (cxpr_ir_instr){
                                       .op = CXPR_OP_CALL_FUNC,
                                       .func = entry,
                                       .payload = ast,
                                       .index = 2u,
                                   }, err)) return false;
                }
            }
            if (!strcmp(fname, "mean")) {
                if (!cxpr_ir_emit(program,
                                  (cxpr_ir_instr){
                                      .op = CXPR_OP_PUSH_CONST,
                                      .value = (double)count,
                                  }, err) ||
                    !cxpr_ir_emit(program,
                                  (cxpr_ir_instr){.op = CXPR_OP_DIV}, err)) {
                    return false;
                }
            }
            return true;
        }
static_reduction_not_handled:
        if (!strcmp(fname, "map")) {
            if (err) {
                err->code = CXPR_ERR_TYPE_MISMATCH;
                err->message = "map result must be consumed by a static reduction";
            }
            return false;
        }
        if (cxpr_expr_ast_call_uses_named_args(ast)) {
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
        if (ast->data.function_call.argc < entry->min_args ||
            ast->data.function_call.argc > entry->max_args) {
            if (err) {
                err->code = CXPR_ERR_WRONG_ARITY;
                err->message = "Wrong number of arguments";
            }
            return false;
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

        if (entry->ast_func) {
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = CXPR_OP_CALL_AST,
                                    .ast = ast,
                                },
                                err);
        }

        if ((strcmp(fname, "min") == 0 || strcmp(fname, "max") == 0 ||
             strcmp(fname, "argmin") == 0 || strcmp(fname, "argmax") == 0 ||
             strcmp(fname, "sum") == 0) &&
            !entry->defined_body &&
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

        if ((entry->struct_producer || entry->typed_struct_producer) && !entry->sync_func) {
            char* const_key = NULL;
            for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
                if (!cxpr_ir_compile_node(ast->data.function_call.args[i], program, reg,
                                          local_names, local_count, subst, inline_depth, err)) {
                    return false;
                }
            }
            if (!entry->typed_struct_producer)
                const_key = cxpr_ir_build_constant_producer_key(ast->data.function_call.name,
                                                                (const cxpr_expr_ast* const*)ast->data.function_call.args,
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
                    .args = (const cxpr_expr_ast* const*)ast->data.function_call.args,
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
        size_t right_start = program->count;
        if (!cxpr_ir_compile_node(ast->data.binary_op.right, program, reg,
                                  local_names, local_count, subst, inline_depth,
                                  err)) {
            return false;
        }
        if ((ast->data.binary_op.op == CXPR_TOK_STAR ||
             ast->data.binary_op.op == CXPR_TOK_SLASH) &&
            program->count == right_start + 1u &&
            program->code[right_start].op == CXPR_OP_PUSH_CONST &&
            program->code[right_start].value == 1.0) {
            program->count = right_start;
            return true;
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
