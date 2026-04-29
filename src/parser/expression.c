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

static bool cxpr_parse_interval(cxpr_parser* p,
                                cxpr_ast** out_min,
                                cxpr_ast** out_max,
                                bool* out_include_min,
                                bool* out_include_max);

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
        if (!true_branch || p->had_error) { cxpr_ast_free(condition); return NULL; }
        if (!cxpr_parser_expect(p, CXPR_TOK_COLON, "Expected ':' in ternary expression")) {
            cxpr_ast_free(condition);
            cxpr_ast_free(true_branch);
            return NULL;
        }
        cxpr_ast* false_branch = cxpr_parse_expression(p);
        if (!false_branch || p->had_error) {
            cxpr_ast_free(condition);
            cxpr_ast_free(true_branch);
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
        if (!right || p->had_error) { cxpr_ast_free(left); return NULL; }
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
        if (!right || p->had_error) { cxpr_ast_free(left); return NULL; }
        left = cxpr_ast_new_binary_op(CXPR_TOK_AND, left, right);
    }
    return left;
}

static cxpr_ast* cxpr_parse_not(cxpr_parser* p) {
    if (cxpr_parser_check(p, CXPR_TOK_NOT)) {
        cxpr_parser_advance(p);
        cxpr_ast* operand = cxpr_parse_not(p);
        if (!operand || p->had_error) return NULL;
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
        if (!right || p->had_error) { cxpr_ast_free(left); return NULL; }
        return cxpr_ast_new_binary_op(op, left, right);
    }
    return left;
}

static cxpr_ast* cxpr_parse_relational(cxpr_parser* p) {
    cxpr_ast* left = cxpr_parse_arithmetic(p);
    if (!left || p->had_error) return left;
    if (cxpr_parser_check(p, CXPR_TOK_LT) || cxpr_parser_check(p, CXPR_TOK_GT) ||
        cxpr_parser_check(p, CXPR_TOK_LTE) || cxpr_parser_check(p, CXPR_TOK_GTE)) {
        int op = p->current.type;
        cxpr_parser_advance(p);
        cxpr_ast* right = cxpr_parse_arithmetic(p);
        if (!right || p->had_error) { cxpr_ast_free(left); return NULL; }
        return cxpr_ast_new_binary_op(op, left, right);
    }
    if ((cxpr_parser_check(p, CXPR_TOK_NOT) && cxpr_parser_peek_next(p).type == CXPR_TOK_IN) ||
        cxpr_parser_check(p, CXPR_TOK_IN)) {
        bool negated = false;
        bool include_min = false;
        bool include_max = false;
        int lo_op;
        int hi_op;
        cxpr_ast* left_clone = NULL;
        cxpr_ast* lower = NULL;
        cxpr_ast* upper = NULL;
        cxpr_ast* lo_cmp = NULL;
        cxpr_ast* hi_cmp = NULL;
        cxpr_ast* both = NULL;
        cxpr_ast* out = NULL;

        if (cxpr_parser_check(p, CXPR_TOK_NOT)) {
            negated = true;
            cxpr_parser_advance(p);
        }
        cxpr_parser_advance(p);
        if (!cxpr_parse_interval(p, &lower, &upper, &include_min, &include_max)) {
            cxpr_ast_free(left);
            return NULL;
        }
        left_clone = cxpr_parser_clone_ast(left);
        if (!left_clone) {
            cxpr_ast_free(left);
            cxpr_ast_free(lower);
            cxpr_ast_free(upper);
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return NULL;
        }
        lo_op = include_min ? CXPR_TOK_GTE : CXPR_TOK_GT;
        hi_op = include_max ? CXPR_TOK_LTE : CXPR_TOK_LT;
        lo_cmp = cxpr_ast_new_binary_op(lo_op, left, lower);
        hi_cmp = cxpr_ast_new_binary_op(hi_op, left_clone, upper);
        if (!lo_cmp || !hi_cmp) {
            cxpr_ast_free(lo_cmp);
            cxpr_ast_free(hi_cmp);
            return NULL;
        }
        both = cxpr_ast_new_binary_op(CXPR_TOK_AND, lo_cmp, hi_cmp);
        if (!both) {
            cxpr_ast_free(lo_cmp);
            cxpr_ast_free(hi_cmp);
            return NULL;
        }
        if (negated) {
            out = cxpr_ast_new_unary_op(CXPR_TOK_NOT, both);
            if (!out) {
                cxpr_ast_free(both);
                return NULL;
            }
            return out;
        }
        return both;
    }
    return left;
}

static cxpr_ast* cxpr_parse_arithmetic(cxpr_parser* p) {
    cxpr_ast* left = cxpr_parse_term(p);
    if (!left || p->had_error) return left;
    while (cxpr_parser_check(p, CXPR_TOK_PLUS) || cxpr_parser_check(p, CXPR_TOK_MINUS)) {
        int op = p->current.type;
        cxpr_parser_advance(p);
        cxpr_ast* right = cxpr_parse_term(p);
        if (!right || p->had_error) { cxpr_ast_free(left); return NULL; }
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
        if (!right || p->had_error) { cxpr_ast_free(left); return NULL; }
        left = cxpr_ast_new_binary_op(op, left, right);
    }
    return left;
}

static cxpr_ast* cxpr_parse_unary(cxpr_parser* p) {
    if (cxpr_parser_check(p, CXPR_TOK_MINUS)) {
        cxpr_parser_advance(p);
        cxpr_ast* operand = cxpr_parse_unary(p);
        if (!operand || p->had_error) return NULL;
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
        cxpr_ast* right = cxpr_parse_power(p);
        if (!right || p->had_error) { cxpr_ast_free(left); return NULL; }
        return cxpr_ast_new_binary_op(CXPR_TOK_POWER, left, right);
    }
    return left;
}

static bool cxpr_parse_interval(cxpr_parser* p,
                                cxpr_ast** out_min,
                                cxpr_ast** out_max,
                                bool* out_include_min,
                                bool* out_include_max) {
    cxpr_ast* first = NULL;
    cxpr_ast* second = NULL;
    cxpr_ast* min_ast = NULL;
    cxpr_ast* max_ast = NULL;
    char* first_name = NULL;
    char* second_name = NULL;
    bool first_named = false;
    bool second_named = false;

    if (!out_min || !out_max || !out_include_min || !out_include_max) return false;
    *out_min = NULL;
    *out_max = NULL;
    *out_include_min = true;
    *out_include_max = true;

    if (!cxpr_parser_match(p, CXPR_TOK_LBRACKET)) {
        cxpr_parser_set_error(p, "Expected '[' to start interval after 'in'");
        return false;
    }

    if (cxpr_parser_check(p, CXPR_TOK_IDENTIFIER) &&
        cxpr_parser_peek_next(p).type == CXPR_TOK_ASSIGN) {
        first_named = true;
        first_name = cxpr_parser_token_to_string(&p->current);
        if (!first_name) {
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return false;
        }
        cxpr_parser_advance(p);
        if (!cxpr_parser_expect(p, CXPR_TOK_ASSIGN, "Expected '=' in interval bound")) goto fail;
    }
    first = cxpr_parse_expression(p);
    if (!first || p->had_error) goto fail;
    if (!cxpr_parser_expect(p, CXPR_TOK_COMMA, "Expected ',' in interval bounds")) goto fail;
    if (cxpr_parser_check(p, CXPR_TOK_IDENTIFIER) &&
        cxpr_parser_peek_next(p).type == CXPR_TOK_ASSIGN) {
        second_named = true;
        second_name = cxpr_parser_token_to_string(&p->current);
        if (!second_name) {
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            goto fail;
        }
        cxpr_parser_advance(p);
        if (!cxpr_parser_expect(p, CXPR_TOK_ASSIGN, "Expected '=' in interval bound")) goto fail;
    }
    second = cxpr_parse_expression(p);
    if (!second || p->had_error) goto fail;
    if (!cxpr_parser_expect(p, CXPR_TOK_RBRACKET, "Expected ']' to close interval")) goto fail;

    if (first_named || second_named) {
        if (!first_named || !second_named || !first_name || !second_name) {
            cxpr_parser_set_error(p, "Named interval bounds require both min=... and max=...");
            goto fail;
        }
        if (strcmp(first_name, "min") == 0 && strcmp(second_name, "max") == 0) {
            min_ast = first;
            max_ast = second;
            first = NULL;
            second = NULL;
        } else if (strcmp(first_name, "max") == 0 && strcmp(second_name, "min") == 0) {
            min_ast = second;
            max_ast = first;
            first = NULL;
            second = NULL;
        } else {
            cxpr_parser_set_error(p, "Named interval bounds must be min=..., max=...");
            goto fail;
        }
    } else {
        min_ast = first;
        max_ast = second;
        first = NULL;
        second = NULL;
    }

    *out_min = min_ast;
    *out_max = max_ast;
    free(first_name);
    free(second_name);
    return true;
fail:
    cxpr_ast_free(first);
    cxpr_ast_free(second);
    cxpr_ast_free(min_ast);
    cxpr_ast_free(max_ast);
    free(first_name);
    free(second_name);
    return false;
}
