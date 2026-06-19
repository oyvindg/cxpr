/**
 * @file constants.c
 * @brief Constant folding and producer cache-key helpers.
 */

#include "internal.h" // IWYU pragma: keep
#include "limits.h"

bool cxpr_eval_constant_double(const cxpr_ast* ast, double* out) {
    double left;
    double right;

    if (!ast || !out) return false;

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        *out = ast->data.number.value;
        return true;

    case CXPR_NODE_UNARY_OP:
        if (ast->data.unary_op.op != CXPR_TOK_MINUS ||
            !cxpr_eval_constant_double(ast->data.unary_op.operand, out)) {
            return false;
        }
        *out = -*out;
        return true;

    case CXPR_NODE_BINARY_OP:
        if (!cxpr_eval_constant_double(ast->data.binary_op.left, &left) ||
            !cxpr_eval_constant_double(ast->data.binary_op.right, &right)) {
            return false;
        }
        switch (ast->data.binary_op.op) {
        case CXPR_TOK_PLUS: *out = left + right; return true;
        case CXPR_TOK_MINUS: *out = left - right; return true;
        case CXPR_TOK_STAR: *out = left * right; return true;
        case CXPR_TOK_SLASH:
            if (right == 0.0) return false;
            *out = left / right;
            return true;
        case CXPR_TOK_PERCENT:
            if (right == 0.0) return false;
            *out = fmod(left, right);
            return true;
        case CXPR_TOK_POWER:
            *out = pow(left, right);
            return true;
        default:
            return false;
        }

    case CXPR_NODE_TERNARY:
        if (!cxpr_eval_constant_double(ast->data.ternary.condition, &left)) return false;
        if (left != 0.0) return cxpr_eval_constant_double(ast->data.ternary.true_branch, out);
        return cxpr_eval_constant_double(ast->data.ternary.false_branch, out);

    default:
        return false;
    }
}

static bool cxpr_eval_const_or_param_double(const cxpr_ast* ast,
                                            const cxpr_context* ctx,
                                            double* out) {
    double left;
    double right;
    bool found = false;

    if (!ast || !out) return false;

    switch (ast->type) {
    case CXPR_NODE_VARIABLE:
        if (!ctx) return false;
        *out = cxpr_context_get_param(ctx, ast->data.variable.name, &found);
        return found;

    case CXPR_NODE_NUMBER:
        *out = ast->data.number.value;
        return true;

    case CXPR_NODE_UNARY_OP:
        if (ast->data.unary_op.op != CXPR_TOK_MINUS ||
            !cxpr_eval_const_or_param_double(ast->data.unary_op.operand, ctx, out)) {
            return false;
        }
        *out = -*out;
        return true;

    case CXPR_NODE_BINARY_OP:
        if (!cxpr_eval_const_or_param_double(ast->data.binary_op.left, ctx, &left) ||
            !cxpr_eval_const_or_param_double(ast->data.binary_op.right, ctx, &right)) {
            return false;
        }
        switch (ast->data.binary_op.op) {
        case CXPR_TOK_PLUS: *out = left + right; return true;
        case CXPR_TOK_MINUS: *out = left - right; return true;
        case CXPR_TOK_STAR: *out = left * right; return true;
        case CXPR_TOK_SLASH:
            if (right == 0.0) return false;
            *out = left / right;
            return true;
        case CXPR_TOK_PERCENT:
            if (right == 0.0) return false;
            *out = fmod(left, right);
            return true;
        case CXPR_TOK_POWER:
            *out = pow(left, right);
            return true;
        default:
            return false;
        }

    case CXPR_NODE_TERNARY:
        if (!cxpr_eval_const_or_param_double(ast->data.ternary.condition, ctx, &left)) {
            return false;
        }
        if (left != 0.0) {
            return cxpr_eval_const_or_param_double(ast->data.ternary.true_branch, ctx, out);
        }
        return cxpr_eval_const_or_param_double(ast->data.ternary.false_branch, ctx, out);

    default:
        return false;
    }
}

const char* cxpr_eval_prepare_const_key_for_producer(const cxpr_ast* ast,
                                                     const cxpr_ast* const* ordered_args,
                                                     size_t argc,
                                                     const cxpr_context* ctx,
                                                     const cxpr_registry* reg,
                                                     char* local_buf,
                                                     size_t local_cap,
                                                     char** heap_buf,
                                                     cxpr_error* err) {
    cxpr_ast* mutable_ast = (cxpr_ast*)ast;
    const char* key;
    double values[CXPR_MAX_CALL_ARGS];

    if (heap_buf) *heap_buf = NULL;
    if (!ast || ast->type != CXPR_NODE_PRODUCER_ACCESS ||
        argc > CXPR_MAX_CALL_ARGS) {
        return NULL;
    }
    if (!cxpr_ast_producer_has_named_args(ast) &&
        ast->data.producer_access.cached_const_key_ready &&
        ast->data.producer_access.cached_const_key) {
        return ast->data.producer_access.cached_const_key;
    }
    if (!cxpr_ast_producer_has_named_args(ast) &&
        !ast->data.producer_access.cached_const_key_ready) {
        mutable_ast->data.producer_access.cached_const_key_ready = true;
        if (argc == 0u) {
            key = cxpr_build_struct_cache_key(ast->data.producer_access.name, values,
                                              0u,
                                              local_buf, local_cap, heap_buf);
            if (!key) return NULL;
            mutable_ast->data.producer_access.cached_const_key =
                heap_buf && *heap_buf ? *heap_buf : cxpr_strdup(key);
            if (heap_buf) *heap_buf = NULL;
            return mutable_ast->data.producer_access.cached_const_key;
        }
        for (size_t i = 0; i < argc; ++i) {
            if (!cxpr_eval_constant_double(ast->data.producer_access.args[i], &values[i])) {
                break;
            }
            if (i + 1u == argc) {
                key = cxpr_build_struct_cache_key(ast->data.producer_access.name, values,
                                                  argc,
                                                  local_buf, local_cap, heap_buf);
                if (!key) return NULL;

                mutable_ast->data.producer_access.cached_const_key =
                    heap_buf && *heap_buf ? *heap_buf : cxpr_strdup(key);
                if (heap_buf) *heap_buf = NULL;
                return mutable_ast->data.producer_access.cached_const_key;
            }
        }
    }

    if (ordered_args && ctx) {
        for (size_t i = 0; i < argc; ++i) {
            if (!cxpr_eval_const_or_param_double(ordered_args[i], ctx, &values[i])) {
                break;
            }
            if (i + 1u == argc) {
                key = cxpr_build_struct_cache_key(ast->data.producer_access.name, values,
                                                  argc,
                                                  local_buf, local_cap, heap_buf);
                if (!key) return NULL;
                return key;
            }
        }
    }

    if (!ordered_args || !ctx || !reg) return NULL;
    for (size_t i = 0; i < argc; ++i) {
        values[i] = cxpr_eval_scalar_arg(ordered_args[i], ctx, reg, err);
        if ((err && err->code != CXPR_OK) || !isfinite(values[i])) return NULL;
    }
    key = cxpr_build_struct_cache_key(ast->data.producer_access.name, values,
                                      argc,
                                      local_buf, local_cap, heap_buf);
    if (!key) return NULL;
    return key;
}

const char* cxpr_build_struct_cache_key(const char* name, const double* args, size_t argc,
                                        char* local_buf, size_t local_cap, char** heap_buf) {
    size_t len;
    size_t offset;
    char* key;
    int written;

    if (heap_buf) *heap_buf = NULL;
    if (!name) return NULL;
    if (argc == 0) return name;

    len = strlen(name) + 4 + (argc * 32);
    if (local_buf && len <= local_cap) {
        key = local_buf;
    } else {
        key = (char*)malloc(len);
        if (!key) return NULL;
        if (heap_buf) *heap_buf = key;
    }

    written = snprintf(key, len, "%s(", name);
    if (written < 0 || (size_t)written >= len) {
        if (heap_buf && *heap_buf) free(*heap_buf);
        if (heap_buf) *heap_buf = NULL;
        return NULL;
    }
    offset = (size_t)written;

    for (size_t i = 0; i < argc; ++i) {
        written = snprintf(key + offset, len - offset, i == 0 ? "%a" : ",%a", args[i]);
        if (written < 0 || (size_t)written >= len - offset) {
            if (heap_buf && *heap_buf) free(*heap_buf);
            if (heap_buf) *heap_buf = NULL;
            return NULL;
        }
        offset += (size_t)written;
    }

    written = snprintf(key + offset, len - offset, ")");
    if (written < 0 || (size_t)written >= len - offset) {
        if (heap_buf && *heap_buf) free(*heap_buf);
        if (heap_buf) *heap_buf = NULL;
        return NULL;
    }

    return key;
}
