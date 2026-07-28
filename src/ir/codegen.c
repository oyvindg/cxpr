/**
 * @file codegen.c
 * @brief IR -> C source backend for scalar cxpr programs.
 */

#include "internal.h"
#include <cxpr/codegen.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* data;
    size_t len;
    size_t cap;
    bool oom;
} cxpr_ir_c_buf;

static void cxpr_ir_c_reserve(cxpr_ir_c_buf* b, size_t extra) {
    if (!b || b->oom) return;
    if (b->len + extra + 1u > b->cap) {
        size_t cap = b->cap ? b->cap : 256u;
        char* grown;
        while (b->len + extra + 1u > cap) cap *= 2u;
        grown = (char*)realloc(b->data, cap);
        if (!grown) {
            b->oom = true;
            return;
        }
        b->data = grown;
        b->cap = cap;
    }
}

static void cxpr_ir_c_puts(cxpr_ir_c_buf* b, const char* s) {
    size_t n;
    if (!s) return;
    n = strlen(s);
    cxpr_ir_c_reserve(b, n);
    if (!b || b->oom) return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void cxpr_ir_c_printf(cxpr_ir_c_buf* b, const char* fmt, ...) {
    va_list ap;
    va_list ap_copy;
    int needed;
    if (!b || b->oom || !fmt) return;
    va_start(ap, fmt);
    va_copy(ap_copy, ap);
    needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) {
        b->oom = true;
        va_end(ap_copy);
        return;
    }
    cxpr_ir_c_reserve(b, (size_t)needed);
    if (!b->oom) {
        vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap_copy);
        b->len += (size_t)needed;
    }
    va_end(ap_copy);
}

static bool cxpr_ir_c_fail(cxpr_error* err, cxpr_error_code code, const char* message) {
    if (err) {
        err->code = code;
        err->message = message;
    }
    return false;
}

static char* cxpr_ir_c_safe_name(const char* prefix, const char* name, size_t index) {
    const char* src = name ? name : "";
    size_t prefix_len = prefix ? strlen(prefix) : 0u;
    size_t src_len = strlen(src);
    size_t cap = prefix_len + src_len + 32u;
    char* out = (char*)malloc(cap);
    size_t pos = 0u;
    if (!out) return NULL;
    if (prefix_len > 0u) {
        memcpy(out, prefix, prefix_len);
        pos = prefix_len;
    }
    if (src_len == 0u) {
        pos += (size_t)snprintf(out + pos, cap - pos, "%zu", index);
    } else {
        for (size_t i = 0u; i < src_len && pos + 2u < cap; ++i) {
            unsigned char ch = (unsigned char)src[i];
            out[pos++] = (isalnum(ch) || ch == '_') ? (char)ch : '_';
        }
    }
    if (pos == 0u || isdigit((unsigned char)out[0])) {
        if (pos + 3u >= cap) {
            char* grown = (char*)realloc(out, cap + 3u);
            if (!grown) {
                free(out);
                return NULL;
            }
            out = grown;
            cap += 3u;
        }
        memmove(out + 3u, out, pos);
        memcpy(out, "cx_", 3u);
        pos += 3u;
    }
    out[pos] = '\0';
    return out;
}

static const cxpr_c_program_arg* cxpr_ir_c_find_arg(const cxpr_c_program_arg* args,
                                                    size_t arg_count,
                                                    cxpr_c_program_arg_kind kind,
                                                    const char* name,
                                                    size_t local_index) {
    for (size_t i = 0u; i < arg_count; ++i) {
        if (args[i].kind != kind) continue;
        if (kind == CXPR_C_PROGRAM_ARG_LOCAL) {
            if (args[i].local_index == local_index) return &args[i];
        } else if (name && args[i].name && strcmp(args[i].name, name) == 0) {
            return &args[i];
        }
    }
    return NULL;
}

static const char* cxpr_ir_c_arg_prefix(cxpr_c_program_arg_kind kind) {
    switch (kind) {
    case CXPR_C_PROGRAM_ARG_PARAM: return "p_";
    case CXPR_C_PROGRAM_ARG_LOCAL: return "l_";
    case CXPR_C_PROGRAM_ARG_VAR:
    default: return "";
    }
}

static char* cxpr_ir_c_arg_name(const cxpr_c_program_arg* arg, size_t ordinal) {
    if (!arg) return NULL;
    if (arg->c_name && arg->c_name[0] != '\0') return cxpr_ir_c_safe_name(NULL, arg->c_name, ordinal);
    return cxpr_ir_c_safe_name(cxpr_ir_c_arg_prefix(arg->kind), arg->name, arg->local_index);
}

static bool cxpr_ir_c_emit_load_arg(cxpr_ir_c_buf* b,
                                    const cxpr_c_program_arg* args,
                                    size_t arg_count,
                                    cxpr_c_program_arg_kind kind,
                                    const char* name,
                                    size_t local_index,
                                    cxpr_error* err) {
    const cxpr_c_program_arg* arg = cxpr_ir_c_find_arg(args, arg_count, kind, name, local_index);
    char* c_name;
    if (!arg) return cxpr_ir_c_fail(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "C backend missing explicit argument binding");
    c_name = cxpr_ir_c_arg_name(arg, (size_t)(arg - args));
    if (!c_name) return cxpr_ir_c_fail(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
    cxpr_ir_c_printf(b, "    _cx_s[_cx_sp++] = %s;\n", c_name);
    free(c_name);
    return true;
}

static bool cxpr_ir_c_emit_load_local(cxpr_ir_c_buf* b,
                                      const cxpr_c_program_arg* args,
                                      size_t arg_count,
                                      size_t local_index,
                                      cxpr_error* err) {
    const cxpr_c_program_arg* arg =
        cxpr_ir_c_find_arg(args, arg_count, CXPR_C_PROGRAM_ARG_LOCAL, NULL, local_index);
    char* c_name;

    if (!arg) {
        cxpr_ir_c_printf(b, "    _cx_s[_cx_sp++] = _cx_l[%zu];\n", local_index);
        return !b->oom;
    }
    c_name = cxpr_ir_c_arg_name(arg, (size_t)(arg - args));
    if (!c_name) return cxpr_ir_c_fail(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
    cxpr_ir_c_printf(b, "    _cx_s[_cx_sp++] = %s;\n", c_name);
    free(c_name);
    return true;
}

static bool cxpr_ir_c_emit_load_local_square(cxpr_ir_c_buf* b,
                                             const cxpr_c_program_arg* args,
                                             size_t arg_count,
                                             size_t local_index,
                                             cxpr_error* err) {
    if (!cxpr_ir_c_emit_load_local(b, args, arg_count, local_index, err)) return false;
    cxpr_ir_c_puts(b, "    _cx_s[_cx_sp - 1u] = _cx_s[_cx_sp - 1u] * _cx_s[_cx_sp - 1u];\n");
    return !b->oom;
}

static size_t cxpr_ir_c_local_slot_count(const cxpr_ir_program* ir,
                                         const cxpr_c_program_arg* args,
                                         size_t arg_count) {
    size_t count = 0u;

    if (!ir) return 0u;
    for (size_t i = 0u; i < ir->count; ++i) {
        const cxpr_ir_instr* instr = &ir->code[i];
        if (instr->op == CXPR_OP_STORE_LOCAL) {
            if (instr->index + 1u > count) count = instr->index + 1u;
        } else if (instr->op == CXPR_OP_LOAD_LOCAL ||
                   instr->op == CXPR_OP_LOAD_LOCAL_SQUARE) {
            if (!cxpr_ir_c_find_arg(
                    args,
                    arg_count,
                    CXPR_C_PROGRAM_ARG_LOCAL,
                    NULL,
                    instr->index) &&
                instr->index + 1u > count) {
                count = instr->index + 1u;
            }
        }
    }
    return count;
}

static const char* cxpr_ir_c_unary_math(cxpr_opcode op) {
    switch (op) {
    case CXPR_OP_SQRT: return "sqrt";
    case CXPR_OP_ABS: return "fabs";
    case CXPR_OP_FLOOR: return "floor";
    case CXPR_OP_CEIL: return "ceil";
    case CXPR_OP_ROUND: return "round";
    default: return NULL;
    }
}

static const char* cxpr_ir_c_binary_op(cxpr_opcode op) {
    switch (op) {
    case CXPR_OP_ADD: return "+";
    case CXPR_OP_SUB: return "-";
    case CXPR_OP_MUL: return "*";
    case CXPR_OP_DIV: return "/";
    case CXPR_OP_CMP_EQ: return "==";
    case CXPR_OP_CMP_NEQ: return "!=";
    case CXPR_OP_CMP_LT: return "<";
    case CXPR_OP_CMP_LTE: return "<=";
    case CXPR_OP_CMP_GT: return ">";
    case CXPR_OP_CMP_GTE: return ">=";
    default: return NULL;
    }
}

static bool cxpr_ir_c_emit_args(cxpr_ir_c_buf* b,
                                const char* scalar_type,
                                const cxpr_c_program_arg* args,
                                size_t arg_count,
                                cxpr_error* err) {
    for (size_t i = 0u; i < arg_count; ++i) {
        char* c_name = cxpr_ir_c_arg_name(&args[i], i);
        if (!c_name) return cxpr_ir_c_fail(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        if (i > 0u) cxpr_ir_c_puts(b, ", ");
        cxpr_ir_c_printf(b, "%s %s", scalar_type, c_name);
        free(c_name);
    }
    if (arg_count == 0u) cxpr_ir_c_puts(b, "void");
    return true;
}

char* cxpr_expr_compiled_to_c_function(const cxpr_expr_compiled* prog,
                                 const char* qualifiers,
                                 const char* return_type,
                                 const char* function_name,
                                 const cxpr_c_program_arg* args,
                                 size_t arg_count,
                                 cxpr_error* err) {
    const cxpr_ir_program* ir;
    const char* scalar_type = return_type && return_type[0] ? return_type : "double";
    char* safe_function_name;
    size_t local_slot_count;
    cxpr_ir_c_buf b = {0};

    if (err) *err = (cxpr_error){0};
    if (!prog || !function_name) {
        cxpr_ir_c_fail(err, CXPR_ERR_SYNTAX, "Invalid C backend arguments");
        return NULL;
    }
    ir = &prog->ir;
    safe_function_name = cxpr_ir_c_safe_name(NULL, function_name, 0u);
    if (!safe_function_name) {
        cxpr_ir_c_fail(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return NULL;
    }

    if (qualifiers && qualifiers[0]) cxpr_ir_c_printf(&b, "%s ", qualifiers);
    cxpr_ir_c_printf(&b, "%s %s(", scalar_type, safe_function_name);
    free(safe_function_name);
    if (!cxpr_ir_c_emit_args(&b, "double", args, arg_count, err)) {
        free(b.data);
        return NULL;
    }
    cxpr_ir_c_puts(&b, ") {\n");
    cxpr_ir_c_printf(&b, "    double _cx_s[%zu];\n", ir->count + 1u);
    cxpr_ir_c_puts(&b, "    unsigned _cx_sp = 0u;\n");
    local_slot_count = cxpr_ir_c_local_slot_count(ir, args, arg_count);
    if (local_slot_count > 0u) {
        cxpr_ir_c_printf(&b, "    double _cx_l[%zu] = {0};\n", local_slot_count);
    }

    for (size_t i = 0u; i < ir->count; ++i) {
        const cxpr_ir_instr* instr = &ir->code[i];
        const char* op;
        const char* fn;
        cxpr_ir_c_printf(&b, "L%zu:\n", i);
        switch (instr->op) {
        case CXPR_OP_PUSH_CONST:
            cxpr_ir_c_printf(&b, "    _cx_s[_cx_sp++] = %.17g;\n", instr->value);
            break;
        case CXPR_OP_PUSH_BOOL:
            cxpr_ir_c_printf(&b, "    _cx_s[_cx_sp++] = %.1f;\n", instr->value != 0.0 ? 1.0 : 0.0);
            break;
        case CXPR_OP_LOAD_VAR:
            if (!cxpr_ir_c_emit_load_arg(&b, args, arg_count, CXPR_C_PROGRAM_ARG_VAR,
                                         instr->name, 0u, err)) goto fail;
            break;
        case CXPR_OP_LOAD_PARAM:
            if (!cxpr_ir_c_emit_load_arg(&b, args, arg_count, CXPR_C_PROGRAM_ARG_PARAM,
                                         instr->name, 0u, err)) goto fail;
            break;
        case CXPR_OP_LOAD_LOCAL:
            if (!cxpr_ir_c_emit_load_local(&b, args, arg_count, instr->index, err)) goto fail;
            break;
        case CXPR_OP_LOAD_VAR_SQUARE:
            if (!cxpr_ir_c_emit_load_arg(&b, args, arg_count, CXPR_C_PROGRAM_ARG_VAR,
                                         instr->name, 0u, err)) goto fail;
            cxpr_ir_c_puts(&b, "    _cx_s[_cx_sp - 1u] = _cx_s[_cx_sp - 1u] * _cx_s[_cx_sp - 1u];\n");
            break;
        case CXPR_OP_LOAD_PARAM_SQUARE:
            if (!cxpr_ir_c_emit_load_arg(&b, args, arg_count, CXPR_C_PROGRAM_ARG_PARAM,
                                         instr->name, 0u, err)) goto fail;
            cxpr_ir_c_puts(&b, "    _cx_s[_cx_sp - 1u] = _cx_s[_cx_sp - 1u] * _cx_s[_cx_sp - 1u];\n");
            break;
        case CXPR_OP_LOAD_LOCAL_SQUARE:
            if (!cxpr_ir_c_emit_load_local_square(&b, args, arg_count, instr->index, err)) goto fail;
            break;
        case CXPR_OP_ADD:
        case CXPR_OP_SUB:
        case CXPR_OP_MUL:
        case CXPR_OP_DIV:
            op = cxpr_ir_c_binary_op(instr->op);
            cxpr_ir_c_printf(&b, "    { double _cx_b = _cx_s[--_cx_sp]; double _cx_a = _cx_s[--_cx_sp]; _cx_s[_cx_sp++] = _cx_a %s _cx_b; }\n", op);
            break;
        case CXPR_OP_CMP_EQ:
        case CXPR_OP_CMP_NEQ:
        case CXPR_OP_CMP_LT:
        case CXPR_OP_CMP_LTE:
        case CXPR_OP_CMP_GT:
        case CXPR_OP_CMP_GTE:
            op = cxpr_ir_c_binary_op(instr->op);
            cxpr_ir_c_printf(&b, "    { double _cx_b = _cx_s[--_cx_sp]; double _cx_a = _cx_s[--_cx_sp]; _cx_s[_cx_sp++] = (_cx_a %s _cx_b) ? 1.0 : 0.0; }\n", op);
            break;
        case CXPR_OP_MOD:
            cxpr_ir_c_puts(&b, "    { double _cx_b = _cx_s[--_cx_sp]; double _cx_a = _cx_s[--_cx_sp]; _cx_s[_cx_sp++] = fmod(_cx_a, _cx_b); }\n");
            break;
        case CXPR_OP_SQUARE:
            cxpr_ir_c_puts(&b, "    _cx_s[_cx_sp - 1u] = _cx_s[_cx_sp - 1u] * _cx_s[_cx_sp - 1u];\n");
            break;
        case CXPR_OP_NOT:
            cxpr_ir_c_puts(&b, "    _cx_s[_cx_sp - 1u] = (_cx_s[_cx_sp - 1u] == 0.0) ? 1.0 : 0.0;\n");
            break;
        case CXPR_OP_NEG:
            cxpr_ir_c_puts(&b, "    _cx_s[_cx_sp - 1u] = -_cx_s[_cx_sp - 1u];\n");
            break;
        case CXPR_OP_SIGN:
            cxpr_ir_c_puts(&b, "    _cx_s[_cx_sp - 1u] = (_cx_s[_cx_sp - 1u] > 0.0) - (_cx_s[_cx_sp - 1u] < 0.0);\n");
            break;
        case CXPR_OP_SQRT:
        case CXPR_OP_ABS:
        case CXPR_OP_FLOOR:
        case CXPR_OP_CEIL:
        case CXPR_OP_ROUND:
            fn = cxpr_ir_c_unary_math(instr->op);
            cxpr_ir_c_printf(&b, "    _cx_s[_cx_sp - 1u] = %s(_cx_s[_cx_sp - 1u]);\n", fn);
            break;
        case CXPR_OP_POW:
            cxpr_ir_c_puts(&b, "    { double _cx_b = _cx_s[--_cx_sp]; double _cx_a = _cx_s[--_cx_sp]; _cx_s[_cx_sp++] = pow(_cx_a, _cx_b); }\n");
            break;
        case CXPR_OP_CLAMP:
            cxpr_ir_c_puts(&b, "    { double _cx_max = _cx_s[--_cx_sp]; double _cx_min = _cx_s[--_cx_sp]; double _cx_v = _cx_s[--_cx_sp]; if (_cx_v < _cx_min) _cx_v = _cx_min; if (_cx_v > _cx_max) _cx_v = _cx_max; _cx_s[_cx_sp++] = _cx_v; }\n");
            break;
        case CXPR_OP_CALL_UNARY:
            {
                const char* name = instr->func ? instr->func->name : NULL;
                if (name && strcmp(name, "abs") == 0) fn = "fabs";
                else if (name && strcmp(name, "sqrt") == 0) fn = "sqrt";
                else if (name && strcmp(name, "floor") == 0) fn = "floor";
                else if (name && strcmp(name, "ceil") == 0) fn = "ceil";
                else if (name && strcmp(name, "round") == 0) fn = "round";
                else fn = NULL;
                if (!fn) {
                    cxpr_ir_c_fail(err, CXPR_ERR_UNKNOWN_FUNCTION,
                                   "Unsupported native unary call in C backend");
                    goto fail;
                }
                cxpr_ir_c_printf(&b, "    _cx_s[_cx_sp - 1u] = %s(_cx_s[_cx_sp - 1u]);\n", fn);
            }
            break;
        case CXPR_OP_CALL_BINARY:
            {
                const char* name = instr->func ? instr->func->name : NULL;
                if (name && strcmp(name, "min") == 0) fn = "fmin";
                else if (name && strcmp(name, "max") == 0) fn = "fmax";
                else if (name && strcmp(name, "pow") == 0) fn = "pow";
                else fn = NULL;
                if (!fn) {
                    cxpr_ir_c_fail(err, CXPR_ERR_UNKNOWN_FUNCTION,
                                   "Unsupported native binary call in C backend");
                    goto fail;
                }
                cxpr_ir_c_printf(&b, "    { double _cx_b = _cx_s[--_cx_sp]; double _cx_a = _cx_s[--_cx_sp]; _cx_s[_cx_sp++] = %s(_cx_a, _cx_b); }\n", fn);
            }
            break;
        case CXPR_OP_CALL_FUNC:
            {
                const char* name = instr->func ? instr->func->name : NULL;
                if (name && strcmp(name, "min") == 0) fn = "fmin";
                else if (name && strcmp(name, "max") == 0) fn = "fmax";
                else fn = NULL;
                if (!fn || instr->index == 0u) {
                    cxpr_ir_c_fail(err, CXPR_ERR_UNKNOWN_FUNCTION,
                                   "Unsupported native variadic call in C backend");
                    goto fail;
                }
                cxpr_ir_c_printf(&b, "    { double _cx_acc = _cx_s[_cx_sp - %zuu];\n", instr->index);
                for (size_t arg = 1u; arg < instr->index; ++arg) {
                    cxpr_ir_c_printf(&b, "      _cx_acc = %s(_cx_acc, _cx_s[_cx_sp - %zuu]);\n",
                                     fn, instr->index - arg);
                }
                cxpr_ir_c_printf(&b, "      _cx_sp -= %zuu; _cx_s[_cx_sp++] = _cx_acc; }\n",
                                 instr->index);
            }
            break;
        case CXPR_OP_JUMP:
            cxpr_ir_c_printf(&b, "    goto L%zu;\n", instr->index);
            break;
        case CXPR_OP_JUMP_IF_FALSE:
            cxpr_ir_c_printf(&b, "    if (_cx_s[--_cx_sp] == 0.0) goto L%zu;\n", instr->index);
            break;
        case CXPR_OP_JUMP_IF_TRUE:
            cxpr_ir_c_printf(&b, "    if (_cx_s[--_cx_sp] != 0.0) goto L%zu;\n", instr->index);
            break;
        case CXPR_OP_STORE_LOCAL:
            cxpr_ir_c_printf(&b, "    _cx_l[%zu] = _cx_s[--_cx_sp];\n", instr->index);
            break;
        case CXPR_OP_CALL_AST:
            if (instr->ast && cxpr_expr_ast_kind_of(instr->ast) == CXPR_NODE_FUNCTION_CALL) {
                static char message[160];
                const char* name = cxpr_expr_ast_call_name(instr->ast);
                (void)snprintf(
                    message,
                    sizeof(message),
                    "Unsupported AST call for C backend: %s",
                    name ? name : "");
                cxpr_ir_c_fail(err, CXPR_ERR_SYNTAX, message);
            } else {
                cxpr_ir_c_fail(err, CXPR_ERR_SYNTAX, "Unsupported AST node for C backend");
            }
            goto fail;
        case CXPR_OP_RETURN:
            cxpr_ir_c_puts(&b, "    return _cx_s[--_cx_sp];\n");
            break;
        default:
            {
                static char message[96];
                (void)snprintf(
                    message,
                    sizeof(message),
                    "Unsupported IR opcode for C backend: %u",
                    (unsigned)instr->op);
                cxpr_ir_c_fail(err, CXPR_ERR_SYNTAX, message);
            }
            goto fail;
        }
        if (b.oom) {
            cxpr_ir_c_fail(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
            goto fail;
        }
    }
    cxpr_ir_c_puts(&b, "}\n");
    if (b.oom) {
        cxpr_ir_c_fail(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        goto fail;
    }
    if (err) err->code = CXPR_OK;
    return b.data;

fail:
    free(b.data);
    return NULL;
}
