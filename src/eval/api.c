/**
 * @file api.c
 * @brief Public evaluator API wrappers.
 */

#include "internal.h"
#include "context/state.h"
#include "ir/exec/internal.h"
#include "ir/internal.h"
#include <math.h>
#include <string.h>

static bool cxpr_eval_number_fast(const cxpr_ast* ast, const cxpr_context* ctx,
                                  const cxpr_registry* reg, double* out, cxpr_error* err);

static bool cxpr_eval_ast_auto_compile_safe(const cxpr_ast* ast, const cxpr_registry* reg) {
    cxpr_func_entry* entry;

    if (!ast) return false;

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
    case CXPR_NODE_BOOL:
    case CXPR_NODE_IDENTIFIER:
    case CXPR_NODE_VARIABLE:
        return true;
    case CXPR_NODE_UNARY_OP:
        return cxpr_eval_ast_auto_compile_safe(ast->data.unary_op.operand, reg);
    case CXPR_NODE_BINARY_OP:
        return cxpr_eval_ast_auto_compile_safe(ast->data.binary_op.left, reg) &&
               cxpr_eval_ast_auto_compile_safe(ast->data.binary_op.right, reg);
    case CXPR_NODE_TERNARY:
        return cxpr_eval_ast_auto_compile_safe(ast->data.ternary.condition, reg) &&
               cxpr_eval_ast_auto_compile_safe(ast->data.ternary.true_branch, reg) &&
               cxpr_eval_ast_auto_compile_safe(ast->data.ternary.false_branch, reg);
    case CXPR_NODE_FUNCTION_CALL:
        if (cxpr_ast_call_uses_named_args(ast)) return false;
        entry = cxpr_eval_cached_function_entry(ast, reg);
        if (!entry || ast->data.function_call.argc < entry->min_args ||
            ast->data.function_call.argc > entry->max_args) {
            return false;
        }
        if (entry->ast_func_overlay || entry->ast_func || entry->struct_producer ||
            entry->struct_fields || entry->value_func || entry->typed_func ||
            (entry->sync_func && entry->native_kind == CXPR_NATIVE_KIND_NONE &&
             !entry->defined_body)) {
            return false;
        }
        if (entry->defined_body) {
            if (!cxpr_ir_prepare_defined_program(entry, reg, NULL) ||
                !cxpr_eval_ast_auto_compile_safe(entry->defined_body, reg)) {
                return false;
            }
        }
        for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
            if (!cxpr_eval_ast_auto_compile_safe(ast->data.function_call.args[i], reg)) {
                return false;
            }
        }
        return true;
    default:
        return false;
    }
}

static cxpr_program* cxpr_eval_ast_cached_program(const cxpr_ast* ast,
                                                  const cxpr_registry* reg) {
    cxpr_ast* mutable_ast;
    const unsigned long version = reg ? reg->version : 0u;
    cxpr_error compile_err = {0};
    cxpr_program* compiled;

    if (!ast) return NULL;
    mutable_ast = (cxpr_ast*)ast;
    if (mutable_ast->compiled_registry != reg ||
        mutable_ast->compiled_registry_version != version) {
        cxpr_program_free(mutable_ast->compiled_cache);
        mutable_ast->compiled_cache = NULL;
        mutable_ast->compiled_registry = reg;
        mutable_ast->compiled_registry_version = version;
        mutable_ast->compiled_cache_failed = false;
    }

    if (mutable_ast->compiled_cache) return mutable_ast->compiled_cache;
    if (mutable_ast->compiled_cache_failed) return NULL;
    if (!cxpr_eval_ast_auto_compile_safe(ast, reg)) return NULL;

    compiled = cxpr_compile(ast, reg, &compile_err);
    if (!compiled) {
        mutable_ast->compiled_cache_failed = true;
        return NULL;
    }

    mutable_ast->compiled_cache = compiled;
    return compiled;
}

static bool cxpr_eval_function_call_memoable_cached(const cxpr_ast* ast,
                                                    const cxpr_registry* reg,
                                                    const cxpr_func_entry* entry) {
    cxpr_ast* mutable_ast;
    const unsigned long version = reg ? reg->version : 0u;

    if (!ast || ast->type != CXPR_NODE_FUNCTION_CALL) return false;
    if (ast->data.function_call.cached_memoable_valid &&
        ast->data.function_call.cached_memoable_registry == reg &&
        ast->data.function_call.cached_memoable_registry_version == version) {
        return ast->data.function_call.cached_memoable;
    }

    mutable_ast = (cxpr_ast*)ast;
    mutable_ast->data.function_call.cached_memoable =
        entry && !entry->ast_func_overlay && !entry->ast_func &&
        !(entry->struct_producer && !entry->sync_func && !entry->value_func);
    mutable_ast->data.function_call.cached_memoable_valid = true;
    mutable_ast->data.function_call.cached_memoable_registry = reg;
    mutable_ast->data.function_call.cached_memoable_registry_version = version;
    return mutable_ast->data.function_call.cached_memoable;
}

static unsigned long cxpr_eval_function_call_hash_cached(const cxpr_ast* ast) {
    cxpr_ast* mutable_ast;

    if (!ast || ast->type != CXPR_NODE_FUNCTION_CALL) return 0u;
    if (ast->data.function_call.cached_hash_valid) return ast->data.function_call.cached_hash;

    mutable_ast = (cxpr_ast*)ast;
    mutable_ast->data.function_call.cached_hash = cxpr_eval_ast_hash(ast);
    mutable_ast->data.function_call.cached_hash_valid = true;
    return mutable_ast->data.function_call.cached_hash;
}

static bool cxpr_eval_root_slot_cached_number(const cxpr_context* ctx,
                                              const cxpr_hashmap* map,
                                              const char* name,
                                              unsigned long hash,
                                              const cxpr_context** cached_ctx,
                                              void** cached_entries_base,
                                              size_t* cached_slot,
                                              unsigned long* cached_version,
                                              unsigned long version,
                                              double* out) {
    const cxpr_hashmap_entry* entry;

    if (!ctx || ctx->parent || !map || !name || !out) return false;

    if (*cached_ctx == ctx && *cached_entries_base == map->entries &&
        *cached_version == version) {
        *out = map->entries[*cached_slot].value;
        return true;
    }

    entry = cxpr_hashmap_find_prehashed_entry(map, name, hash);
    if (!entry) return false;

    *cached_ctx = ctx;
    *cached_entries_base = (void*)map->entries;
    *cached_slot = (size_t)(entry - map->entries);
    *cached_version = version;
    *out = entry->value;
    return true;
}

static bool cxpr_eval_bool_fast(const cxpr_ast* ast, const cxpr_context* ctx,
                                const cxpr_registry* reg, bool* out, cxpr_error* err) {
    bool left_bool;
    bool right_bool;
    double left;
    double right;

    if (!ast || !out) return false;

    switch (ast->type) {
    case CXPR_NODE_BOOL:
        *out = ast->data.boolean.value;
        return true;

    case CXPR_NODE_UNARY_OP:
        if (ast->data.unary_op.op != CXPR_TOK_NOT) return false;
        if (!cxpr_eval_bool_fast(ast->data.unary_op.operand, ctx, reg, &left_bool, err)) {
            return false;
        }
        *out = !left_bool;
        return true;

    case CXPR_NODE_BINARY_OP:
        switch (ast->data.binary_op.op) {
        case CXPR_TOK_AND:
            if (!cxpr_eval_bool_fast(ast->data.binary_op.left, ctx, reg, &left_bool, err)) {
                return false;
            }
            if (!left_bool) {
                *out = false;
                return true;
            }
            if (!cxpr_eval_bool_fast(ast->data.binary_op.right, ctx, reg, &right_bool, err)) {
                return false;
            }
            *out = right_bool;
            return true;
        case CXPR_TOK_OR:
            if (!cxpr_eval_bool_fast(ast->data.binary_op.left, ctx, reg, &left_bool, err)) {
                return false;
            }
            if (left_bool) {
                *out = true;
                return true;
            }
            if (!cxpr_eval_bool_fast(ast->data.binary_op.right, ctx, reg, &right_bool, err)) {
                return false;
            }
            *out = right_bool;
            return true;
        case CXPR_TOK_EQ:
        case CXPR_TOK_NEQ:
        case CXPR_TOK_LT:
        case CXPR_TOK_LTE:
        case CXPR_TOK_GT:
        case CXPR_TOK_GTE:
            if (!cxpr_eval_number_fast(ast->data.binary_op.left, ctx, reg, &left, err)) {
                return false;
            }
            if (!cxpr_eval_number_fast(ast->data.binary_op.right, ctx, reg, &right, err)) {
                return false;
            }
            if (ast->data.binary_op.op == CXPR_TOK_EQ) *out = (left == right);
            else if (ast->data.binary_op.op == CXPR_TOK_NEQ) *out = (left != right);
            else if (ast->data.binary_op.op == CXPR_TOK_LT) *out = (left < right);
            else if (ast->data.binary_op.op == CXPR_TOK_LTE) *out = (left <= right);
            else if (ast->data.binary_op.op == CXPR_TOK_GT) *out = (left > right);
            else *out = (left >= right);
            return true;
        default:
            return false;
        }

    default:
        return false;
    }
}

static bool cxpr_eval_number_fast(const cxpr_ast* ast, const cxpr_context* ctx,
                                  const cxpr_registry* reg, double* out, cxpr_error* err) {
    double left;
    double right;
    bool found;
    bool cond;

    if (!ast || !out) return false;

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        *out = ast->data.number.value;
        return true;

    case CXPR_NODE_IDENTIFIER:
        found = false;
        if (ctx && !ctx->expression_scope && ctx->bools.count == 0u) {
            cxpr_ast* mutable_ast = (cxpr_ast*)ast;
            found = cxpr_eval_root_slot_cached_number(
                ctx,
                &ctx->variables,
                ast->data.identifier.name,
                ast->data.identifier.hash,
                &mutable_ast->data.identifier.cached_ctx,
                &mutable_ast->data.identifier.cached_entries_base,
                &mutable_ast->data.identifier.cached_slot,
                &mutable_ast->data.identifier.cached_version,
                ctx->variables_version,
                out);
            if (!found) {
                *out = cxpr_ir_context_get_prehashed(ctx,
                                                     ast->data.identifier.name,
                                                     ast->data.identifier.hash,
                                                     &found);
            }
        } else if (ctx && !ctx->expression_scope) {
            *out = cxpr_ir_context_get_prehashed(ctx,
                                                 ast->data.identifier.name,
                                                 ast->data.identifier.hash,
                                                 &found);
            if (!found) {
                *out = cxpr_context_get(ctx, ast->data.identifier.name, &found);
            }
        } else {
            *out = cxpr_context_get(ctx, ast->data.identifier.name, &found);
        }
        if (!found) {
            if (err) {
                err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                err->message = "Unknown identifier";
            }
            *out = NAN;
        }
        return true;

    case CXPR_NODE_VARIABLE:
        found = false;
        if (ctx) {
            cxpr_ast* mutable_ast = (cxpr_ast*)ast;
            found = cxpr_eval_root_slot_cached_number(
                ctx,
                &ctx->params,
                ast->data.variable.name,
                ast->data.variable.hash,
                &mutable_ast->data.variable.cached_ctx,
                &mutable_ast->data.variable.cached_entries_base,
                &mutable_ast->data.variable.cached_slot,
                &mutable_ast->data.variable.cached_version,
                ctx->params_version,
                out);
        }
        if (!found) {
            *out = cxpr_context_get_param(ctx, ast->data.variable.name, &found);
        }
        if (!found) {
            if (err) {
                err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                err->message = "Unknown parameter variable";
            }
            *out = NAN;
        }
        return true;

    case CXPR_NODE_UNARY_OP:
        if (ast->data.unary_op.op != CXPR_TOK_MINUS) return false;
        if (!cxpr_eval_number_fast(ast->data.unary_op.operand, ctx, reg, out, err)) return false;
        *out = -*out;
        return true;

    case CXPR_NODE_BINARY_OP:
        switch (ast->data.binary_op.op) {
        case CXPR_TOK_PLUS:
        case CXPR_TOK_MINUS:
        case CXPR_TOK_STAR:
        case CXPR_TOK_SLASH:
        case CXPR_TOK_PERCENT:
        case CXPR_TOK_POWER:
            if (!cxpr_eval_number_fast(ast->data.binary_op.left, ctx, reg, &left, err)) {
                return false;
            }
            if (!cxpr_eval_number_fast(ast->data.binary_op.right, ctx, reg, &right, err)) {
                return false;
            }
            if ((ast->data.binary_op.op == CXPR_TOK_SLASH ||
                 ast->data.binary_op.op == CXPR_TOK_PERCENT) &&
                right == 0.0) {
                if (err) {
                    err->code = CXPR_ERR_DIVISION_BY_ZERO;
                    err->message = ast->data.binary_op.op == CXPR_TOK_SLASH
                                       ? "Division by zero"
                                       : "Modulo by zero";
                }
                *out = NAN;
                return true;
            }
            if (ast->data.binary_op.op == CXPR_TOK_PLUS) *out = left + right;
            else if (ast->data.binary_op.op == CXPR_TOK_MINUS) *out = left - right;
            else if (ast->data.binary_op.op == CXPR_TOK_STAR) *out = left * right;
            else if (ast->data.binary_op.op == CXPR_TOK_SLASH) *out = left / right;
            else if (ast->data.binary_op.op == CXPR_TOK_PERCENT) *out = fmod(left, right);
            else *out = pow(left, right);
            return true;
        default:
            return false;
        }

    case CXPR_NODE_TERNARY:
        if (!cxpr_eval_bool_fast(ast->data.ternary.condition, ctx, reg, &cond, err)) return false;
        return cxpr_eval_number_fast(cond ? ast->data.ternary.true_branch
                                          : ast->data.ternary.false_branch,
                                     ctx, reg, out, err);

    case CXPR_NODE_FUNCTION_CALL: {
        const char* name = ast->data.function_call.name;
        const size_t argc = ast->data.function_call.argc;
        const cxpr_ast* const* ordered_args =
            (const cxpr_ast* const*)ast->data.function_call.args;
        double args[CXPR_MAX_CALL_ARGS];
        cxpr_func_entry* entry;
        bool should_memo;
        unsigned long memo_hash = 0u;
        cxpr_value memo_value;

        if (argc > CXPR_MAX_CALL_ARGS || cxpr_ast_call_uses_named_args(ast)) return false;
        entry = cxpr_eval_cached_function_entry(ast, reg);
        if (!entry || entry->ast_func_overlay || entry->ast_func || entry->struct_producer ||
            entry->struct_fields || entry->value_func || entry->typed_func) {
            return false;
        }
        if (argc < entry->min_args || argc > entry->max_args) {
            if (err) {
                err->code = CXPR_ERR_WRONG_ARITY;
                err->message = "Wrong number of arguments";
            }
            *out = NAN;
            return true;
        }

        should_memo = cxpr_eval_function_call_memoable_cached(ast, reg, entry);
        if (should_memo) {
            memo_hash = cxpr_eval_function_call_hash_cached(ast);
            if (cxpr_eval_memo_get(ctx, ast, memo_hash, &memo_value)) {
                if (memo_value.type != CXPR_VALUE_NUMBER) return false;
                *out = memo_value.d;
                return true;
            }
        }

        if (entry->defined_body) {
            if (argc != entry->defined_param_count || argc > CXPR_MAX_CALL_ARGS) return false;
            for (size_t i = 0; i < entry->defined_param_count; ++i) {
                if (entry->defined_param_fields[i] &&
                    entry->defined_param_field_counts[i] > 0) {
                    return false;
                }
            }
            for (size_t i = 0; i < argc; ++i) {
                if (!cxpr_eval_number_fast(ordered_args[i], ctx, reg, &args[i], err)) {
                    return false;
                }
                if (err && err->code != CXPR_OK) {
                    *err = (cxpr_error){0};
                    return false;
                }
            }
            if (!cxpr_ir_prepare_defined_program(entry, reg, err) || !entry->defined_program) {
                if (err) *err = (cxpr_error){0};
                return false;
            }
            *out = cxpr_ir_exec_with_locals(&entry->defined_program->ir, ctx, reg, args, argc, err);
            if (!(err && err->code != CXPR_OK) && should_memo) {
                (void)cxpr_eval_memo_set(ctx, ast, memo_hash, cxpr_fv_double(*out));
            }
            return true;
        }

        for (size_t i = 0; i < argc; ++i) {
            if (!cxpr_eval_number_fast(ordered_args[i], ctx, reg, &args[i], err)) return false;
            if (err && err->code != CXPR_OK) {
                *out = NAN;
                return true;
            }
        }

        if (entry->native_kind == CXPR_NATIVE_KIND_NONE && entry->sync_func) {
            *out = entry->sync_func(args, argc, entry->userdata);
            if (should_memo) (void)cxpr_eval_memo_set(ctx, ast, memo_hash, cxpr_fv_double(*out));
            return true;
        }
        if (entry->native_kind == CXPR_NATIVE_KIND_NULLARY && argc == 0) {
            *out = entry->native_scalar.nullary();
            if (should_memo) (void)cxpr_eval_memo_set(ctx, ast, memo_hash, cxpr_fv_double(*out));
            return true;
        }
        if (entry->native_kind == CXPR_NATIVE_KIND_UNARY && argc == 1) {
            *out = entry->native_scalar.unary(args[0]);
            if (should_memo) (void)cxpr_eval_memo_set(ctx, ast, memo_hash, cxpr_fv_double(*out));
            return true;
        }
        if (entry->native_kind == CXPR_NATIVE_KIND_BINARY && argc == 2) {
            *out = entry->native_scalar.binary(args[0], args[1]);
            if (should_memo) (void)cxpr_eval_memo_set(ctx, ast, memo_hash, cxpr_fv_double(*out));
            return true;
        }
        if (entry->native_kind == CXPR_NATIVE_KIND_TERNARY && argc == 3) {
            *out = entry->native_scalar.ternary(args[0], args[1], args[2]);
            if (should_memo) (void)cxpr_eval_memo_set(ctx, ast, memo_hash, cxpr_fv_double(*out));
            return true;
        }
        if (strcmp(name, "if") == 0 && argc == 3) return false;
        if (!entry->sync_func) return false;
        *out = entry->sync_func(args, argc, entry->userdata);
        if (should_memo) (void)cxpr_eval_memo_set(ctx, ast, memo_hash, cxpr_fv_double(*out));
        return true;
    }

    default:
        return false;
    }
}

cxpr_value cxpr_eval_ast_value(const cxpr_ast* ast, const cxpr_context* ctx,
                               const cxpr_registry* reg, cxpr_error* err) {
    cxpr_value value;

    if (err) *err = (cxpr_error){0};

    cxpr_eval_memo_enter((cxpr_context*)ctx);
    value = cxpr_eval_node(ast, ctx, reg, err);
    cxpr_eval_memo_leave((cxpr_context*)ctx);
    return value;
}

bool cxpr_eval_ast(const cxpr_ast* ast, const cxpr_context* ctx,
                   const cxpr_registry* reg, cxpr_value* out_value, cxpr_error* err) {
    cxpr_value value;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }

    value = cxpr_eval_ast_value(ast, ctx, reg, err);
    if (err && err->code != CXPR_OK) return false;
    *out_value = value;
    return true;
}

bool cxpr_eval_ast_number(const cxpr_ast* ast, const cxpr_context* ctx,
                          const cxpr_registry* reg, double* out_value, cxpr_error* err) {
    cxpr_value value;
    cxpr_program* cached;
    double fast_value;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }

    if (err) *err = (cxpr_error){0};
    cached = cxpr_eval_ast_cached_program(ast, reg);
    if (cached && cxpr_eval_program_number(cached, ctx, reg, out_value, err)) return true;
    if (err && err->code != CXPR_OK) *err = (cxpr_error){0};

    cxpr_eval_memo_enter((cxpr_context*)ctx);
    if (cxpr_eval_number_fast(ast, ctx, reg, &fast_value, err)) {
        cxpr_eval_memo_leave((cxpr_context*)ctx);
        if (err && err->code != CXPR_OK) return false;
        *out_value = fast_value;
        return true;
    }
    cxpr_eval_memo_leave((cxpr_context*)ctx);

    value = cxpr_eval_ast_value(ast, ctx, reg, err);
    if (err && err->code != CXPR_OK) return false;
    if (value.type != CXPR_VALUE_NUMBER) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expression did not evaluate to double";
        }
        return false;
    }
    *out_value = value.d;
    return true;
}

bool cxpr_eval_ast_bool(const cxpr_ast* ast, const cxpr_context* ctx,
                        const cxpr_registry* reg, bool* out_value, cxpr_error* err) {
    cxpr_value value;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }

    value = cxpr_eval_ast_value(ast, ctx, reg, err);
    if (err && err->code != CXPR_OK) return false;
    if (value.type != CXPR_VALUE_BOOL) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expression did not evaluate to bool";
        }
        return false;
    }
    *out_value = value.b;
    return true;
}

bool cxpr_eval_ast_at_lookback(const cxpr_ast* ast,
                               const cxpr_ast* index_ast,
                               const cxpr_context* ctx,
                               const cxpr_registry* reg,
                               cxpr_value* out_value,
                               cxpr_error* err) {
    cxpr_ast* target_copy = NULL;
    cxpr_ast* index_copy = NULL;
    cxpr_ast* lookback_ast = NULL;
    bool ok = false;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }
    if (!ast || !index_ast) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Lookback evaluation requires target and index";
        }
        return false;
    }

    target_copy = cxpr_eval_clone_ast(ast);
    index_copy = cxpr_eval_clone_ast(index_ast);
    if (!target_copy || !index_copy) {
        cxpr_ast_free(target_copy);
        cxpr_ast_free(index_copy);
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return false;
    }

    lookback_ast = cxpr_ast_new_lookback(target_copy, index_copy);
    if (!lookback_ast) {
        cxpr_ast_free(target_copy);
        cxpr_ast_free(index_copy);
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return false;
    }

    ok = cxpr_eval_ast(lookback_ast, ctx, reg, out_value, err);
    cxpr_ast_free(lookback_ast);
    return ok;
}

bool cxpr_eval_ast_at_offset(const cxpr_ast* ast,
                             double lookback,
                             const cxpr_context* ctx,
                             const cxpr_registry* reg,
                             cxpr_value* out_value,
                             cxpr_error* err) {
    cxpr_ast* index_ast = NULL;
    bool ok = false;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }
    if (!isfinite(lookback) || lookback < 0.0) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Lookback offset must be a finite non-negative number";
        }
        return false;
    }

    index_ast = cxpr_ast_new_number(lookback);
    if (!index_ast) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return false;
    }

    ok = cxpr_eval_ast_at_lookback(ast, index_ast, ctx, reg, out_value, err);
    cxpr_ast_free(index_ast);
    return ok;
}

bool cxpr_eval_ast_number_at_offset(const cxpr_ast* ast,
                                    double lookback,
                                    const cxpr_context* ctx,
                                    const cxpr_registry* reg,
                                    double* out_value,
                                    cxpr_error* err) {
    cxpr_value value;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }
    if (!cxpr_eval_ast_at_offset(ast, lookback, ctx, reg, &value, err)) return false;
    if (value.type != CXPR_VALUE_NUMBER) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expected number";
        }
        return false;
    }
    *out_value = value.d;
    return true;
}

bool cxpr_eval_ast_bool_at_offset(const cxpr_ast* ast,
                                  double lookback,
                                  const cxpr_context* ctx,
                                  const cxpr_registry* reg,
                                  bool* out_value,
                                  cxpr_error* err) {
    cxpr_value value;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }
    if (!cxpr_eval_ast_at_offset(ast, lookback, ctx, reg, &value, err)) return false;
    if (value.type != CXPR_VALUE_BOOL) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expected bool";
        }
        return false;
    }
    *out_value = value.b;
    return true;
}
