/**
 * @file token.h
 * @brief Public token/operator tags for cxpr.
 */

#ifndef CXPR_TOKEN_H
#define CXPR_TOKEN_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Token/operator tags used by the parser and stored on operator AST nodes.
 *
 * These values are part of the public AST surface because `cxpr_ast_operator()`
 * returns them for unary and binary operator nodes.
 */
typedef enum {
    /* Literals */
    CXPR_TOK_NUMBER,            /**< Numeric literal */
    CXPR_TOK_IDENTIFIER,        /**< Identifier (e.g. "rsi", "ema_fast") */
    CXPR_TOK_VARIABLE,          /**< Parameter variable ($name) */
    CXPR_TOK_TRUE,              /**< true */
    CXPR_TOK_FALSE,             /**< false */
    CXPR_TOK_STRING,            /**< String literal */

    /* Arithmetic operators */
    CXPR_TOK_PLUS,              /**< + */
    CXPR_TOK_MINUS,             /**< - */
    CXPR_TOK_STAR,              /**< * */
    CXPR_TOK_SLASH,             /**< / */
    CXPR_TOK_PERCENT,           /**< % */
    CXPR_TOK_POWER,             /**< ^ or ** */

    /* Comparison / assignment operators */
    CXPR_TOK_ASSIGN,            /**< = */
    CXPR_TOK_EQ,                /**< == */
    CXPR_TOK_NEQ,               /**< != */
    CXPR_TOK_LT,                /**< < */
    CXPR_TOK_GT,                /**< > */
    CXPR_TOK_LTE,               /**< <= */
    CXPR_TOK_GTE,               /**< >= */

    /* Logical operators */
    CXPR_TOK_AND,               /**< && or and */
    CXPR_TOK_OR,                /**< || or or */
    CXPR_TOK_NOT,               /**< ! or not */
    CXPR_TOK_IN,                /**< in (set membership) */

    /* Delimiters */
    CXPR_TOK_LPAREN,            /**< ( */
    CXPR_TOK_RPAREN,            /**< ) */
    CXPR_TOK_LBRACKET,          /**< [ */
    CXPR_TOK_RBRACKET,          /**< ] */
    CXPR_TOK_LBRACE,            /**< { */
    CXPR_TOK_RBRACE,            /**< } */
    CXPR_TOK_COMMA,             /**< , */
    CXPR_TOK_DOT,               /**< . */
    CXPR_TOK_PIPE,              /**< |> */
    CXPR_TOK_QUESTION,          /**< ? */
    CXPR_TOK_COLON,             /**< : */

    /* Special */
    CXPR_TOK_EOF,               /**< End of input */
    CXPR_TOK_ERROR              /**< Lexer error */
} cxpr_token_type;

#ifdef __cplusplus
}
#endif

#endif /* CXPR_TOKEN_H */
