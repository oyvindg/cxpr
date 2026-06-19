/**
 * @file printer.c
 * @brief AST-to-source rendering.
 */

#include "internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* buf;
    size_t len;
    size_t cap;
} cxpr_ast_printer;

typedef enum {
    CXPR_ASSOC_NONE = 0,
    CXPR_ASSOC_LEFT,
    CXPR_ASSOC_RIGHT
} cxpr_assoc;

static int cxpr_printer_reserve(cxpr_ast_printer* p, size_t extra) {
    size_t needed;
    size_t cap;
    char* grown;

    if (!p) return 0;
    if (extra > (size_t)-1 - p->len - 1u) return 0;
    needed = p->len + extra + 1u;
    if (needed <= p->cap) return 1;
    cap = p->cap ? p->cap : 64u;
    while (cap < needed) {
        if (cap > (size_t)-1 / 2u) {
            cap = needed;
            break;
        }
        cap *= 2u;
    }
    grown = (char*)realloc(p->buf, cap);
    if (!grown) return 0;
    p->buf = grown;
    p->cap = cap;
    return 1;
}

static int cxpr_printer_append_n(cxpr_ast_printer* p, const char* text, size_t len) {
    if (!text) return 0;
    if (!cxpr_printer_reserve(p, len)) return 0;
    memcpy(p->buf + p->len, text, len);
    p->len += len;
    p->buf[p->len] = '\0';
    return 1;
}

static int cxpr_printer_append(cxpr_ast_printer* p, const char* text) {
    return cxpr_printer_append_n(p, text, strlen(text));
}

static int cxpr_printer_append_char(cxpr_ast_printer* p, char ch) {
    if (!cxpr_printer_reserve(p, 1u)) return 0;
    p->buf[p->len++] = ch;
    p->buf[p->len] = '\0';
    return 1;
}

static int cxpr_op_precedence(int op) {
    switch (op) {
        case CXPR_TOK_OR: return 2;
        case CXPR_TOK_AND: return 3;
        case CXPR_TOK_EQ:
        case CXPR_TOK_NEQ: return 5;
        case CXPR_TOK_LT:
        case CXPR_TOK_GT:
        case CXPR_TOK_LTE:
        case CXPR_TOK_GTE: return 6;
        case CXPR_TOK_PLUS:
        case CXPR_TOK_MINUS: return 7;
        case CXPR_TOK_STAR:
        case CXPR_TOK_SLASH:
        case CXPR_TOK_PERCENT: return 8;
        case CXPR_TOK_POWER: return 10;
        default: return 0;
    }
}

static cxpr_assoc cxpr_op_assoc(int op) {
    return op == CXPR_TOK_POWER ? CXPR_ASSOC_RIGHT : CXPR_ASSOC_LEFT;
}

static int cxpr_ast_precedence(const cxpr_ast* ast) {
    if (!ast) return 0;
    switch (ast->type) {
        case CXPR_NODE_TERNARY: return 1;
        case CXPR_NODE_BINARY_OP: return cxpr_op_precedence(ast->data.binary_op.op);
        case CXPR_NODE_UNARY_OP: return 9;
        default: return 11;
    }
}

static const char* cxpr_binary_op_text(int op) {
    switch (op) {
        case CXPR_TOK_PLUS: return "+";
        case CXPR_TOK_MINUS: return "-";
        case CXPR_TOK_STAR: return "*";
        case CXPR_TOK_SLASH: return "/";
        case CXPR_TOK_PERCENT: return "%";
        case CXPR_TOK_POWER: return "^";
        case CXPR_TOK_EQ: return "==";
        case CXPR_TOK_NEQ: return "!=";
        case CXPR_TOK_LT: return "<";
        case CXPR_TOK_GT: return ">";
        case CXPR_TOK_LTE: return "<=";
        case CXPR_TOK_GTE: return ">=";
        case CXPR_TOK_AND: return "and";
        case CXPR_TOK_OR: return "or";
        default: return NULL;
    }
}

static int cxpr_print_node(cxpr_ast_printer* p, const cxpr_ast* ast, int parent_prec);

static int cxpr_print_with_parens(cxpr_ast_printer* p, const cxpr_ast* ast, int parens) {
    if (parens && !cxpr_printer_append_char(p, '(')) return 0;
    if (!cxpr_print_node(p, ast, 0)) return 0;
    if (parens && !cxpr_printer_append_char(p, ')')) return 0;
    return 1;
}

static int cxpr_binary_child_needs_parens(const cxpr_ast* child,
                                          int parent_op,
                                          int is_right_child) {
    int parent_prec;
    int child_prec;

    if (!child) return 0;
    if (child->type == CXPR_NODE_TERNARY) return 1;
    parent_prec = cxpr_op_precedence(parent_op);
    child_prec = cxpr_ast_precedence(child);
    if (child_prec < parent_prec) return 1;
    if (child->type != CXPR_NODE_BINARY_OP || child_prec != parent_prec) return 0;

    if (cxpr_op_assoc(parent_op) == CXPR_ASSOC_RIGHT) return !is_right_child;
    return is_right_child;
}

static int cxpr_print_number(cxpr_ast_printer* p, double value) {
    char buf[64];
    char* end = NULL;

    if (isnan(value)) return cxpr_printer_append(p, "nan()");
    if (isinf(value)) return cxpr_printer_append(p, value < 0.0 ? "-inf()" : "inf()");
    for (int precision = 15; precision <= 17; ++precision) {
        snprintf(buf, sizeof(buf), "%.*g", precision, value);
        end = NULL;
        if (strtod(buf, &end) == value && end && *end == '\0') {
            return cxpr_printer_append(p, buf);
        }
    }
    snprintf(buf, sizeof(buf), "%.17g", value);
    return cxpr_printer_append(p, buf);
}

static int cxpr_print_string(cxpr_ast_printer* p, const char* value) {
    const unsigned char* cursor = (const unsigned char*)(value ? value : "");
    unsigned int backslash_run = 0u;

    if (!cxpr_printer_append_char(p, '"')) return 0;
    while (*cursor) {
        if (*cursor == '"' && (backslash_run % 2u) == 0u) {
            if (!cxpr_printer_append_char(p, '\\')) return 0;
        }
        if (!cxpr_printer_append_char(p, (char)*cursor)) return 0;
        if (*cursor == '\\') {
            ++backslash_run;
        } else {
            backslash_run = 0u;
        }
        ++cursor;
    }
    return cxpr_printer_append_char(p, '"');
}

static int cxpr_print_call_args(cxpr_ast_printer* p,
                                cxpr_ast* const* args,
                                char* const* arg_names,
                                size_t argc) {
    for (size_t i = 0u; i < argc; ++i) {
        if (i > 0u && !cxpr_printer_append(p, ", ")) return 0;
        if (arg_names && arg_names[i]) {
            if (!cxpr_printer_append(p, arg_names[i]) ||
                !cxpr_printer_append_char(p, '=')) {
                return 0;
            }
        }
        if (!cxpr_print_node(p, args[i], 0)) return 0;
    }
    return 1;
}

static int cxpr_print_binary(cxpr_ast_printer* p, const cxpr_ast* ast, int parent_prec) {
    int op = ast->data.binary_op.op;
    int prec = cxpr_op_precedence(op);
    const char* op_text = cxpr_binary_op_text(op);
    int parens = prec < parent_prec;
    int left_parens;
    int right_parens;

    if (!op_text) return 0;
    left_parens = cxpr_binary_child_needs_parens(ast->data.binary_op.left, op, 0);
    right_parens = cxpr_binary_child_needs_parens(ast->data.binary_op.right, op, 1);

    if (parens && !cxpr_printer_append_char(p, '(')) return 0;
    if (!cxpr_print_with_parens(p, ast->data.binary_op.left, left_parens)) return 0;
    if (!cxpr_printer_append_char(p, ' ') ||
        !cxpr_printer_append(p, op_text) ||
        !cxpr_printer_append_char(p, ' ')) {
        return 0;
    }
    if (!cxpr_print_with_parens(p, ast->data.binary_op.right, right_parens)) return 0;
    if (parens && !cxpr_printer_append_char(p, ')')) return 0;
    return 1;
}

static int cxpr_print_unary(cxpr_ast_printer* p, const cxpr_ast* ast, int parent_prec) {
    int prec = cxpr_ast_precedence(ast);
    int parens = prec < parent_prec;
    const cxpr_ast* operand = ast->data.unary_op.operand;
    int operand_parens = operand && cxpr_ast_precedence(operand) < prec;

    if (parens && !cxpr_printer_append_char(p, '(')) return 0;
    if (ast->data.unary_op.op == CXPR_TOK_NOT) {
        if (!cxpr_printer_append(p, "not ")) return 0;
    } else if (ast->data.unary_op.op == CXPR_TOK_MINUS) {
        if (!cxpr_printer_append_char(p, '-')) return 0;
    } else {
        return 0;
    }
    if (!cxpr_print_with_parens(p, operand, operand_parens)) return 0;
    if (parens && !cxpr_printer_append_char(p, ')')) return 0;
    return 1;
}

static int cxpr_print_ternary(cxpr_ast_printer* p, const cxpr_ast* ast, int parent_prec) {
    int parens = 1 < parent_prec;

    if (parens && !cxpr_printer_append_char(p, '(')) return 0;
    if (!cxpr_print_with_parens(
            p,
            ast->data.ternary.condition,
            ast->data.ternary.condition &&
                ast->data.ternary.condition->type == CXPR_NODE_TERNARY)) {
        return 0;
    }
    if (!cxpr_printer_append(p, " ? ")) return 0;
    if (!cxpr_print_with_parens(
            p,
            ast->data.ternary.true_branch,
            ast->data.ternary.true_branch &&
                ast->data.ternary.true_branch->type == CXPR_NODE_TERNARY)) {
        return 0;
    }
    if (!cxpr_printer_append(p, " : ")) return 0;
    if (!cxpr_print_with_parens(
            p,
            ast->data.ternary.false_branch,
            ast->data.ternary.false_branch &&
                ast->data.ternary.false_branch->type == CXPR_NODE_TERNARY)) {
        return 0;
    }
    if (parens && !cxpr_printer_append_char(p, ')')) return 0;
    return 1;
}

static int cxpr_print_node(cxpr_ast_printer* p, const cxpr_ast* ast, int parent_prec) {
    if (!p || !ast) return 0;

    switch (ast->type) {
        case CXPR_NODE_NUMBER:
            return cxpr_print_number(p, ast->data.number.value);
        case CXPR_NODE_BOOL:
            return cxpr_printer_append(p, ast->data.boolean.value ? "true" : "false");
        case CXPR_NODE_STRING:
            return cxpr_print_string(p, ast->data.string.value);
        case CXPR_NODE_IDENTIFIER:
            return cxpr_printer_append(p, ast->data.identifier.name);
        case CXPR_NODE_VARIABLE:
            return cxpr_printer_append_char(p, '$') &&
                   cxpr_printer_append(p, ast->data.variable.name);
        case CXPR_NODE_FIELD_ACCESS:
            return cxpr_printer_append(p, ast->data.field_access.object) &&
                   cxpr_printer_append_char(p, '.') &&
                   cxpr_printer_append(p, ast->data.field_access.field);
        case CXPR_NODE_CHAIN_ACCESS:
            for (size_t i = 0u; i < ast->data.chain_access.depth; ++i) {
                if (i > 0u && !cxpr_printer_append_char(p, '.')) return 0;
                if (!cxpr_printer_append(p, ast->data.chain_access.path[i])) return 0;
            }
            return 1;
        case CXPR_NODE_PRODUCER_ACCESS:
            if (!cxpr_printer_append(p, ast->data.producer_access.name) ||
                !cxpr_printer_append_char(p, '(') ||
                !cxpr_print_call_args(p,
                                      ast->data.producer_access.args,
                                      ast->data.producer_access.arg_names,
                                      ast->data.producer_access.argc) ||
                !cxpr_printer_append(p, ").") ||
                !cxpr_printer_append(p, ast->data.producer_access.field)) {
                return 0;
            }
            return 1;
        case CXPR_NODE_BINARY_OP:
            return cxpr_print_binary(p, ast, parent_prec);
        case CXPR_NODE_UNARY_OP:
            return cxpr_print_unary(p, ast, parent_prec);
        case CXPR_NODE_FUNCTION_CALL:
            return cxpr_printer_append(p, ast->data.function_call.name) &&
                   cxpr_printer_append_char(p, '(') &&
                   cxpr_print_call_args(p,
                                        ast->data.function_call.args,
                                        ast->data.function_call.arg_names,
                                        ast->data.function_call.argc) &&
                   cxpr_printer_append_char(p, ')');
        case CXPR_NODE_LOOKBACK:
            return cxpr_print_node(p, ast->data.lookback.target, cxpr_ast_precedence(ast)) &&
                   cxpr_printer_append_char(p, '[') &&
                   cxpr_print_node(p, ast->data.lookback.index, 0) &&
                   cxpr_printer_append_char(p, ']');
        case CXPR_NODE_TERNARY:
            return cxpr_print_ternary(p, ast, parent_prec);
        default:
            return 0;
    }
}

char* cxpr_ast_to_string(const cxpr_ast* ast) {
    cxpr_ast_printer p = {0};

    if (!ast) return NULL;
    if (!cxpr_print_node(&p, ast, 0)) {
        free(p.buf);
        return NULL;
    }
    return p.buf;
}

void cxpr_ast_dump(const cxpr_ast* ast, FILE* out) {
    char* text;

    if (!out) return;
    text = cxpr_ast_to_string(ast);
    if (!text) return;
    fputs(text, out);
    free(text);
}
