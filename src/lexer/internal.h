/**
 * @file internal.h
 * @brief Internal lexer types and declarations for cxpr.
 */

#ifndef CXPR_LEXER_INTERNAL_H
#define CXPR_LEXER_INTERNAL_H

#include <cxpr/ast.h>
#include <stddef.h>

/** @brief One lexer token with source span metadata. */
typedef struct {
    cxpr_token_type type;
    const char* start;
    size_t length;
    double number_value;
    size_t position;
    size_t line;
    size_t column;
} cxpr_token;

/** @brief Mutable lexer state used while tokenizing one source string. */
typedef struct {
    const char* source;
    const char* current;
    size_t line;
    size_t column;
    size_t position;
} cxpr_lexer;

/**
 * @brief Initialize a lexer for one source string.
 * @param lexer Lexer state to initialize.
 * @param source NUL-terminated source to tokenize.
 */
void cxpr_lexer_init(cxpr_lexer* lexer, const char* source);
/**
 * @brief Consume and return the next token from the source stream.
 * @param lexer Lexer state to advance.
 * @return Next token, including source position metadata.
 */
cxpr_token cxpr_lexer_next(cxpr_lexer* lexer);
/**
 * @brief Return the next token without consuming it.
 * @param lexer Lexer state to inspect.
 * @return Next token as if `cxpr_lexer_next()` had been called.
 */
cxpr_token cxpr_lexer_peek(cxpr_lexer* lexer);

#endif /* CXPR_LEXER_INTERNAL_H */
