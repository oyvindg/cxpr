/**
 * @file codegen.c
 * @brief cxpr AST -> C source transpiler (see cxpr/codegen.h).
 */

#include <cxpr/codegen.h>
#include <cxpr/typecheck.h>

#include "core.h"
#include "lookback.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── growable string buffer ──────────────────────────────────────────────── */

typedef struct {
    char* data;
    size_t len;
    size_t cap;
    bool oom;
} cxpr_cg_buf;

static void cxpr_cg_reserve(cxpr_cg_buf* b, size_t extra) {
    if (b->oom) return;
    if (b->len + extra + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        while (b->len + extra + 1 > cap) cap *= 2;
        char* grown = (char*)realloc(b->data, cap);
        if (!grown) { b->oom = true; return; }
        b->data = grown;
        b->cap = cap;
    }
}

static void cxpr_cg_puts(cxpr_cg_buf* b, const char* s) {
    size_t n = strlen(s);
    cxpr_cg_reserve(b, n);
    if (b->oom) return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void cxpr_cg_putc(cxpr_cg_buf* b, char c) {
    cxpr_cg_reserve(b, 1);
    if (b->oom) return;
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

static int cxpr_cg_err(cxpr_error* err, cxpr_error_code code, const char* msg) {
    if (err) { err->code = code; err->message = msg; }
    return 0; /* false */
}

static void cxpr_cg_format_double(char* out, size_t out_size, double value) {
    if (isfinite(value) && floor(value) == value) {
        snprintf(out, out_size, "%.1f", value);
    } else {
        snprintf(out, out_size, "%.17g", value);
    }
}

static int cxpr_cg_target_has_offset_leaf(const cxpr_c_target* target) {
    return target &&
           target->api_version == CXPR_C_TARGET_API_VERSION &&
           target->emit_leaf_at_offset;
}

static int cxpr_cg_target_has_call(const cxpr_c_target* target) {
    return target &&
           target->api_version == CXPR_C_TARGET_API_VERSION &&
           target->emit_call_at_offset;
}

/* ── function mapping ────────────────────────────────────────────────────── */

/* Default portable C/CUDA names for cxpr builtins. min/max are handled
 * separately (variadic -> nested fmin/fmax). Returns NULL to reject. */
static const char* cxpr_cg_default_function(const char* name, size_t argc) {
    static const char* const identity[] = {
        "sqrt", "cbrt", "exp", "exp2", "expm1", "log", "log2", "log10", "log1p",
        "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh",
        "tanh", "pow", "hypot", "copysign", "floor", "ceil", "round", "trunc",
        "fmod", "fmin", "fmax", "isnan", "isfinite", NULL
    };
    (void)argc;
    if (strcmp(name, "abs") == 0) return "fabs";
    if (strcmp(name, "mod") == 0) return "fmod";
    for (size_t i = 0; identity[i]; ++i)
        if (strcmp(name, identity[i]) == 0) return identity[i];
    return NULL;
}

static const char* cxpr_cg_binary_op_str(int op) {
    switch (op) {
    case CXPR_TOK_PLUS:  return "+";
    case CXPR_TOK_MINUS: return "-";
    case CXPR_TOK_STAR:  return "*";
    case CXPR_TOK_SLASH: return "/";
    case CXPR_TOK_EQ:    return "==";
    case CXPR_TOK_NEQ:   return "!=";
    case CXPR_TOK_LT:    return "<";
    case CXPR_TOK_GT:    return ">";
    case CXPR_TOK_LTE:   return "<=";
    case CXPR_TOK_GTE:   return ">=";
    case CXPR_TOK_AND:   return "&&";
    case CXPR_TOK_OR:    return "||";
    default:             return NULL;
    }
}

/* ── recursive emitter ───────────────────────────────────────────────────── */

static int cxpr_cg_emit_at_offset(const cxpr_ast* ast, unsigned lookback_offset,
                                  cxpr_cg_buf* b, const cxpr_c_target* target,
                                  cxpr_error* err);

static int cxpr_cg_emit_trend_call(const cxpr_ast* ast, unsigned lookback_offset,
                                   cxpr_cg_buf* b, const cxpr_c_target* target,
                                   cxpr_error* err, int rising) {
    const char* name = rising ? "rising" : "falling";
    size_t argc = cxpr_ast_function_argc(ast);
    const cxpr_ast* value_ast = NULL;
    const cxpr_ast* bars_ast = NULL;
    double raw;
    unsigned bars;

    if (argc != 2u) {
        return cxpr_cg_err(err, CXPR_ERR_SYNTAX, "rising/falling codegen requires value and literal bars");
    }
    for (size_t i = 0u; i < argc; ++i) {
        const char* arg_name = cxpr_ast_function_arg_name(ast, i);
        if (arg_name && strcmp(arg_name, "value") == 0) {
            value_ast = cxpr_ast_function_arg(ast, i);
        } else if (arg_name && (strcmp(arg_name, "bars") == 0 || strcmp(arg_name, "samples") == 0)) {
            bars_ast = cxpr_ast_function_arg(ast, i);
        }
    }
    if (!value_ast) value_ast = cxpr_ast_function_arg(ast, 0u);
    if (!bars_ast) bars_ast = cxpr_ast_function_arg(ast, 1u);
    if (!bars_ast || cxpr_ast_type(bars_ast) != CXPR_NODE_NUMBER) {
        static CXPR_THREAD_LOCAL char msg[128];
        snprintf(msg, sizeof(msg), "%s codegen requires a constant bars argument", name);
        return cxpr_cg_err(err, CXPR_ERR_SYNTAX, msg);
    }
    raw = cxpr_ast_number_value(bars_ast);
    bars = raw >= 0.0 ? (unsigned)(raw + 0.5) : 0u;
    if (!isfinite(raw) || raw < 2.0 || fabs(raw - (double)bars) > 1e-9) {
        static CXPR_THREAD_LOCAL char msg[128];
        snprintf(msg, sizeof(msg), "%s codegen bars must be an integer >= 2", name);
        return cxpr_cg_err(err, CXPR_ERR_SYNTAX, msg);
    }

    cxpr_cg_putc(b, '(');
    for (unsigned i = 0u; i + 1u < bars; ++i) {
        if (i > 0u) cxpr_cg_puts(b, " && ");
        cxpr_cg_putc(b, '(');
        if (!cxpr_cg_emit_at_offset(value_ast, lookback_offset + i, b, target, err)) return 0;
        cxpr_cg_puts(b, rising ? " > " : " < ");
        if (!cxpr_cg_emit_at_offset(value_ast, lookback_offset + i + 1u, b, target, err)) return 0;
        cxpr_cg_putc(b, ')');
    }
    cxpr_cg_putc(b, ')');
    return 1;
}

static int cxpr_cg_emit_repeat_call(const cxpr_ast* ast, unsigned lookback_offset,
                                    cxpr_cg_buf* b, const cxpr_c_target* target,
                                    cxpr_error* err) {
    size_t argc = cxpr_ast_function_argc(ast);
    const cxpr_ast* condition_ast = NULL;
    const cxpr_ast* bars_ast = NULL;
    double raw;
    unsigned bars;

    if (argc != 2u) {
        return cxpr_cg_err(err, CXPR_ERR_SYNTAX, "repeat codegen requires condition and literal bars");
    }
    for (size_t i = 0u; i < argc; ++i) {
        const char* arg_name = cxpr_ast_function_arg_name(ast, i);
        if (arg_name && strcmp(arg_name, "condition") == 0) {
            condition_ast = cxpr_ast_function_arg(ast, i);
        } else if (arg_name && (strcmp(arg_name, "bars") == 0 || strcmp(arg_name, "samples") == 0)) {
            bars_ast = cxpr_ast_function_arg(ast, i);
        }
    }
    if (!condition_ast) condition_ast = cxpr_ast_function_arg(ast, 0u);
    if (!bars_ast) bars_ast = cxpr_ast_function_arg(ast, 1u);
    if (!bars_ast || cxpr_ast_type(bars_ast) != CXPR_NODE_NUMBER) {
        return cxpr_cg_err(err, CXPR_ERR_SYNTAX, "repeat codegen requires a constant bars argument");
    }
    raw = cxpr_ast_number_value(bars_ast);
    bars = raw >= 0.0 ? (unsigned)(raw + 0.5) : 0u;
    if (!isfinite(raw) || raw < 0.0 || fabs(raw - (double)bars) > 1e-9) {
        return cxpr_cg_err(err, CXPR_ERR_SYNTAX, "repeat codegen bars must be a non-negative integer");
    }
    if (bars == 0u) {
        cxpr_cg_puts(b, "true");
        return 1;
    }
    cxpr_cg_putc(b, '(');
    for (unsigned i = 0u; i < bars; ++i) {
        if (i > 0u) cxpr_cg_puts(b, " && ");
        cxpr_cg_putc(b, '(');
        if (!cxpr_cg_emit_at_offset(condition_ast, lookback_offset + i, b, target, err)) return 0;
        cxpr_cg_putc(b, ')');
    }
    cxpr_cg_putc(b, ')');
    return 1;
}

static int cxpr_cg_emit_call_at_offset(const cxpr_ast* ast, unsigned lookback_offset,
                                       cxpr_cg_buf* b, const cxpr_c_target* target,
                                       cxpr_error* err) {
    const char* name = cxpr_ast_function_name(ast);
    size_t argc = cxpr_ast_function_argc(ast);

    if (cxpr_cg_target_has_call(target)) {
        bool handled = false;
        cxpr_error herr = {0};
        char* out = target->emit_call_at_offset(ast, lookback_offset, target->userdata, &handled, &herr);
        if (handled) {
            if (!out) {
                if (err) *err = herr;
                return 0;
            }
            cxpr_cg_puts(b, out);
            free(out);
            return 1;
        }
        free(out); /* defensive: ignored when not handled */
    }

    if (strcmp(name, "rising") == 0 || strcmp(name, "falling") == 0) {
        return cxpr_cg_emit_trend_call(
            ast, lookback_offset, b, target, err, strcmp(name, "rising") == 0);
    }
    if (strcmp(name, "repeat") == 0) {
        return cxpr_cg_emit_repeat_call(ast, lookback_offset, b, target, err);
    }

    /* min/max: variadic -> nested fmin/fmax (right-folded). */
    if ((strcmp(name, "min") == 0 || strcmp(name, "max") == 0) && argc >= 1) {
        const char* fn = (name[1] == 'i') ? "fmin" : "fmax";
        if (argc == 1) return cxpr_cg_emit_at_offset(cxpr_ast_function_arg(ast, 0), lookback_offset, b, target, err);
        for (size_t i = 0; i + 1 < argc; ++i) { cxpr_cg_puts(b, fn); cxpr_cg_putc(b, '('); }
        if (!cxpr_cg_emit_at_offset(cxpr_ast_function_arg(ast, 0), lookback_offset, b, target, err)) return 0;
        for (size_t i = 1; i < argc; ++i) {
            cxpr_cg_puts(b, ", ");
            if (!cxpr_cg_emit_at_offset(cxpr_ast_function_arg(ast, i), lookback_offset, b, target, err)) return 0;
            cxpr_cg_putc(b, ')');
        }
        return 1;
    }

    const char* mapped = NULL;
    if (target && target->map_function) mapped = target->map_function(name, argc, target->userdata);
    if (!mapped) mapped = cxpr_cg_default_function(name, argc);
    if (!mapped) {
        static CXPR_THREAD_LOCAL char msg[128];
        snprintf(msg, sizeof(msg),
                 "no C mapping for function '%s' (supply one via cxpr_c_target.map_function)", name);
        return cxpr_cg_err(err, CXPR_ERR_SYNTAX, msg);
    }
    cxpr_cg_puts(b, mapped);
    cxpr_cg_putc(b, '(');
    for (size_t i = 0; i < argc; ++i) {
        if (i) cxpr_cg_puts(b, ", ");
        if (!cxpr_cg_emit_at_offset(cxpr_ast_function_arg(ast, i), lookback_offset, b, target, err)) return 0;
    }
    cxpr_cg_putc(b, ')');
    return 1;
}

static int cxpr_cg_emit_hooked_leaf(const cxpr_ast* ast, unsigned lookback_offset,
                                    cxpr_cg_buf* b, const cxpr_c_target* target,
                                    cxpr_error* err) {
    char* out;
    if (!cxpr_cg_target_has_offset_leaf(target)) return 0;
    out = target->emit_leaf_at_offset(ast, lookback_offset, target->userdata, err);
    if (!out) return 0;
    cxpr_cg_puts(b, out);
    free(out);
    return 1;
}

static int cxpr_cg_emit_at_offset(const cxpr_ast* ast, unsigned lookback_offset,
                                  cxpr_cg_buf* b, const cxpr_c_target* target,
                                  cxpr_error* err) {
    if (!ast) return cxpr_cg_err(err, CXPR_ERR_SYNTAX, "NULL AST node");

    switch (cxpr_ast_type(ast)) {
    case CXPR_NODE_NUMBER: {
        char num[32];
        cxpr_cg_format_double(num, sizeof(num), cxpr_ast_number_value(ast));
        cxpr_cg_puts(b, num);
        return 1;
    }
    case CXPR_NODE_BOOL:
        cxpr_cg_puts(b, cxpr_ast_bool_value(ast) ? "true" : "false");
        return 1;
    case CXPR_NODE_STRING: {
        const char* s = cxpr_ast_string_value(ast);
        cxpr_cg_putc(b, '"');
        for (; s && *s; ++s) {
            if (*s == '"' || *s == '\\') cxpr_cg_putc(b, '\\');
            cxpr_cg_putc(b, *s);
        }
        cxpr_cg_putc(b, '"');
        return 1;
    }
    case CXPR_NODE_IDENTIFIER:
        if (cxpr_cg_target_has_offset_leaf(target)) {
            return cxpr_cg_emit_hooked_leaf(ast, lookback_offset, b, target, err);
        }
        if (lookback_offset > 0u) {
            return cxpr_cg_err(err, CXPR_ERR_SYNTAX,
                               "lookback codegen requires cxpr_c_target.emit_leaf_at_offset");
        }
        cxpr_cg_puts(b, cxpr_ast_identifier_name(ast));
        return 1;
    case CXPR_NODE_VARIABLE: {
        const char* name = cxpr_ast_variable_name(ast);
        if (cxpr_cg_target_has_offset_leaf(target)) {
            return cxpr_cg_emit_hooked_leaf(ast, lookback_offset, b, target, err);
        }
        if (lookback_offset > 0u) {
            return cxpr_cg_err(err, CXPR_ERR_SYNTAX,
                               "lookback codegen requires cxpr_c_target.emit_leaf_at_offset");
        }
        if (name && name[0] == '$') name++; /* emit the bare name */
        cxpr_cg_puts(b, name ? name : "");
        return 1;
    }
    case CXPR_NODE_BINARY_OP: {
        int op = cxpr_ast_operator(ast);
        const cxpr_ast* l = cxpr_ast_left(ast);
        const cxpr_ast* r = cxpr_ast_right(ast);
        if (op == CXPR_TOK_POWER || op == CXPR_TOK_PERCENT) {
            cxpr_cg_puts(b, op == CXPR_TOK_POWER ? "pow(" : "fmod(");
            if (!cxpr_cg_emit_at_offset(l, lookback_offset, b, target, err)) return 0;
            cxpr_cg_puts(b, ", ");
            if (!cxpr_cg_emit_at_offset(r, lookback_offset, b, target, err)) return 0;
            cxpr_cg_putc(b, ')');
            return 1;
        }
        const char* ops = cxpr_cg_binary_op_str(op);
        if (!ops) return cxpr_cg_err(err, CXPR_ERR_SYNTAX, "unsupported binary operator in C codegen");
        cxpr_cg_putc(b, '(');
        if (!cxpr_cg_emit_at_offset(l, lookback_offset, b, target, err)) return 0;
        cxpr_cg_putc(b, ' '); cxpr_cg_puts(b, ops); cxpr_cg_putc(b, ' ');
        if (!cxpr_cg_emit_at_offset(r, lookback_offset, b, target, err)) return 0;
        cxpr_cg_putc(b, ')');
        return 1;
    }
    case CXPR_NODE_UNARY_OP: {
        int op = cxpr_ast_operator(ast);
        if (op != CXPR_TOK_MINUS && op != CXPR_TOK_NOT)
            return cxpr_cg_err(err, CXPR_ERR_SYNTAX, "unsupported unary operator in C codegen");
        cxpr_cg_putc(b, '(');
        cxpr_cg_putc(b, op == CXPR_TOK_MINUS ? '-' : '!');
        if (!cxpr_cg_emit_at_offset(cxpr_ast_operand(ast), lookback_offset, b, target, err)) return 0;
        cxpr_cg_putc(b, ')');
        return 1;
    }
    case CXPR_NODE_TERNARY:
        cxpr_cg_putc(b, '(');
        if (!cxpr_cg_emit_at_offset(cxpr_ast_ternary_condition(ast), lookback_offset, b, target, err)) return 0;
        cxpr_cg_puts(b, " ? ");
        if (!cxpr_cg_emit_at_offset(cxpr_ast_ternary_true_branch(ast), lookback_offset, b, target, err)) return 0;
        cxpr_cg_puts(b, " : ");
        if (!cxpr_cg_emit_at_offset(cxpr_ast_ternary_false_branch(ast), lookback_offset, b, target, err)) return 0;
        cxpr_cg_putc(b, ')');
        return 1;
    case CXPR_NODE_FUNCTION_CALL:
        return cxpr_cg_emit_call_at_offset(ast, lookback_offset, b, target, err);
    case CXPR_NODE_LOOKBACK: {
        const cxpr_ast* index = cxpr_ast_lookback_index(ast);
        unsigned offset;
        unsigned next_offset;
        if (!cxpr_lookback_literal_offset(index, &offset, NULL, NULL)) {
            if (target && target->api_version == CXPR_C_TARGET_API_VERSION &&
                target->emit_lookback_at_offset) {
                char* dynamic = target->emit_lookback_at_offset(
                    ast, lookback_offset, target->userdata, err);
                if (!dynamic) return 0;
                cxpr_cg_puts(b, dynamic);
                free(dynamic);
                return !b->oom;
            }
            return cxpr_cg_err(err, CXPR_ERR_SYNTAX,
                               "C codegen requires constant integer lookback indexes");
        }
        if (!cxpr_lookback_add_unsigned(
                lookback_offset, offset, &next_offset, err, "C codegen lookback offset overflow")) return 0;
        return cxpr_cg_emit_at_offset(
            cxpr_ast_lookback_target(ast), next_offset, b, target, err);
    }
    case CXPR_NODE_FIELD_ACCESS:
        if (cxpr_cg_target_has_offset_leaf(target)) {
            return cxpr_cg_emit_hooked_leaf(ast, lookback_offset, b, target, err);
        }
        if (lookback_offset > 0u) {
            return cxpr_cg_err(err, CXPR_ERR_SYNTAX,
                               "lookback codegen requires cxpr_c_target.emit_leaf_at_offset");
        }
        /* fallthrough */
    case CXPR_NODE_CHAIN_ACCESS:
        if (cxpr_cg_target_has_offset_leaf(target)) {
            return cxpr_cg_emit_hooked_leaf(ast, lookback_offset, b, target, err);
        }
        if (lookback_offset > 0u) {
            return cxpr_cg_err(err, CXPR_ERR_SYNTAX,
                               "lookback codegen requires cxpr_c_target.emit_leaf_at_offset");
        }
        /* fallthrough */
    case CXPR_NODE_PRODUCER_ACCESS:
        if (cxpr_cg_target_has_offset_leaf(target)) {
            return cxpr_cg_emit_hooked_leaf(ast, lookback_offset, b, target, err);
        }
        if (lookback_offset > 0u) {
            return cxpr_cg_err(err, CXPR_ERR_SYNTAX,
                               "lookback codegen requires cxpr_c_target.emit_leaf_at_offset");
        }
        return cxpr_cg_err(err, CXPR_ERR_SYNTAX,
                      "field/chain/producer nodes have no standalone C form");
    default:
        return cxpr_cg_err(err, CXPR_ERR_SYNTAX, "unsupported AST node in C codegen");
    }
}

bool cxpr_codegen_emit_lookback_offset(const cxpr_ast* ast,
                                       int current_offset,
                                       cxpr_c_emit_offset_fn emit,
                                       void* userdata,
                                       cxpr_error* err) {
    const cxpr_ast* index_ast;
    unsigned offset;
    int next_offset;

    if (err) *err = (cxpr_error){0};
    if (!ast || cxpr_ast_type(ast) != CXPR_NODE_LOOKBACK || !emit) {
        cxpr_cg_err(err, CXPR_ERR_SYNTAX, "invalid lookback codegen arguments");
        return false;
    }
    index_ast = cxpr_ast_lookback_index(ast);
    if (!cxpr_lookback_literal_offset(
            index_ast, &offset, err, "codegen requires constant integer lookback indexes")) return false;
    if (!cxpr_lookback_add_int(
            current_offset, offset, &next_offset, err, "codegen lookback offset overflow")) return false;
    return emit(userdata, cxpr_ast_lookback_target(ast), next_offset, err);
}

char* cxpr_ast_to_c(const cxpr_ast* ast, const cxpr_c_target* target, cxpr_error* err) {
    return cxpr_ast_to_c_at_offset(ast, 0u, target, err);
}

char* cxpr_ast_to_c_at_offset(const cxpr_ast* ast, unsigned lookback_offset,
                              const cxpr_c_target* target, cxpr_error* err) {
    cxpr_cg_buf b = {0};
    if (err) *err = (cxpr_error){0};
    if (!cxpr_cg_target_has_call(target) && !cxpr_typecheck(ast, NULL, NULL, err)) return NULL;
    if (!cxpr_cg_emit_at_offset(ast, lookback_offset, &b, target, err)) { free(b.data); return NULL; }
    if (b.oom) { free(b.data); cxpr_cg_err(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory"); return NULL; }
    return b.data ? b.data : cxpr_strdup("");
}

/* ── interdependent set: topological order over inter-name references ─────── */

#define CXPR_CG_MAX_REFS 64

char* cxpr_exprset_to_c(const cxpr_c_named_expr* exprs, size_t count,
                        const char* decl_type, const cxpr_c_target* target,
                        cxpr_error* err) {
    if (err) *err = (cxpr_error){0};
    if (!exprs || !decl_type) { cxpr_cg_err(err, CXPR_ERR_SYNTAX, "invalid arguments"); return NULL; }
    if (count == 0) return cxpr_strdup("");

    /* emitted[i] = already written; temp[i] = on the current DFS stack. */
    unsigned char* emitted = (unsigned char*)calloc(count, 1);
    unsigned char* temp = (unsigned char*)calloc(count, 1);
    size_t* order = (size_t*)malloc(count * sizeof(size_t));
    size_t order_n = 0;
    if (!emitted || !temp || !order) {
        free(emitted); free(temp); free(order);
        cxpr_cg_err(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return NULL;
    }

    /* Iterative-friendly recursive DFS via an explicit helper using recursion
     * over a small N. Cap N to keep the stack bounded. */
    size_t stack[256];
    if (count > 256) {
        free(emitted); free(temp); free(order);
        cxpr_cg_err(err, CXPR_ERR_SYNTAX, "too many expressions for codegen (max 256)");
        return NULL;
    }

    for (size_t start = 0; start < count; ++start) {
        if (emitted[start]) continue;
        size_t sp = 0;
        stack[sp++] = start;
        temp[start] = 1;
        while (sp > 0) {
            size_t i = stack[sp - 1];
            const char* refs[CXPR_CG_MAX_REFS];
            size_t nrefs = cxpr_ast_references(exprs[i].ast, refs, CXPR_CG_MAX_REFS);
            if (nrefs > CXPR_CG_MAX_REFS) nrefs = CXPR_CG_MAX_REFS;
            size_t pushed = 0;
            for (size_t k = 0; k < nrefs && !pushed; ++k) {
                for (size_t j = 0; j < count; ++j) {
                    if (j == i || emitted[j]) continue;
                    if (strcmp(refs[k], exprs[j].name) != 0) continue;
                    if (temp[j]) {
                        free(emitted); free(temp); free(order);
                        cxpr_cg_err(err, CXPR_ERR_CIRCULAR_DEPENDENCY,
                               "dependency cycle among expressions");
                        return NULL;
                    }
                    temp[j] = 1;
                    stack[sp++] = j;
                    pushed = 1;
                    break;
                }
            }
            if (!pushed) {
                emitted[i] = 1;
                temp[i] = 0;
                order[order_n++] = i;
                sp--;
            }
        }
    }

    cxpr_cg_buf b = {0};
    for (size_t k = 0; k < order_n; ++k) {
        size_t i = order[k];
        char* expr_c = cxpr_ast_to_c(exprs[i].ast, target, err);
        if (!expr_c) { free(b.data); free(emitted); free(temp); free(order); return NULL; }
        cxpr_cg_puts(&b, decl_type);
        cxpr_cg_putc(&b, ' ');
        cxpr_cg_puts(&b, exprs[i].name);
        cxpr_cg_puts(&b, " = ");
        cxpr_cg_puts(&b, expr_c);
        cxpr_cg_puts(&b, ";\n");
        free(expr_c);
    }

    free(emitted); free(temp); free(order);
    if (b.oom) { free(b.data); cxpr_cg_err(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory"); return NULL; }
    return b.data ? b.data : cxpr_strdup("");
}

char* cxpr_exprset_to_c_function(const char* qualifiers, const char* return_struct,
                                 const char* scalar_type, const char* function_name,
                                 const char* const* inputs, size_t input_count,
                                 const cxpr_c_named_expr* exprs, size_t count,
                                 const cxpr_c_target* target, cxpr_error* err) {
    char indented_decl[64];
    cxpr_cg_buf b = {0};
    char* block;

    if (err) *err = (cxpr_error){0};
    if (!return_struct || !scalar_type || !function_name || (count && !exprs) ||
        (input_count && !inputs)) {
        cxpr_cg_err(err, CXPR_ERR_SYNTAX, "invalid arguments");
        return NULL;
    }

    /* Declaration block (topologically ordered), indented for the function body. */
    snprintf(indented_decl, sizeof(indented_decl), "    %s", scalar_type);
    block = cxpr_exprset_to_c(exprs, count, indented_decl, target, err);
    if (!block) return NULL;

    /* Result struct: one field per expression name. */
    cxpr_cg_puts(&b, "typedef struct ");
    cxpr_cg_puts(&b, return_struct);
    cxpr_cg_puts(&b, " {\n");
    for (size_t i = 0; i < count; ++i) {
        cxpr_cg_puts(&b, "    ");
        cxpr_cg_puts(&b, scalar_type);
        cxpr_cg_putc(&b, ' ');
        cxpr_cg_puts(&b, exprs[i].name);
        cxpr_cg_puts(&b, ";\n");
    }
    cxpr_cg_puts(&b, "} ");
    cxpr_cg_puts(&b, return_struct);
    cxpr_cg_puts(&b, ";\n\n");

    /* Function signature: one scalar param per input. */
    if (qualifiers && *qualifiers) { cxpr_cg_puts(&b, qualifiers); cxpr_cg_putc(&b, ' '); }
    cxpr_cg_puts(&b, return_struct);
    cxpr_cg_putc(&b, ' ');
    cxpr_cg_puts(&b, function_name);
    cxpr_cg_putc(&b, '(');
    if (input_count == 0) {
        cxpr_cg_puts(&b, "void");
    } else {
        for (size_t i = 0; i < input_count; ++i) {
            if (i) cxpr_cg_puts(&b, ", ");
            cxpr_cg_puts(&b, scalar_type);
            cxpr_cg_putc(&b, ' ');
            cxpr_cg_puts(&b, inputs[i]);
        }
    }
    cxpr_cg_puts(&b, ") {\n");
    cxpr_cg_puts(&b, block);   /* "    <type> name = ...;\n" lines */
    cxpr_cg_puts(&b, "    ");
    cxpr_cg_puts(&b, return_struct);
    cxpr_cg_puts(&b, " _cx_out;\n");
    for (size_t i = 0; i < count; ++i) {
        cxpr_cg_puts(&b, "    _cx_out.");
        cxpr_cg_puts(&b, exprs[i].name);
        cxpr_cg_puts(&b, " = ");
        cxpr_cg_puts(&b, exprs[i].name);
        cxpr_cg_puts(&b, ";\n");
    }
    cxpr_cg_puts(&b, "    return _cx_out;\n}\n");

    free(block);
    if (b.oom) { free(b.data); cxpr_cg_err(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory"); return NULL; }
    return b.data ? b.data : cxpr_strdup("");
}
