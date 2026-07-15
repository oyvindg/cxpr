/**
 * @file tree.c
 * @brief Core tree-walk evaluator implementation.
 */

#include "internal.h"

#include "context/internal.h" // IWYU pragma: keep
#include "core.h"
#include "expression/internal.h"
#include "limits.h"
#include "lookback.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* cxpr_eval_unknown_function_message(const char* name) {
    static CXPR_THREAD_LOCAL char message[256];
    if (!name || name[0] == '\0') return "Unknown function";
    snprintf(message, sizeof(message), "Unknown function '%s'", name);
    return message;
}

cxpr_value cxpr_eval_field_access(const cxpr_ast* ast, const cxpr_context* ctx,
                                  const cxpr_registry* reg, cxpr_error* err) {
    bool found = false;
    if (ctx && ctx->expression_scope) {
        cxpr_value scoped = cxpr_expression_lookup_typed_result(
            ctx->expression_scope,
            ast->data.field_access.full_key,
            &found);
        if (found) return scoped;

        scoped = cxpr_expression_lookup_typed_result(
            ctx->expression_scope,
            ast->data.field_access.object,
            &found);
        if (found && scoped.type != CXPR_VALUE_STRUCT) {
            return cxpr_eval_error(
                err,
                CXPR_ERR_UNKNOWN_IDENTIFIER,
                "Expression-scope chain has no struct prefix");
        }
    }
    cxpr_value value =
        cxpr_context_get_field(ctx, ast->data.field_access.object, ast->data.field_access.field,
                               &found);

    if (!found) {
        double fallback =
            cxpr_context_get(ctx, ast->data.field_access.full_key, &found);
        if (found) {
            /* Deprecated flat-key fallback kept for backward compatibility. */
            return cxpr_num(fallback);
        }
        {
            cxpr_func_entry* producer = cxpr_registry_find(reg, ast->data.field_access.object);
            if (producer && producer->struct_producer) {
                value = cxpr_eval_struct_producer(producer, ast->data.field_access.object,
                                                  ast->data.field_access.field, NULL, 0,
                                                  ctx, reg, err);
                if (err && err->code != CXPR_OK) return cxpr_num(NAN);
                found = true;
            } else {
                return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown field access");
            }
        }
    }

    return value;
}

cxpr_value cxpr_eval_chain_access(const cxpr_ast* ast, const cxpr_context* ctx,
                                  cxpr_error* err) {
    const cxpr_struct_value* current;
    size_t start_index = 1u;

    if (ctx && ctx->expression_scope) {
        bool found = false;
        bool scoped_prefix_found = false;
        cxpr_value scoped = cxpr_expression_lookup_typed_result(
            ctx->expression_scope,
            ast->data.chain_access.full_key,
            &found);
        if (found) return scoped;

        char prefix[512];
        size_t offset = 0u;
        for (size_t i = 0u; i + 1u < ast->data.chain_access.depth; ++i) {
            const char* segment = ast->data.chain_access.path[i];
            int written = snprintf(
                prefix + offset,
                sizeof(prefix) - offset,
                i == 0u ? "%s" : ".%s",
                segment ? segment : "");
            if (written <= 0 || (size_t)written >= sizeof(prefix) - offset) break;
            offset += (size_t)written;
            scoped = cxpr_expression_lookup_typed_result(
                ctx->expression_scope,
                prefix,
                &found);
            if (found) scoped_prefix_found = true;
            if (found && scoped.type == CXPR_VALUE_STRUCT) {
                current = scoped.s;
                start_index = i + 1u;
                goto walk_fields;
            }
        }
        if (scoped_prefix_found) {
            return cxpr_eval_error(
                err,
                CXPR_ERR_UNKNOWN_IDENTIFIER,
                "Expression-scope chain has no struct prefix");
        }
    }

    current = cxpr_context_get_struct(ctx, ast->data.chain_access.path[0]);
    if (!current) {
        bool found = false;
        cxpr_value root = cxpr_context_get_typed(ctx, ast->data.chain_access.path[0], &found);
        if (found && root.type == CXPR_VALUE_STRUCT) {
            current = root.s;
        }
    }
    if (!current) {
        return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown identifier");
    }

walk_fields:
    for (size_t i = start_index; i < ast->data.chain_access.depth; i++) {
        bool found = false;
        cxpr_value value = cxpr_num(0.0);

        for (size_t j = 0; j < current->field_count; j++) {
            if (strcmp(current->field_names[j], ast->data.chain_access.path[i]) == 0) {
                found = true;
                value = current->field_values[j];
                break;
            }
        }

        if (!found) {
            return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown field access");
        }

        if (i + 1 == ast->data.chain_access.depth) return cxpr_value_clone(&value);

        if (value.type != CXPR_VALUE_STRUCT) {
            return cxpr_eval_error(err, CXPR_ERR_TYPE_MISMATCH,
                                   "Chain access requires struct intermediates");
        }
        current = value.s;
    }

    return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown field access");
}

static const char* cxpr_eval_prepare_const_key_for_call(const cxpr_ast* ast,
                                                        char* local_buf,
                                                        size_t local_cap,
                                                        char** heap_buf) {
    cxpr_ast* mutable_ast = (cxpr_ast*)ast;
    double values[CXPR_MAX_CALL_ARGS];
    const char* key;

    if (heap_buf) *heap_buf = NULL;
    if (!ast || ast->type != CXPR_NODE_FUNCTION_CALL ||
        cxpr_ast_call_uses_named_args(ast) ||
        ast->data.function_call.argc > CXPR_MAX_CALL_ARGS) {
        return NULL;
    }

    if (ast->data.function_call.cached_const_key_ready) {
        return ast->data.function_call.cached_const_key;
    }

    mutable_ast->data.function_call.cached_const_key_ready = true;
    for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
        if (!cxpr_eval_constant_double(ast->data.function_call.args[i], &values[i])) {
            return NULL;
        }
    }

    key = cxpr_build_struct_cache_key(ast->data.function_call.name, values,
                                      ast->data.function_call.argc,
                                      local_buf, local_cap, heap_buf);
    if (!key) return NULL;

    mutable_ast->data.function_call.cached_const_key =
        heap_buf && *heap_buf ? *heap_buf : cxpr_strdup(key);
    if (heap_buf) *heap_buf = NULL;
    return mutable_ast->data.function_call.cached_const_key;
}

static bool cxpr_eval_values_equal(cxpr_value left, cxpr_value right, bool* out) {
    if (!out || left.type != right.type) return false;

    switch (left.type) {
    case CXPR_VALUE_NUMBER:
        *out = left.d == right.d;
        return true;
    case CXPR_VALUE_BOOL:
        *out = left.b == right.b;
        return true;
    case CXPR_VALUE_STRING:
        *out = strcmp(left.str, right.str) == 0;
        return true;
    case CXPR_VALUE_NULL:
        *out = true;
        return true;
    case CXPR_VALUE_TIMESTAMP:
    case CXPR_VALUE_DURATION:
        *out = left.i64 == right.i64;
        return true;
    default:
        return false;
    }
}

static bool cxpr_tok_to_arith_valop(int op, cxpr_valop* out) {
    switch (op) {
    case CXPR_TOK_PLUS: *out = CXPR_VALOP_ADD; return true;
    case CXPR_TOK_MINUS: *out = CXPR_VALOP_SUB; return true;
    case CXPR_TOK_STAR: *out = CXPR_VALOP_MUL; return true;
    case CXPR_TOK_SLASH: *out = CXPR_VALOP_DIV; return true;
    default: return false; /* %, ^ have no temporal meaning */
    }
}

static cxpr_value cxpr_eval_arithmetic_op(int op, cxpr_value left, cxpr_value right,
                                          cxpr_error* err) {
    if (left.type != CXPR_VALUE_NUMBER || right.type != CXPR_VALUE_NUMBER) {
        cxpr_valop vop;
        cxpr_value out;
        if (cxpr_tok_to_arith_valop(op, &vop)) {
            int handled = cxpr_value_binary_op(vop, left, right, &out, err);
            if (handled == 1) return out;
            if (handled == -1) return cxpr_num(NAN);
        }
        return cxpr_eval_error(err, CXPR_ERR_TYPE_MISMATCH,
                               "Arithmetic requires double operands");
    }
    if ((op == CXPR_TOK_SLASH || op == CXPR_TOK_PERCENT) && right.d == 0.0) {
        return cxpr_eval_error(err, CXPR_ERR_DIVISION_BY_ZERO,
                               op == CXPR_TOK_SLASH ? "Division by zero"
                                                    : "Modulo by zero");
    }

    switch (op) {
    case CXPR_TOK_PLUS: return cxpr_num(left.d + right.d);
    case CXPR_TOK_MINUS: return cxpr_num(left.d - right.d);
    case CXPR_TOK_STAR: return cxpr_num(left.d * right.d);
    case CXPR_TOK_SLASH: return cxpr_num(left.d / right.d);
    case CXPR_TOK_PERCENT: return cxpr_num(fmod(left.d, right.d));
    default: return cxpr_num(pow(left.d, right.d));
    }
}

static cxpr_value cxpr_eval_comparison_op(int op, cxpr_value left, cxpr_value right,
                                          cxpr_error* err) {
    if (left.type != CXPR_VALUE_NUMBER || right.type != CXPR_VALUE_NUMBER) {
        cxpr_valop vop = CXPR_VALOP_LT;
        cxpr_value out;
        int handled;
        switch (op) {
        case CXPR_TOK_LT: vop = CXPR_VALOP_LT; break;
        case CXPR_TOK_LTE: vop = CXPR_VALOP_LTE; break;
        case CXPR_TOK_GT: vop = CXPR_VALOP_GT; break;
        default: vop = CXPR_VALOP_GTE; break;
        }
        handled = cxpr_value_binary_op(vop, left, right, &out, err);
        if (handled == 1) return out;
        if (handled == -1) return cxpr_num(NAN);
        return cxpr_eval_error(err, CXPR_ERR_TYPE_MISMATCH,
                               "Comparison requires double operands");
    }

    switch (op) {
    case CXPR_TOK_LT: return cxpr_bool(left.d < right.d);
    case CXPR_TOK_LTE: return cxpr_bool(left.d <= right.d);
    case CXPR_TOK_GT: return cxpr_bool(left.d > right.d);
    default: return cxpr_bool(left.d >= right.d);
    }
}

static cxpr_value cxpr_eval_equality_op(int op, cxpr_value left, cxpr_value right,
                                        cxpr_error* err) {
    bool equal = false;

    if (!cxpr_eval_values_equal(left, right, &equal)) {
        return cxpr_eval_error(err, CXPR_ERR_TYPE_MISMATCH,
                               "Equality requires matching scalar types");
    }
    return cxpr_bool(op == CXPR_TOK_EQ ? equal : !equal);
}

static cxpr_value cxpr_eval_binary_values(int op, cxpr_value left, cxpr_value right,
                                          cxpr_error* err) {
    switch (op) {
    case CXPR_TOK_PLUS:
    case CXPR_TOK_MINUS:
    case CXPR_TOK_STAR:
    case CXPR_TOK_SLASH:
    case CXPR_TOK_PERCENT:
    case CXPR_TOK_POWER:
        return cxpr_eval_arithmetic_op(op, left, right, err);

    case CXPR_TOK_LT:
    case CXPR_TOK_LTE:
    case CXPR_TOK_GT:
    case CXPR_TOK_GTE:
        return cxpr_eval_comparison_op(op, left, right, err);

    case CXPR_TOK_EQ:
    case CXPR_TOK_NEQ:
        return cxpr_eval_equality_op(op, left, right, err);

    default:
        return cxpr_eval_error(err, CXPR_ERR_SYNTAX, "Unknown binary operator");
    }
}

static cxpr_value cxpr_eval_binary_op(const cxpr_ast* ast, const cxpr_context* ctx,
                                      const cxpr_registry* reg, cxpr_error* err) {
    int op = ast->data.binary_op.op;
    cxpr_value left = cxpr_eval_node(ast->data.binary_op.left, ctx, reg, err);
    cxpr_value right;

    if (err && err->code != CXPR_OK) return cxpr_num(NAN);

    if (op == CXPR_TOK_AND || op == CXPR_TOK_OR) {
        if (!cxpr_require_type(left, CXPR_VALUE_BOOL, err, "Logical operators require bool")) {
            return cxpr_num(NAN);
        }
        if (op == CXPR_TOK_AND && !left.b) return cxpr_bool(false);
        if (op == CXPR_TOK_OR && left.b) return cxpr_bool(true);
    }

    right = cxpr_eval_node(ast->data.binary_op.right, ctx, reg, err);
    if (err && err->code != CXPR_OK) return cxpr_num(NAN);

    if (op == CXPR_TOK_AND || op == CXPR_TOK_OR) {
        if (!cxpr_require_type(right, CXPR_VALUE_BOOL, err, "Logical operators require bool")) {
            return cxpr_num(NAN);
        }
        return cxpr_bool(right.b);
    }

    return cxpr_eval_binary_values(op, left, right, err);
}

static cxpr_value cxpr_eval_unary_op(const cxpr_ast* ast, const cxpr_context* ctx,
                                     const cxpr_registry* reg, cxpr_error* err) {
    cxpr_value operand = cxpr_eval_node(ast->data.unary_op.operand, ctx, reg, err);
    if (err && err->code != CXPR_OK) return cxpr_num(NAN);

    switch (ast->data.unary_op.op) {
    case CXPR_TOK_MINUS:
        if (!cxpr_require_type(operand, CXPR_VALUE_NUMBER, err,
                               "Unary minus requires double")) {
            return cxpr_num(NAN);
        }
        return cxpr_num(-operand.d);
    case CXPR_TOK_NOT:
        if (!cxpr_require_type(operand, CXPR_VALUE_BOOL, err,
                               "Logical not requires bool")) {
            return cxpr_num(NAN);
        }
        return cxpr_bool(!operand.b);
    default:
        return cxpr_eval_error(err, CXPR_ERR_SYNTAX, "Unknown unary operator");
    }
}

static cxpr_value cxpr_eval_ternary_op(const cxpr_ast* ast, const cxpr_context* ctx,
                                       const cxpr_registry* reg, cxpr_error* err) {
    cxpr_value condition = cxpr_eval_node(ast->data.ternary.condition, ctx, reg, err);
    if (err && err->code != CXPR_OK) return cxpr_num(NAN);
    if (!cxpr_require_type(condition, CXPR_VALUE_BOOL, err,
                           "Ternary condition must be bool")) {
        return cxpr_num(NAN);
    }

    return cxpr_eval_node(condition.b ? ast->data.ternary.true_branch
                                      : ast->data.ternary.false_branch,
                          ctx, reg, err);
}

static cxpr_value cxpr_eval_node_uncached(const cxpr_ast* ast, const cxpr_context* ctx,
                                          const cxpr_registry* reg, cxpr_error* err) {
    if (!ast) return cxpr_eval_error(err, CXPR_ERR_SYNTAX, "NULL AST node");

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        return cxpr_num(ast->data.number.value);

    case CXPR_NODE_BOOL:
        return cxpr_bool(ast->data.boolean.value);

    case CXPR_NODE_ARRAY: {
        cxpr_value* values = NULL;
        cxpr_array_value* array;
        if (ast->data.array.count > 0u) {
            values = (cxpr_value*)calloc(ast->data.array.count, sizeof(cxpr_value));
            if (!values) {
                return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
            }
            for (size_t i = 0u; i < ast->data.array.count; ++i) {
                values[i] = cxpr_eval_node(ast->data.array.elements[i], ctx, reg, err);
                if (err && err->code != CXPR_OK) {
                    free(values);
                    return cxpr_num(NAN);
                }
            }
        }
        array = cxpr_array_value_new(values, ast->data.array.count);
        free(values);
        if (!array) {
            return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        }
        return cxpr_array(array);
    }

    case CXPR_NODE_STRING:
        return cxpr_string(ast->data.string.value);

    case CXPR_NODE_IDENTIFIER: {
        bool found = false;
        cxpr_value value = cxpr_context_get_typed(ctx, ast->data.identifier.name, &found);
        if (!found) {
            return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown identifier");
        }
        return value;
    }

    case CXPR_NODE_VARIABLE: {
        bool found = false;
        cxpr_value value = cxpr_context_get_param_typed(ctx, ast->data.variable.name, &found);
        if (!found) {
            return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                   "Unknown parameter variable");
        }
        return value;
    }

    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_eval_field_access(ast, ctx, reg, err);

    case CXPR_NODE_CHAIN_ACCESS:
        return cxpr_eval_chain_access(ast, ctx, err);

    case CXPR_NODE_LOOKBACK: {
        cxpr_value value;
        if (reg && reg->lookback_resolver) {
            const cxpr_ast* target = ast->data.lookback.target;
            cxpr_ast index_ast = {0};
            unsigned offset;
            bool flattened = false;
            value = cxpr_num(NAN);
            if (cxpr_lookback_literal_offset(ast->data.lookback.index,
                                             &offset,
                                             NULL,
                                             NULL)) {
                while (target && target->type == CXPR_NODE_LOOKBACK) {
                    unsigned inner_offset;
                    unsigned summed;
                    if (!cxpr_lookback_literal_offset(target->data.lookback.index,
                                                      &inner_offset,
                                                      NULL,
                                                      NULL) ||
                        !cxpr_lookback_add_unsigned(offset,
                                                    inner_offset,
                                                    &summed,
                                                    NULL,
                                                    NULL)) {
                        target = ast->data.lookback.target;
                        break;
                    }
                    offset = summed;
                    target = target->data.lookback.target;
                }
                if (target != ast->data.lookback.target) {
                    index_ast.type = CXPR_NODE_NUMBER;
                    index_ast.data.number.value = (double)offset;
                    flattened = true;
                }
            }
            if (reg->lookback_resolver(target,
                                       flattened ? &index_ast : ast->data.lookback.index,
                                       ctx,
                                       reg,
                                       reg->lookback_userdata,
                                       &value,
                                       err)) {
                return value;
            }
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
        }
        return cxpr_eval_error(err, CXPR_ERR_SYNTAX,
                               "Native lookback requires a registry lookback resolver");
    }

    case CXPR_NODE_BINARY_OP:
        return cxpr_eval_binary_op(ast, ctx, reg, err);

    case CXPR_NODE_UNARY_OP:
        return cxpr_eval_unary_op(ast, ctx, reg, err);

    case CXPR_NODE_FUNCTION_CALL: {
        const char* name = ast->data.function_call.name;
        size_t argc = ast->data.function_call.argc;
        const cxpr_ast* ordered_args[CXPR_MAX_CALL_ARGS] = {0};
        cxpr_func_entry* entry = cxpr_eval_cached_function_entry(ast, reg);

        if (!entry) {
            return cxpr_eval_error(
                err,
                CXPR_ERR_UNKNOWN_FUNCTION,
                cxpr_eval_unknown_function_message(ast->data.producer_access.name));
        }
        if (entry->ast_func_handler) {
            return entry->ast_func_handler(ast, ctx, reg, entry->ast_func_handler_userdata, err);
        }
        if (entry->ast_func) {
            return entry->ast_func(ast, ctx, reg, entry->userdata, err);
        }
        if (entry->struct_producer && !entry->sync_func && !entry->value_func &&
            !cxpr_ast_call_uses_named_args(ast)) {
            char const_key_local[256];
            char* const_key_heap = NULL;
            const char* const_key = cxpr_eval_prepare_const_key_for_call(
                ast, const_key_local, sizeof(const_key_local), &const_key_heap);
            if (const_key) {
                const cxpr_struct_value* cached = cxpr_context_get_cached_struct(ctx, const_key);
                if (cached) {
                    free(const_key_heap);
                    return cxpr_struct((cxpr_struct_value*)cached);
                }
            }
            {
                const cxpr_ast* const* direct_args =
                    (const cxpr_ast* const*)ast->data.function_call.args;
                const cxpr_struct_value* produced =
                    cxpr_eval_struct_result(entry, name, direct_args, argc,
                                            const_key, ctx, reg, err);
                free(const_key_heap);
                if (err && err->code != CXPR_OK) return cxpr_num(NAN);
                return cxpr_struct((cxpr_struct_value*)produced);
            }
        }
        if (!cxpr_eval_bind_call_args(ast, entry, ordered_args, err)) {
            return cxpr_num(NAN);
        }
        if (entry->model_producer) {
            return entry->model_producer(ast, ctx, reg, entry->model_producer_userdata, err);
        }
        if (entry->defined_body || entry->defined_return_field_count > 0u) {
            return cxpr_eval_defined_function(entry, ast, ctx, reg, err);
        }
        if (entry->struct_producer && !entry->sync_func && !entry->value_func) {
            const cxpr_struct_value* produced =
                cxpr_eval_struct_result(entry, name, ordered_args, argc, NULL, ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            return cxpr_struct((cxpr_struct_value*)produced);
        }

        if (entry->struct_fields && !entry->struct_producer && entry->sync_func) {
            double args[CXPR_MAX_CALL_ARGS];
            size_t out = 0;

            if (argc != entry->struct_argc) {
                return cxpr_eval_error(err, CXPR_ERR_WRONG_ARITY,
                                       "Wrong number of struct arguments");
            }

            for (size_t i = 0; i < entry->struct_argc && out < 32; i++) {
                const cxpr_ast* arg = ordered_args[i];
                if (arg->type != CXPR_NODE_IDENTIFIER) {
                    return cxpr_eval_error(err, CXPR_ERR_SYNTAX,
                                           "Struct argument must be an identifier");
                }
                for (size_t f = 0; f < entry->fields_per_arg && out < 32; f++) {
                    bool found = false;
                    cxpr_value value =
                        cxpr_context_get_field(ctx, arg->data.identifier.name,
                                               entry->struct_fields[f], &found);
                    if (!found) {
                        return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                               "Unknown struct field");
                    }
                    if (!cxpr_require_type(value, CXPR_VALUE_NUMBER, err,
                                           "Struct function arguments must be scalar doubles")) {
                        return cxpr_num(NAN);
                    }
                    args[out++] = value.d;
                }
            }

            return cxpr_num(entry->sync_func(args, out, entry->userdata));
        }

        if (strcmp(name, "if") == 0 && argc == 3) {
            cxpr_value cond = cxpr_eval_node(ordered_args[0], ctx, reg, err);
            bool take_true;
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);

            if (cond.type == CXPR_VALUE_BOOL) {
                take_true = cond.b;
            } else {
                return cxpr_eval_error(err, CXPR_ERR_TYPE_MISMATCH,
                                       "if() condition must be bool");
            }

            if (take_true) {
                return cxpr_eval_node(ordered_args[1], ctx, reg, err);
            }
            return cxpr_eval_node(ordered_args[2], ctx, reg, err);
        }

        if (argc < entry->min_args || argc > entry->max_args) {
            return cxpr_eval_error(err, CXPR_ERR_WRONG_ARITY, "Wrong number of arguments");
        }

        if (entry->native_kind == CXPR_NATIVE_KIND_NULLARY && argc == 0) {
            return cxpr_num(entry->native_scalar.nullary());
        }
        if (entry->native_kind == CXPR_NATIVE_KIND_UNARY && argc == 1) {
            double a = cxpr_eval_scalar_arg(ordered_args[0], ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            return cxpr_num(entry->native_scalar.unary(a));
        }
        if (entry->native_kind == CXPR_NATIVE_KIND_BINARY && argc == 2) {
            double a = cxpr_eval_scalar_arg(ordered_args[0], ctx, reg, err);
            double b = cxpr_eval_scalar_arg(ordered_args[1], ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            return cxpr_num(entry->native_scalar.binary(a, b));
        }
        if (entry->native_kind == CXPR_NATIVE_KIND_TERNARY && argc == 3) {
            double a = cxpr_eval_scalar_arg(ordered_args[0], ctx, reg, err);
            double b = cxpr_eval_scalar_arg(ordered_args[1], ctx, reg, err);
            double c = cxpr_eval_scalar_arg(ordered_args[2], ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            return cxpr_num(entry->native_scalar.ternary(a, b, c));
        }

        {
            cxpr_value args[CXPR_MAX_CALL_ARGS];
            if (argc > CXPR_MAX_CALL_ARGS) {
                return cxpr_eval_error(err, CXPR_ERR_WRONG_ARITY, "Too many function arguments");
            }
            for (size_t i = 0; i < argc; i++) {
                args[i] = cxpr_eval_node(ordered_args[i], ctx, reg, err);
                if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            }
            return cxpr_registry_call_typed(reg, name, args, argc, err);
        }
    }

    case CXPR_NODE_PRODUCER_ACCESS: {
        cxpr_func_entry* entry = cxpr_eval_cached_producer_entry(ast, reg);
        if (!entry) {
            return cxpr_eval_error(
                err,
                CXPR_ERR_UNKNOWN_FUNCTION,
                cxpr_eval_unknown_function_message(ast->data.producer_access.name));
        }
        if (entry->ast_func_handler) {
            return entry->ast_func_handler(ast, ctx, reg, entry->ast_func_handler_userdata, err);
        }
        {
            cxpr_value value = cxpr_eval_cached_producer_access(ast, ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            return value;
        }
    }

    case CXPR_NODE_TERNARY:
        return cxpr_eval_ternary_op(ast, ctx, reg, err);
    }

    return cxpr_eval_error(err, CXPR_ERR_SYNTAX, "Unknown AST node type");
}

cxpr_value cxpr_eval_node(const cxpr_ast* ast, const cxpr_context* ctx,
                          const cxpr_registry* reg, cxpr_error* err) {
    unsigned long hash;
    cxpr_value cached;
    cxpr_value value;

    if (!ast) return cxpr_eval_node_uncached(ast, ctx, reg, err);

    if (cxpr_eval_current_lookback_offset() != 0.0 ||
        ast->type != CXPR_NODE_FUNCTION_CALL ||
        !cxpr_eval_ast_memoable(ast, reg)) {
        return cxpr_eval_node_uncached(ast, ctx, reg, err);
    }

    hash = cxpr_eval_ast_hash(ast);
    if (cxpr_eval_memo_get(ctx, ast, hash, &cached)) return cached;

    value = cxpr_eval_node_uncached(ast, ctx, reg, err);
    if (err && err->code != CXPR_OK) return value;
    (void)cxpr_eval_memo_set(ctx, ast, hash, value);
    return value;
}
