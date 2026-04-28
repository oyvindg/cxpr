/**
 * @file parser_internal.h
 * @brief Internal helpers shared by parser modules.
 */

#ifndef CXPR_PARSER_INTERNAL_H
#define CXPR_PARSER_INTERNAL_H

#include "../lexer/internal.h"
#include "../ast/internal.h"

/** @brief Mutable parser state for one in-progress parse. */
struct cxpr_parser {
    cxpr_lexer lexer;
    cxpr_token current;         /**< Current token */
    cxpr_token previous;        /**< Previous token (for error reporting) */
    bool had_error;
    cxpr_error last_error;
};

/** @brief Copy one token's lexeme into a newly allocated string. */
char* cxpr_parser_token_to_string(const cxpr_token* tok);
/** @brief Peek one token ahead without consuming the current token. */
cxpr_token cxpr_parser_peek_next(const cxpr_parser* p);
/** @brief Deep-clone an AST subtree for parser rewrites. */
cxpr_ast* cxpr_parser_clone_ast(const cxpr_ast* ast);
/** @brief Record a parser error at the current token position. */
void cxpr_parser_set_error(cxpr_parser* p, const char* message);
/** @brief Inject a piped expression as the leading argument to the next call stage. */
cxpr_ast* cxpr_parser_pipe_inject_argument(cxpr_parser* p, cxpr_ast* stage, cxpr_ast* piped);
/** @brief Advance the parser to the next token. */
void cxpr_parser_advance(cxpr_parser* p);
/** @brief Check whether the current token matches one expected type. */
bool cxpr_parser_check(const cxpr_parser* p, cxpr_token_type type);
/** @brief Consume the current token when it matches one expected type. */
bool cxpr_parser_match(cxpr_parser* p, cxpr_token_type type);
/** @brief Require one token kind or record a parser error. */
bool cxpr_parser_expect(cxpr_parser* p, cxpr_token_type type, const char* message);
/** @brief Parse one full expression at the current precedence entry point. */
cxpr_ast* cxpr_parse_expression(cxpr_parser* p);
/** @brief Parse one primary expression node. */
cxpr_ast* cxpr_parse_primary(cxpr_parser* p);

#endif /* CXPR_PARSER_INTERNAL_H */
