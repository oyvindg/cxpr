/**
 * @file lexer.c
 * @brief Tokenizer for cxpr expressions.
 *
 * Converts expression strings into a stream of tokens.
 * Supports numbers, identifiers, $variables, operators, keywords,
 * and position tracking (line/column).
 */

#include "internal.h"
#include <ctype.h>
#include <math.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Lexer helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

/** @brief Check if character can start an identifier. */
static bool cxpr_is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

/** @brief Check if character can continue an identifier. */
static bool cxpr_is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

/** @brief Advance lexer by one character, updating position tracking. */
static void cxpr_lexer_advance(cxpr_lexer* lexer) {
    if (*lexer->current == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    lexer->current++;
    lexer->position++;
}

/** @brief Peek at next character (one ahead of current). */
static char cxpr_lexer_peek_next(const cxpr_lexer* lexer) {
    if (*lexer->current == '\0') return '\0';
    return lexer->current[1];
}

/** @brief Skip whitespace characters, advancing position. */
static void cxpr_lexer_skip_whitespace(cxpr_lexer* lexer) {
    while (*lexer->current && isspace((unsigned char)*lexer->current)) {
        cxpr_lexer_advance(lexer);
    }
}

/** @brief Construct a token with the given type and position. */
static cxpr_token cxpr_make_token(cxpr_token_type type, const char* start, size_t length,
                               size_t position, size_t line, size_t column) {
    cxpr_token tok;
    tok.type = type;
    tok.start = start;
    tok.length = length;
    tok.number_value = 0.0;
    tok.position = position;
    tok.line = line;
    tok.column = column;
    return tok;
}

/** @brief Construct an error token at current position. */
static cxpr_token cxpr_make_error_token(const char* message, const cxpr_lexer* lexer) {
    cxpr_token tok;
    tok.type = CXPR_TOK_ERROR;
    tok.start = message;
    tok.length = strlen(message);
    tok.number_value = 0.0;
    tok.position = lexer->position;
    tok.line = lexer->line;
    tok.column = lexer->column;
    return tok;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Keyword / alias lookup
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char* keyword;
    cxpr_token_type type;
} cxpr_keyword_entry;

/**
 * @brief Case-insensitive keyword/alias table.
 *
 * Checked after an identifier is lexed to see if it's
 * a keyword (and, or, not, eq, lt, etc.) or boolean literal.
 */
static const cxpr_keyword_entry cxpr_keywords[] = {
    /* Logical */
    {"and",   CXPR_TOK_AND},
    {"AND",   CXPR_TOK_AND},
    {"or",    CXPR_TOK_OR},
    {"OR",    CXPR_TOK_OR},
    {"not",   CXPR_TOK_NOT},
    {"NOT",   CXPR_TOK_NOT},
    {"in",    CXPR_TOK_IN},
    {"IN",    CXPR_TOK_IN},
    {"true",  CXPR_TOK_TRUE},
    {"TRUE",  CXPR_TOK_TRUE},
    {"false", CXPR_TOK_FALSE},
    {"FALSE", CXPR_TOK_FALSE},
    /* Comparison aliases */
    {"eq",    CXPR_TOK_EQ},
    {"EQ",    CXPR_TOK_EQ},
    {"ne",    CXPR_TOK_NEQ},
    {"NE",    CXPR_TOK_NEQ},
    {"neq",   CXPR_TOK_NEQ},
    {"NEQ",   CXPR_TOK_NEQ},
    {"lt",    CXPR_TOK_LT},
    {"LT",    CXPR_TOK_LT},
    {"gt",    CXPR_TOK_GT},
    {"GT",    CXPR_TOK_GT},
    {"le",    CXPR_TOK_LTE},
    {"LE",    CXPR_TOK_LTE},
    {"lte",   CXPR_TOK_LTE},
    {"LTE",   CXPR_TOK_LTE},
    {"ge",    CXPR_TOK_GTE},
    {"GE",    CXPR_TOK_GTE},
    {"gte",   CXPR_TOK_GTE},
    {"GTE",   CXPR_TOK_GTE},
    {NULL, 0}
};

/**
 * @brief Check if identifier text matches a keyword.
 * @return Token type if keyword, or CXPR_TOK_IDENTIFIER if not.
 */
static cxpr_token_type cxpr_check_keyword(const char* text, size_t length) {
    for (const cxpr_keyword_entry* kw = cxpr_keywords; kw->keyword; kw++) {
        if (strlen(kw->keyword) == length && memcmp(kw->keyword, text, length) == 0) {
            return kw->type;
        }
    }
    return CXPR_TOK_IDENTIFIER;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Number parsing
 * ═══════════════════════════════════════════════════════════════════════════ */

/** @brief Lex a numeric literal (integer, decimal, scientific). */
static cxpr_token cxpr_lexer_number(cxpr_lexer* lexer) {
    const char* start = lexer->current;
    size_t start_pos = lexer->position;
    size_t start_line = lexer->line;
    size_t start_col = lexer->column;

    /* Integer part */
    while (isdigit((unsigned char)*lexer->current)) {
        cxpr_lexer_advance(lexer);
    }

    /* Decimal part */
    if (*lexer->current == '.' && isdigit((unsigned char)lexer->current[1])) {
        cxpr_lexer_advance(lexer); /* skip '.' */
        while (isdigit((unsigned char)*lexer->current)) {
            cxpr_lexer_advance(lexer);
        }
    }

    /* Scientific notation */
    if (*lexer->current == 'e' || *lexer->current == 'E') {
        cxpr_lexer_advance(lexer);
        if (*lexer->current == '+' || *lexer->current == '-') {
            cxpr_lexer_advance(lexer);
        }
        if (!isdigit((unsigned char)*lexer->current)) {
            return cxpr_make_error_token("Invalid scientific notation", lexer);
        }
        while (isdigit((unsigned char)*lexer->current)) {
            cxpr_lexer_advance(lexer);
        }
    }

    cxpr_token tok = cxpr_make_token(CXPR_TOK_NUMBER, start,
                                  (size_t)(lexer->current - start),
                                  start_pos, start_line, start_col);

    /* Parse the number value */
    char buf[64];
    size_t len = (size_t)(lexer->current - start);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';
    tok.number_value = strtod(buf, NULL);
    return tok;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Identifier / keyword parsing
 * ═══════════════════════════════════════════════════════════════════════════ */

/** @brief Lex an identifier or keyword. */
static cxpr_token cxpr_lexer_identifier(cxpr_lexer* lexer) {
    const char* start = lexer->current;
    size_t start_pos = lexer->position;
    size_t start_line = lexer->line;
    size_t start_col = lexer->column;

    while (cxpr_is_ident_char(*lexer->current)) {
        cxpr_lexer_advance(lexer);
    }

    size_t length = (size_t)(lexer->current - start);

    /* null / NULL — NaN literal (no-value sentinel, like Pine Script's na) */
    if ((length == 4 && (memcmp(start, "null", 4) == 0 || memcmp(start, "NULL", 4) == 0)) ||
        (length == 4 && memcmp(start, "Null", 4) == 0)) {
        cxpr_token tok = cxpr_make_token(CXPR_TOK_NUMBER, start, length,
                                      start_pos, start_line, start_col);
        tok.number_value = (double)NAN;
        return tok;
    }

    /* Check for keyword aliases */
    {
        cxpr_token_type kw_type = cxpr_check_keyword(start, length);
        return cxpr_make_token(kw_type, start, length, start_pos, start_line, start_col);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Variable ($name) parsing
 * ═══════════════════════════════════════════════════════════════════════════ */

/** @brief Lex a $variable parameter reference. */
static cxpr_token cxpr_lexer_variable(cxpr_lexer* lexer) {
    size_t start_pos = lexer->position;
    size_t start_line = lexer->line;
    size_t start_col = lexer->column;

    cxpr_lexer_advance(lexer); /* skip '$' */
    const char* name_start = lexer->current;

    if (!cxpr_is_ident_start(*lexer->current)) {
        return cxpr_make_error_token("Expected identifier after '$'", lexer);
    }

    while (cxpr_is_ident_char(*lexer->current)) {
        cxpr_lexer_advance(lexer);
    }

    /* Support dotted parameter paths: $group.param_name */
    while (*lexer->current == '.') {
        if (!cxpr_is_ident_start(cxpr_lexer_peek_next(lexer))) {
            return cxpr_make_error_token("Expected identifier after '.' in variable", lexer);
        }
        cxpr_lexer_advance(lexer); /* consume '.' */
        while (cxpr_is_ident_char(*lexer->current)) {
            cxpr_lexer_advance(lexer);
        }
    }

    return cxpr_make_token(CXPR_TOK_VARIABLE, name_start,
                         (size_t)(lexer->current - name_start),
                         start_pos, start_line, start_col);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Initialize lexer state for a source string.
 * @param[in] lexer Lexer to initialize
 * @param[in] source Null-terminated source string to tokenize
 */
void cxpr_lexer_init(cxpr_lexer* lexer, const char* source) {
    lexer->source = source;
    lexer->current = source;
    lexer->line = 1;
    lexer->column = 1;
    lexer->position = 0;
}

/**
 * @brief Consume and return the next token from the stream.
 * @param[in] lexer Lexer state
 * @return Next token (CXPR_TOK_EOF at end of input)
 */
cxpr_token cxpr_lexer_next(cxpr_lexer* lexer) {
    cxpr_lexer_skip_whitespace(lexer);

    if (*lexer->current == '\0') {
        return cxpr_make_token(CXPR_TOK_EOF, lexer->current, 0,
                             lexer->position, lexer->line, lexer->column);
    }

    char c = *lexer->current;
    const char* start = lexer->current;
    size_t start_pos = lexer->position;
    size_t start_line = lexer->line;
    size_t start_col = lexer->column;

    /* Numbers */
    if (isdigit((unsigned char)c)) {
        return cxpr_lexer_number(lexer);
    }

    /* Identifiers and keywords */
    if (cxpr_is_ident_start(c)) {
        return cxpr_lexer_identifier(lexer);
    }

    /* Variables ($name) */
    if (c == '$') {
        return cxpr_lexer_variable(lexer);
    }

    /* String literals ("...") */
    if (c == '"') {
        cxpr_lexer_advance(lexer); /* skip opening '"' */
        const char* str_start = lexer->current;
        while (*lexer->current && *lexer->current != '"') {
            cxpr_lexer_advance(lexer);
        }
        cxpr_token tok = cxpr_make_token(CXPR_TOK_STRING, str_start,
                                          (size_t)(lexer->current - str_start),
                                          start_pos, start_line, start_col);
        if (*lexer->current == '"') cxpr_lexer_advance(lexer); /* skip closing '"' */
        return tok;
    }

    /* Two-character operators */
    char next = cxpr_lexer_peek_next(lexer);

    if (c == '=' && next == '=') {
        cxpr_lexer_advance(lexer); cxpr_lexer_advance(lexer);
        return cxpr_make_token(CXPR_TOK_EQ, start, 2, start_pos, start_line, start_col);
    }
    if (c == '!' && next == '=') {
        cxpr_lexer_advance(lexer); cxpr_lexer_advance(lexer);
        return cxpr_make_token(CXPR_TOK_NEQ, start, 2, start_pos, start_line, start_col);
    }
    if (c == '<' && next == '=') {
        cxpr_lexer_advance(lexer); cxpr_lexer_advance(lexer);
        return cxpr_make_token(CXPR_TOK_LTE, start, 2, start_pos, start_line, start_col);
    }
    if (c == '>' && next == '=') {
        cxpr_lexer_advance(lexer); cxpr_lexer_advance(lexer);
        return cxpr_make_token(CXPR_TOK_GTE, start, 2, start_pos, start_line, start_col);
    }
    if (c == '&' && next == '&') {
        cxpr_lexer_advance(lexer); cxpr_lexer_advance(lexer);
        return cxpr_make_token(CXPR_TOK_AND, start, 2, start_pos, start_line, start_col);
    }
    if (c == '|' && next == '>') {
        cxpr_lexer_advance(lexer); cxpr_lexer_advance(lexer);
        return cxpr_make_token(CXPR_TOK_PIPE, start, 2, start_pos, start_line, start_col);
    }
    if (c == '|' && next == '|') {
        cxpr_lexer_advance(lexer); cxpr_lexer_advance(lexer);
        return cxpr_make_token(CXPR_TOK_OR, start, 2, start_pos, start_line, start_col);
    }
    if (c == '*' && next == '*') {
        cxpr_lexer_advance(lexer); cxpr_lexer_advance(lexer);
        return cxpr_make_token(CXPR_TOK_POWER, start, 2, start_pos, start_line, start_col);
    }

    /* Single-character operators and delimiters */
    cxpr_lexer_advance(lexer);
    switch (c) {
    case '+': return cxpr_make_token(CXPR_TOK_PLUS, start, 1, start_pos, start_line, start_col);
    case '-': return cxpr_make_token(CXPR_TOK_MINUS, start, 1, start_pos, start_line, start_col);
    case '*': return cxpr_make_token(CXPR_TOK_STAR, start, 1, start_pos, start_line, start_col);
    case '/': return cxpr_make_token(CXPR_TOK_SLASH, start, 1, start_pos, start_line, start_col);
    case '%': return cxpr_make_token(CXPR_TOK_PERCENT, start, 1, start_pos, start_line, start_col);
    case '^': return cxpr_make_token(CXPR_TOK_POWER, start, 1, start_pos, start_line, start_col);
    case '=': return cxpr_make_token(CXPR_TOK_ASSIGN, start, 1, start_pos, start_line, start_col);
    case '<': return cxpr_make_token(CXPR_TOK_LT, start, 1, start_pos, start_line, start_col);
    case '>': return cxpr_make_token(CXPR_TOK_GT, start, 1, start_pos, start_line, start_col);
    case '!': return cxpr_make_token(CXPR_TOK_NOT, start, 1, start_pos, start_line, start_col);
    case '(': return cxpr_make_token(CXPR_TOK_LPAREN, start, 1, start_pos, start_line, start_col);
    case ')': return cxpr_make_token(CXPR_TOK_RPAREN, start, 1, start_pos, start_line, start_col);
    case '[': return cxpr_make_token(CXPR_TOK_LBRACKET, start, 1, start_pos, start_line, start_col);
    case ']': return cxpr_make_token(CXPR_TOK_RBRACKET, start, 1, start_pos, start_line, start_col);
    case ',': return cxpr_make_token(CXPR_TOK_COMMA, start, 1, start_pos, start_line, start_col);
    case '.': return cxpr_make_token(CXPR_TOK_DOT, start, 1, start_pos, start_line, start_col);
    case '?': return cxpr_make_token(CXPR_TOK_QUESTION, start, 1, start_pos, start_line, start_col);
    case ':': return cxpr_make_token(CXPR_TOK_COLON, start, 1, start_pos, start_line, start_col);
    default:
        return cxpr_make_error_token("Unexpected character", lexer);
    }
}

/**
 * @brief Peek at the next token without consuming it.
 * @param[in] lexer Lexer state
 * @return Next token (CXPR_TOK_EOF at end of input)
 */
cxpr_token cxpr_lexer_peek(cxpr_lexer* lexer) {
    cxpr_lexer saved = *lexer;
    cxpr_token tok = cxpr_lexer_next(lexer);
    *lexer = saved;
    return tok;
}
