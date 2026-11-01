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
        const cxpr_token next = cxpr_expr_parser_peek_next(p);
        const bool adjacent_zero_prefix =
            p->previous.type == CXPR_TOK_NUMBER && p->previous.length == 1u &&
            p->previous.start[0] == '0' && p->current.type == CXPR_TOK_IDENTIFIER &&
            p->previous.position + p->previous.length == p->current.position;
        const bool unsupported_binary = adjacent_zero_prefix &&
            (p->current.start[0] == 'b' || p->current.start[0] == 'B');
        const bool unsupported_hexadecimal = adjacent_zero_prefix &&
            (p->current.start[0] == 'x' || p->current.start[0] == 'X');
        p->had_error = true;
        p->last_error.code = CXPR_ERR_SYNTAX;
        if (unsupported_binary) {
            p->last_error.message =
                "Binary numeric literals are not supported; use decimal notation";
        } else if (unsupported_hexadecimal) {
            p->last_error.message =
                "Hexadecimal numeric literals are not supported; use decimal notation";
        } else if (p->current.type == CXPR_TOK_IDENTIFIER && next.type == CXPR_TOK_LBRACE) {
            p->last_error.message =
                "Unexpected identifier before metadata block; metadata must follow the value directly";
        } else {
            p->last_error.message = cxpr_expr_parser_token_starts_primary(p->current.type)
                                        ? "Implicit multiplication is not supported; use '*' explicitly"
                                        : "Unexpected token after expression";
        }
        const cxpr_token* error_token = unsupported_binary || unsupported_hexadecimal
            ? &p->previous : &p->current;
        p->last_error.position = error_token->position;
        p->last_error.line = error_token->line;
        p->last_error.column = error_token->column;
        cxpr_expr_ast_free(ast);
        if (err) *err = p->last_error;
        return NULL;
    }

    if (err) err->code = CXPR_OK;
    return ast;
}
