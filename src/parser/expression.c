/**
 * @file expression.c
 * @brief Recursive descent parsing for expression operators above primary nodes.
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>

static cxpr_ast* cxpr_parse_pipe(cxpr_parser* p);
static cxpr_ast* cxpr_parse_ternary(cxpr_parser* p);
static cxpr_ast* cxpr_parse_or(cxpr_parser* p);
static cxpr_ast* cxpr_parse_and(cxpr_parser* p);
static cxpr_ast* cxpr_parse_not(cxpr_parser* p);
static cxpr_ast* cxpr_parse_equality(cxpr_parser* p);
static cxpr_ast* cxpr_parse_relational(cxpr_parser* p);
static cxpr_ast* cxpr_parse_arithmetic(cxpr_parser* p);
static cxpr_ast* cxpr_parse_term(cxpr_parser* p);
static cxpr_ast* cxpr_parse_unary(cxpr_parser* p);
static cxpr_ast* cxpr_parse_power(cxpr_parser* p);

static cxpr_ast* cxpr_parse_set_membership(cxpr_parser* p, cxpr_ast* left, bool negated);

static bool cxpr_parser_is_relational_token(cxpr_token_type type) {
    return type == CXPR_TOK_LT || type == CXPR_TOK_GT ||
           type == CXPR_TOK_LTE || type == CXPR_TOK_GTE;
}

cxpr_ast* cxpr_parse_expression(cxpr_parser* p) { return cxpr_parse_pipe(p); }

static cxpr_ast* cxpr_parse_pipe(cxpr_parser* p) {
    cxpr_ast* left = cxpr_parse_ternary(p);
    if (!left || p->had_error) return left;
    while (cxpr_parser_check(p, CXPR_TOK_PIPE)) {
        cxpr_ast* stage = NULL;
        cxpr_parser_advance(p);
        stage = cxpr_parse_ternary(p);
        if (!stage || p->had_error) {
            cxpr_ast_free(left);
            cxpr_ast_free(stage);
            return NULL;
        }
        left = cxpr_parser_pipe_inject_argument(p, stage, left);
        if (!left || p->had_error) return NULL;
    }
    return left;
}

static cxpr_ast* cxpr_parse_ternary(cxpr_parser* p) {
    cxpr_ast* condition = cxpr_parse_or(p);
    if (!condition || p->had_error) return condition;
    if (cxpr_parser_match(p, CXPR_TOK_QUESTION)) {
        cxpr_ast* true_branch = cxpr_parse_expression(p);
        if (!true_branch || p->had_error) { cxpr_ast_free(condition); cxpr_ast_free(true_branch); return NULL; }
        if (!cxpr_parser_expect(p, CXPR_TOK_COLON, "Expected ':' in ternary expression")) {
            cxpr_ast_free(condition);
            cxpr_ast_free(true_branch);
            return NULL;
        }
        cxpr_ast* false_branch = cxpr_parse_expression(p);
        if (!false_branch || p->had_error) {
            cxpr_ast_free(condition);
            cxpr_ast_free(true_branch);
            cxpr_ast_free(false_branch);
            return NULL;
        }
        return cxpr_ast_new_ternary(condition, true_branch, false_branch);
    }
    return condition;
}

static cxpr_ast* cxpr_parse_or(cxpr_parser* p) {
    cxpr_ast* left = cxpr_parse_and(p);
    if (!left || p->had_error) return left;
    while (cxpr_parser_check(p, CXPR_TOK_OR)) {
        cxpr_parser_advance(p);
        cxpr_ast* right = cxpr_parse_and(p);
        if (!right || p->had_error) { cxpr_ast_free(left); cxpr_ast_free(right); return NULL; }
        left = cxpr_ast_new_binary_op(CXPR_TOK_OR, left, right);
    }
    return left;
}

static cxpr_ast* cxpr_parse_and(cxpr_parser* p) {
    cxpr_ast* left = cxpr_parse_not(p);
    if (!left || p->had_error) return left;
    while (cxpr_parser_check(p, CXPR_TOK_AND)) {
        cxpr_parser_advance(p);
        cxpr_ast* right = cxpr_parse_not(p);
        if (!right || p->had_error) { cxpr_ast_free(left); cxpr_ast_free(right); return NULL; }
        left = cxpr_ast_new_binary_op(CXPR_TOK_AND, left, right);
    }
    return left;
}

static cxpr_ast* cxpr_parse_not(cxpr_parser* p) {
    if (cxpr_parser_check(p, CXPR_TOK_NOT)) {
        cxpr_parser_advance(p);
        cxpr_ast* operand = cxpr_parse_not(p);
        if (!operand || p->had_error) { cxpr_ast_free(operand); return NULL; }
        return cxpr_ast_new_unary_op(CXPR_TOK_NOT, operand);
    }
    return cxpr_parse_equality(p);
}

static cxpr_ast* cxpr_parse_equality(cxpr_parser* p) {
    cxpr_ast* left = cxpr_parse_relational(p);
    if (!left || p->had_error) return left;
    if (cxpr_parser_check(p, CXPR_TOK_EQ) || cxpr_parser_check(p, CXPR_TOK_NEQ)) {
        int op = p->current.type;
        cxpr_parser_advance(p);
        cxpr_ast* right = cxpr_parse_relational(p);
        if (!right || p->had_error) { cxpr_ast_free(left); cxpr_ast_free(right); return NULL; }
        return cxpr_ast_new_binary_op(op, left, right);
    }
    return left;
}

static cxpr_ast* cxpr_parse_relational(cxpr_parser* p) {
    cxpr_ast* left = cxpr_parse_arithmetic(p);
    if (!left || p->had_error) return left;
    if (cxpr_parser_is_relational_token(p->current.type)) {
        cxpr_ast* cmp;
        int op = p->current.type;
        cxpr_parser_advance(p);
        cxpr_ast* right = cxpr_parse_arithmetic(p);
        if (!right || p->had_error) { cxpr_ast_free(left); cxpr_ast_free(right); return NULL; }
        cmp = cxpr_ast_new_binary_op(op, left, right);
        if (!cmp) {
            cxpr_ast_free(left);
            cxpr_ast_free(right);
            return NULL;
        }
        left = cmp;
        while (cxpr_parser_is_relational_token(p->current.type)) {
            cxpr_ast* previous_right = cxpr_parser_clone_ast(right);
            cxpr_ast* next_right;
            cxpr_ast* next_cmp;
            cxpr_ast* joined;
            if (!previous_right) {
                p->had_error = true;
                p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
                p->last_error.message = "Out of memory";
                p->last_error.position = p->current.position;
                p->last_error.line = p->current.line;
                p->last_error.column = p->current.column;
                cxpr_ast_free(left);
                return NULL;
            }
            op = p->current.type;
            cxpr_parser_advance(p);
            next_right = cxpr_parse_arithmetic(p);
            if (!next_right || p->had_error) {
                cxpr_ast_free(left);
                cxpr_ast_free(previous_right);
                cxpr_ast_free(next_right);
                return NULL;
            }
            next_cmp = cxpr_ast_new_binary_op(op, previous_right, next_right);
            if (!next_cmp) {
                cxpr_ast_free(left);
                cxpr_ast_free(previous_right);
                cxpr_ast_free(next_right);
                return NULL;
            }
            joined = cxpr_ast_new_binary_op(CXPR_TOK_AND, left, next_cmp);
            if (!joined) {
                cxpr_ast_free(left);
                cxpr_ast_free(next_cmp);
                return NULL;
            }
            left = joined;
            right = next_right;
        }
        return left;
    }
    if ((cxpr_parser_check(p, CXPR_TOK_NOT) &&
         cxpr_parser_peek_next(p).type == CXPR_TOK_IN) ||
        cxpr_parser_check(p, CXPR_TOK_IN)) {
        bool negated = false;
        if (cxpr_parser_check(p, CXPR_TOK_NOT)) {
            negated = true;
            cxpr_parser_advance(p);
        }
        cxpr_parser_advance(p); /* consume 'in' */
        return cxpr_parse_set_membership(p, left, negated);
    }
    return left;
}

/* `x in [a, b, c]` -> `contains(x, [a, b, c])` (set membership).
 * `x not in [...]` negates the contains call. Requires at least one element.
 * `left` is consumed. */
static cxpr_ast* cxpr_parse_set_membership(cxpr_parser* p, cxpr_ast* left, bool negated) {
    cxpr_ast* array = NULL;
    cxpr_ast** args = NULL;
    cxpr_ast* call = NULL;

    if (!cxpr_parser_check(p, CXPR_TOK_LBRACKET)) {
        cxpr_parser_set_error(p, "Expected '[' to start set after 'in'");
        cxpr_ast_free(left);
        return NULL;
    }
    if (cxpr_parser_peek_next(p).type == CXPR_TOK_RBRACKET) {
        cxpr_parser_set_error(p, "Set membership requires at least one element");
        cxpr_ast_free(left);
        return NULL;
    }

    array = cxpr_parse_arithmetic(p);
    if (!array || p->had_error) goto fail;

    args = (cxpr_ast**)calloc(2u, sizeof(cxpr_ast*));
    if (!args) {
        p->had_error = true;
        p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
        p->last_error.message = "Out of memory";
        p->last_error.position = p->current.position;
        p->last_error.line = p->current.line;
        p->last_error.column = p->current.column;
        goto fail;
    }
    args[0] = left;
    args[1] = array;
    left = NULL;
    array = NULL;

    call = cxpr_ast_new_function_call("contains", args, 2u);
    if (!call) goto fail;
    args = NULL;

    if (negated) {
        cxpr_ast* out = cxpr_ast_new_unary_op(CXPR_TOK_NOT, call);
        if (!out) {
            cxpr_ast_free(call);
            return NULL;
        }
        return out;
    }
    return call;

fail:
    cxpr_ast_free(left);
    cxpr_ast_free(array);
    if (args) {
        cxpr_ast_free(args[0]);
        cxpr_ast_free(args[1]);
        free(args);
    }
    cxpr_ast_free(call);
    return NULL;
}

static cxpr_ast* cxpr_parse_arithmetic(cxpr_parser* p) {
    cxpr_ast* left = cxpr_parse_term(p);
    if (!left || p->had_error) return left;
    while (cxpr_parser_check(p, CXPR_TOK_PLUS) || cxpr_parser_check(p, CXPR_TOK_MINUS)) {
        int op = p->current.type;
        cxpr_parser_advance(p);
        cxpr_ast* right = cxpr_parse_term(p);
        if (!right || p->had_error) { cxpr_ast_free(left); cxpr_ast_free(right); return NULL; }
        left = cxpr_ast_new_binary_op(op, left, right);
    }
    return left;
}

static cxpr_ast* cxpr_parse_term(cxpr_parser* p) {
    cxpr_ast* left = cxpr_parse_unary(p);
    if (!left || p->had_error) return left;
    while (cxpr_parser_check(p, CXPR_TOK_STAR) || cxpr_parser_check(p, CXPR_TOK_SLASH) ||
           cxpr_parser_check(p, CXPR_TOK_PERCENT)) {
        int op = p->current.type;
        cxpr_parser_advance(p);
        cxpr_ast* right = cxpr_parse_unary(p);
        if (!right || p->had_error) { cxpr_ast_free(left); cxpr_ast_free(right); return NULL; }
        left = cxpr_ast_new_binary_op(op, left, right);
    }
    return left;
}

static cxpr_ast* cxpr_parse_unary(cxpr_parser* p) {
    if (cxpr_parser_check(p, CXPR_TOK_MINUS)) {
        cxpr_parser_advance(p);
        cxpr_ast* operand = cxpr_parse_unary(p);
        if (!operand || p->had_error) { cxpr_ast_free(operand); return NULL; }
        return cxpr_ast_new_unary_op(CXPR_TOK_MINUS, operand);
    }
    if (cxpr_parser_check(p, CXPR_TOK_PLUS)) {
        cxpr_parser_advance(p);
        return cxpr_parse_unary(p);
    }
    return cxpr_parse_power(p);
}

static cxpr_ast* cxpr_parse_power(cxpr_parser* p) {
    cxpr_ast* left = cxpr_parse_primary(p);
    if (!left || p->had_error) return left;
    if (cxpr_parser_check(p, CXPR_TOK_POWER)) {
        cxpr_parser_advance(p);
        cxpr_ast* right = cxpr_parse_unary(p);
        if (!right || p->had_error) { cxpr_ast_free(left); cxpr_ast_free(right); return NULL; }
        return cxpr_ast_new_binary_op(CXPR_TOK_POWER, left, right);
    }
    return left;
}
