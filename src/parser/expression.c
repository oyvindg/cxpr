/**
 * @file expression.c
 * @brief Recursive descent parsing for expression operators above primary nodes.
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>

static cxpr_expr_ast* cxpr_parse_pipe(cxpr_expr_parser* p);
static cxpr_expr_ast* cxpr_parse_ternary(cxpr_expr_parser* p);
static cxpr_expr_ast* cxpr_parse_or(cxpr_expr_parser* p);
static cxpr_expr_ast* cxpr_parse_and(cxpr_expr_parser* p);
static cxpr_expr_ast* cxpr_parse_not(cxpr_expr_parser* p);
static cxpr_expr_ast* cxpr_parse_equality(cxpr_expr_parser* p);
static cxpr_expr_ast* cxpr_parse_relational(cxpr_expr_parser* p);
static cxpr_expr_ast* cxpr_parse_arithmetic(cxpr_expr_parser* p);
static cxpr_expr_ast* cxpr_parse_term(cxpr_expr_parser* p);
static cxpr_expr_ast* cxpr_parse_unary(cxpr_expr_parser* p);
static cxpr_expr_ast* cxpr_parse_power(cxpr_expr_parser* p);

static cxpr_expr_ast* cxpr_parse_set_membership(cxpr_expr_parser* p, cxpr_expr_ast* left, bool negated);

static bool cxpr_expr_parser_is_relational_token(cxpr_token_type type) {
    return type == CXPR_TOK_LT || type == CXPR_TOK_GT ||
           type == CXPR_TOK_LTE || type == CXPR_TOK_GTE;
}

static bool cxpr_expr_parser_is_subject_node(const cxpr_expr_ast* ast) {
    if (!ast) return false;
    switch (cxpr_expr_ast_kind_of(ast)) {
        case CXPR_NODE_IDENTIFIER:
        case CXPR_NODE_VARIABLE:
        case CXPR_NODE_FIELD_ACCESS:
        case CXPR_NODE_CHAIN_ACCESS:
        case CXPR_NODE_LOOKBACK:
        case CXPR_NODE_PRODUCER_ACCESS:
            return true;
        default:
            return false;
    }
}

static bool cxpr_expr_parser_is_threshold_node(const cxpr_expr_ast* ast) {
    if (!ast) return false;
    switch (cxpr_expr_ast_kind_of(ast)) {
        case CXPR_NODE_NUMBER:
        case CXPR_NODE_VARIABLE:
            return true;
        default:
            return false;
    }
}

static bool cxpr_expr_parser_relational_ops_cross_bounds(int first_op, int next_op) {
    return ((first_op == CXPR_TOK_GT || first_op == CXPR_TOK_GTE) &&
            (next_op == CXPR_TOK_LT || next_op == CXPR_TOK_LTE)) ||
           ((first_op == CXPR_TOK_LT || first_op == CXPR_TOK_LTE) &&
            (next_op == CXPR_TOK_GT || next_op == CXPR_TOK_GTE));
}

cxpr_expr_ast* cxpr_parse_expression(cxpr_expr_parser* p) { return cxpr_parse_pipe(p); }

static cxpr_expr_ast* cxpr_parse_pipe(cxpr_expr_parser* p) {
    cxpr_expr_ast* left = cxpr_parse_ternary(p);
    if (!left || p->had_error) return left;
    while (cxpr_expr_parser_check(p, CXPR_TOK_PIPE)) {
        cxpr_expr_ast* stage = NULL;
        cxpr_expr_parser_advance(p);
        stage = cxpr_parse_ternary(p);
        if (!stage || p->had_error) {
            cxpr_expr_ast_free(left);
            cxpr_expr_ast_free(stage);
            return NULL;
        }
        left = cxpr_expr_parser_pipe_inject_argument(p, stage, left);
        if (!left || p->had_error) return NULL;
    }
    return left;
}

static cxpr_expr_ast* cxpr_parse_ternary(cxpr_expr_parser* p) {
    cxpr_expr_ast* condition = cxpr_parse_or(p);
    if (!condition || p->had_error) return condition;
    if (cxpr_expr_parser_match(p, CXPR_TOK_QUESTION)) {
        cxpr_expr_ast* true_branch = cxpr_parse_expression(p);
        if (!true_branch || p->had_error) { cxpr_expr_ast_free(condition); cxpr_expr_ast_free(true_branch); return NULL; }
        if (!cxpr_expr_parser_expect(p, CXPR_TOK_COLON, "Expected ':' in ternary expression")) {
            cxpr_expr_ast_free(condition);
            cxpr_expr_ast_free(true_branch);
            return NULL;
        }
        cxpr_expr_ast* false_branch = cxpr_parse_expression(p);
        if (!false_branch || p->had_error) {
            cxpr_expr_ast_free(condition);
            cxpr_expr_ast_free(true_branch);
            cxpr_expr_ast_free(false_branch);
            return NULL;
        }
        return cxpr_expr_ast_ternary_new(condition, true_branch, false_branch);
    }
    return condition;
}

static cxpr_expr_ast* cxpr_parse_or(cxpr_expr_parser* p) {
    cxpr_expr_ast* left = cxpr_parse_and(p);
    if (!left || p->had_error) return left;
    while (cxpr_expr_parser_check(p, CXPR_TOK_OR)) {
        cxpr_expr_parser_advance(p);
        cxpr_expr_ast* right = cxpr_parse_and(p);
        if (!right || p->had_error) { cxpr_expr_ast_free(left); cxpr_expr_ast_free(right); return NULL; }
        left = cxpr_expr_ast_binary_new(CXPR_TOK_OR, left, right);
    }
    return left;
}

static cxpr_expr_ast* cxpr_parse_and(cxpr_expr_parser* p) {
    cxpr_expr_ast* left = cxpr_parse_not(p);
    if (!left || p->had_error) return left;
    while (cxpr_expr_parser_check(p, CXPR_TOK_AND)) {
        cxpr_expr_parser_advance(p);
        cxpr_expr_ast* right = cxpr_parse_not(p);
        if (!right || p->had_error) { cxpr_expr_ast_free(left); cxpr_expr_ast_free(right); return NULL; }
        left = cxpr_expr_ast_binary_new(CXPR_TOK_AND, left, right);
    }
    return left;
}

static cxpr_expr_ast* cxpr_parse_not(cxpr_expr_parser* p) {
    if (cxpr_expr_parser_check(p, CXPR_TOK_NOT)) {
        cxpr_expr_parser_advance(p);
        cxpr_expr_ast* operand = cxpr_parse_not(p);
        if (!operand || p->had_error) { cxpr_expr_ast_free(operand); return NULL; }
        return cxpr_expr_ast_unary_new(CXPR_TOK_NOT, operand);
    }
    return cxpr_parse_equality(p);
}

static cxpr_expr_ast* cxpr_parse_equality(cxpr_expr_parser* p) {
    cxpr_expr_ast* left = cxpr_parse_relational(p);
    if (!left || p->had_error) return left;
    if (cxpr_expr_parser_check(p, CXPR_TOK_EQ) || cxpr_expr_parser_check(p, CXPR_TOK_NEQ)) {
        int op = p->current.type;
        cxpr_expr_parser_advance(p);
        cxpr_expr_ast* right = cxpr_parse_relational(p);
        if (!right || p->had_error) { cxpr_expr_ast_free(left); cxpr_expr_ast_free(right); return NULL; }
        return cxpr_expr_ast_binary_new(op, left, right);
    }
    return left;
}

static cxpr_expr_ast* cxpr_parse_relational(cxpr_expr_parser* p) {
    cxpr_expr_ast* left = cxpr_parse_arithmetic(p);
    if (!left || p->had_error) return left;
    if (cxpr_expr_parser_is_relational_token(p->current.type)) {
        cxpr_expr_ast* cmp;
        cxpr_expr_ast* chain_subject = NULL;
        int op = p->current.type;
        int first_op = op;
        bool first_left_is_subject = cxpr_expr_parser_is_subject_node(left);
        cxpr_expr_parser_advance(p);
        cxpr_expr_ast* right = cxpr_parse_arithmetic(p);
        if (!right || p->had_error) { cxpr_expr_ast_free(left); cxpr_expr_ast_free(right); return NULL; }
        if (first_left_is_subject && cxpr_expr_parser_is_threshold_node(right)) {
            chain_subject = cxpr_expr_parser_clone_ast(left);
            if (!chain_subject) {
                p->had_error = true;
                p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
                p->last_error.message = "Out of memory";
                p->last_error.position = p->current.position;
                p->last_error.line = p->current.line;
                p->last_error.column = p->current.column;
                cxpr_expr_ast_free(left);
                cxpr_expr_ast_free(right);
                return NULL;
            }
        }
        cmp = cxpr_expr_ast_binary_new(op, left, right);
        if (!cmp) {
            cxpr_expr_ast_free(chain_subject);
            cxpr_expr_ast_free(left);
            cxpr_expr_ast_free(right);
            return NULL;
        }
        left = cmp;
        while (cxpr_expr_parser_is_relational_token(p->current.type)) {
            int next_op = p->current.type;
            bool compare_subject = chain_subject &&
                                   cxpr_expr_parser_relational_ops_cross_bounds(first_op, next_op);
            cxpr_expr_ast* next_left = cxpr_expr_parser_clone_ast(compare_subject ? chain_subject : right);
            cxpr_expr_ast* next_right;
            cxpr_expr_ast* next_cmp;
            cxpr_expr_ast* joined;
            if (!next_left) {
                p->had_error = true;
                p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
                p->last_error.message = "Out of memory";
                p->last_error.position = p->current.position;
                p->last_error.line = p->current.line;
                p->last_error.column = p->current.column;
                cxpr_expr_ast_free(left);
                cxpr_expr_ast_free(chain_subject);
                return NULL;
            }
            op = next_op;
            cxpr_expr_parser_advance(p);
            next_right = cxpr_parse_arithmetic(p);
            if (!next_right || p->had_error) {
                cxpr_expr_ast_free(left);
                cxpr_expr_ast_free(next_left);
                cxpr_expr_ast_free(next_right);
                cxpr_expr_ast_free(chain_subject);
                return NULL;
            }
            next_cmp = cxpr_expr_ast_binary_new(op, next_left, next_right);
            if (!next_cmp) {
                cxpr_expr_ast_free(left);
                cxpr_expr_ast_free(next_left);
                cxpr_expr_ast_free(next_right);
                cxpr_expr_ast_free(chain_subject);
                return NULL;
            }
            joined = cxpr_expr_ast_binary_new(CXPR_TOK_AND, left, next_cmp);
            if (!joined) {
                cxpr_expr_ast_free(left);
                cxpr_expr_ast_free(next_cmp);
                cxpr_expr_ast_free(chain_subject);
                return NULL;
            }
            left = joined;
            right = next_right;
        }
        cxpr_expr_ast_free(chain_subject);
        return left;
    }
    if ((cxpr_expr_parser_check(p, CXPR_TOK_NOT) &&
         cxpr_expr_parser_peek_next(p).type == CXPR_TOK_IN) ||
        cxpr_expr_parser_check(p, CXPR_TOK_IN)) {
        bool negated = false;
        if (cxpr_expr_parser_check(p, CXPR_TOK_NOT)) {
            negated = true;
            cxpr_expr_parser_advance(p);
        }
        cxpr_expr_parser_advance(p); /* consume 'in' */
        return cxpr_parse_set_membership(p, left, negated);
    }
    return left;
}

/* `x in [a, b, c]` -> `contains(x, [a, b, c])` (set membership).
 * `x not in [...]` negates the contains call. Requires at least one element.
 * `left` is consumed. */
static cxpr_expr_ast* cxpr_parse_set_membership(cxpr_expr_parser* p, cxpr_expr_ast* left, bool negated) {
    cxpr_expr_ast* array = NULL;
    cxpr_expr_ast** args = NULL;
    cxpr_expr_ast* call = NULL;

    if (!cxpr_expr_parser_check(p, CXPR_TOK_LBRACKET)) {
        cxpr_expr_parser_set_error(p, "Expected '[' to start set after 'in'");
        cxpr_expr_ast_free(left);
        return NULL;
    }
    if (cxpr_expr_parser_peek_next(p).type == CXPR_TOK_RBRACKET) {
        cxpr_expr_parser_set_error(p, "Set membership requires at least one element");
        cxpr_expr_ast_free(left);
        return NULL;
    }

    array = cxpr_parse_arithmetic(p);
    if (!array || p->had_error) goto fail;

    args = (cxpr_expr_ast**)calloc(2u, sizeof(cxpr_expr_ast*));
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

    call = cxpr_expr_ast_call_new("contains", args, 2u);
    if (!call) goto fail;
    args = NULL;

    if (negated) {
        cxpr_expr_ast* out = cxpr_expr_ast_unary_new(CXPR_TOK_NOT, call);
        if (!out) {
            cxpr_expr_ast_free(call);
            return NULL;
        }
        return out;
    }
    return call;

fail:
    cxpr_expr_ast_free(left);
    cxpr_expr_ast_free(array);
    if (args) {
        cxpr_expr_ast_free(args[0]);
        cxpr_expr_ast_free(args[1]);
        free(args);
    }
    cxpr_expr_ast_free(call);
    return NULL;
}

static cxpr_expr_ast* cxpr_parse_arithmetic(cxpr_expr_parser* p) {
    cxpr_expr_ast* left = cxpr_parse_term(p);
    if (!left || p->had_error) return left;
    while (cxpr_expr_parser_check(p, CXPR_TOK_PLUS) || cxpr_expr_parser_check(p, CXPR_TOK_MINUS)) {
        int op = p->current.type;
        cxpr_expr_parser_advance(p);
        cxpr_expr_ast* right = cxpr_parse_term(p);
        if (!right || p->had_error) { cxpr_expr_ast_free(left); cxpr_expr_ast_free(right); return NULL; }
        left = cxpr_expr_ast_binary_new(op, left, right);
    }
    return left;
}

static cxpr_expr_ast* cxpr_parse_term(cxpr_expr_parser* p) {
    cxpr_expr_ast* left = cxpr_parse_unary(p);
    if (!left || p->had_error) return left;
    while (cxpr_expr_parser_check(p, CXPR_TOK_STAR) || cxpr_expr_parser_check(p, CXPR_TOK_SLASH) ||
           cxpr_expr_parser_check(p, CXPR_TOK_PERCENT)) {
        int op = p->current.type;
        cxpr_expr_parser_advance(p);
        cxpr_expr_ast* right = cxpr_parse_unary(p);
        if (!right || p->had_error) { cxpr_expr_ast_free(left); cxpr_expr_ast_free(right); return NULL; }
        left = cxpr_expr_ast_binary_new(op, left, right);
    }
    return left;
}

static cxpr_expr_ast* cxpr_parse_unary(cxpr_expr_parser* p) {
    if (cxpr_expr_parser_check(p, CXPR_TOK_MINUS)) {
        cxpr_expr_parser_advance(p);
        cxpr_expr_ast* operand = cxpr_parse_unary(p);
        if (!operand || p->had_error) { cxpr_expr_ast_free(operand); return NULL; }
        return cxpr_expr_ast_unary_new(CXPR_TOK_MINUS, operand);
    }
    if (cxpr_expr_parser_check(p, CXPR_TOK_PLUS)) {
        cxpr_expr_parser_advance(p);
        return cxpr_parse_unary(p);
    }
    return cxpr_parse_power(p);
}

static cxpr_expr_ast* cxpr_parse_power(cxpr_expr_parser* p) {
    cxpr_expr_ast* left = cxpr_parse_primary(p);
    if (!left || p->had_error) return left;
    if (cxpr_expr_parser_check(p, CXPR_TOK_POWER)) {
        cxpr_expr_parser_advance(p);
        cxpr_expr_ast* right = cxpr_parse_unary(p);
        if (!right || p->had_error) { cxpr_expr_ast_free(left); cxpr_expr_ast_free(right); return NULL; }
        return cxpr_expr_ast_binary_new(CXPR_TOK_POWER, left, right);
    }
    return left;
}
