/**
 * @file parser_parser.c
 * @brief Parser lifecycle, token primitives, and public parse entrypoint.
 */

#include "internal.h"
#include <stdlib.h>

void cxpr_expr_parser_advance(cxpr_expr_parser* p) {
    p->previous = p->current;
    p->current = cxpr_lexer_next(&p->lexer);
    if (p->current.type == CXPR_TOK_ERROR) {
        p->had_error = true;
        p->last_error.code = CXPR_ERR_SYNTAX;
        p->last_error.message = p->current.start;
        p->last_error.position = p->current.position;
        p->last_error.line = p->current.line;
        p->last_error.column = p->current.column;
    }
}

bool cxpr_expr_parser_check(const cxpr_expr_parser* p, cxpr_token_type type) {
    return p->current.type == type;
}

bool cxpr_expr_parser_match(cxpr_expr_parser* p, cxpr_token_type type) {
    if (!cxpr_expr_parser_check(p, type)) return false;
    cxpr_expr_parser_advance(p);
    return true;
}

bool cxpr_expr_parser_expect(cxpr_expr_parser* p, cxpr_token_type type, const char* message) {
    if (cxpr_expr_parser_check(p, type)) {
        cxpr_expr_parser_advance(p);
        return true;
    }
    p->had_error = true;
    p->last_error.code = CXPR_ERR_SYNTAX;
    p->last_error.message = message;
    p->last_error.position = p->current.position;
    p->last_error.line = p->current.line;
    p->last_error.column = p->current.column;
    return false;
}

static bool cxpr_expr_parser_token_starts_primary(cxpr_token_type type) {
    switch (type) {
        case CXPR_TOK_NUMBER:
        case CXPR_TOK_IDENTIFIER:
        case CXPR_TOK_VARIABLE:
        case CXPR_TOK_STRING:
        case CXPR_TOK_TRUE:
        case CXPR_TOK_FALSE:
        case CXPR_TOK_LPAREN:
            return true;
        default:
            return false;
    }
}

cxpr_expr_parser* cxpr_expr_parser_new(void) {
    return (cxpr_expr_parser*)calloc(1, sizeof(cxpr_expr_parser));
}

void cxpr_expr_parser_free(cxpr_expr_parser* p) {
    free(p);
}

cxpr_expr_ast* cxpr_expr_ast_parse(cxpr_expr_parser* p, const char* expression, cxpr_error* err) {
    if (!p || !expression) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "NULL parser or expression";
        }
        return NULL;
    }

    cxpr_lexer_init(&p->lexer, expression);
    p->had_error = false;
    p->last_error = (cxpr_error){0};
    cxpr_expr_parser_advance(p);

    if (p->had_error) {
        if (err) *err = p->last_error;
        return NULL;
    }

    cxpr_expr_ast* ast = cxpr_parse_expression(p);

    if (p->had_error) {
        cxpr_expr_ast_free(ast);
        if (err) *err = p->last_error;
        return NULL;
    }

    if (!cxpr_expr_parser_check(p, CXPR_TOK_EOF)) {
        p->had_error = true;
        p->last_error.code = CXPR_ERR_SYNTAX;
        p->last_error.message = cxpr_expr_parser_token_starts_primary(p->current.type)
                                    ? "Implicit multiplication is not supported; use '*' explicitly"
                                    : "Unexpected token after expression";
        p->last_error.position = p->current.position;
        p->last_error.line = p->current.line;
        p->last_error.column = p->current.column;
        cxpr_expr_ast_free(ast);
        if (err) *err = p->last_error;
        return NULL;
    }

    if (err) err->code = CXPR_OK;
    return ast;
}
