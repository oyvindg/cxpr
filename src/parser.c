/**
 * @file parser.c
 * @brief Recursive descent parser for cxpr expressions.
 *
 * Parses token streams into AST using the EBNF grammar:
 *   expression  → ternary
 *   ternary     → or_expr [ "?" expression ":" expression ]
 *   or_expr     → and_expr { ("||"|"or") and_expr }
 *   and_expr    → not_expr { ("&&"|"and") not_expr }
 *   not_expr    → ("!"|"not") not_expr | equality
 *   equality    → relational [ ("=="|"!=") relational ]
 *   relational  → arithmetic [ ("<"|"<="|">"|">=") arithmetic ]
 *   arithmetic  → term { ("+"|"-") term }
 *   term        → unary { ("*"|"/"|"%") unary }
 *   unary       → ("-"|"+") unary | power
 *   power       → primary [ ("^"|"**") power ]  (right-assoc)
 *   primary     → number | variable | func_call | field_access | ident | "(" expr ")"
 */

#include "internal.h"
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Parser helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Advance to next token; set error state on lex failure.
 * @param p Parser instance.
 */
static void parser_advance(cxpr_parser* p) {
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

/**
 * @brief Check if current token matches a type (without consuming).
 * @param p Parser instance.
 * @param type Token type to check.
 * @return True if current token matches type.
 */
static bool parser_check(const cxpr_parser* p, cxpr_token_type type) {
    return p->current.type == type;
}

/**
 * @brief If current token matches type, consume it and return true.
 * @param p Parser instance.
 * @param type Token type to match.
 * @return True if matched and consumed.
 */
static bool parser_match(cxpr_parser* p, cxpr_token_type type) {
    if (!parser_check(p, type)) return false;
    parser_advance(p);
    return true;
}

/**
 * @brief Consume expected token or set parse error.
 * @param p Parser instance.
 * @param type Token type expected.
 * @param message Error message if token does not match.
 * @return True if token matched and was consumed, false otherwise.
 */
static bool parser_expect(cxpr_parser* p, cxpr_token_type type, const char* message) {
    if (parser_check(p, type)) {
        parser_advance(p);
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

/**
 * @brief Extract a null-terminated string from a token.
 * @return Heap-allocated string (caller must free), or NULL on failure.
 */
static char* token_to_string(const cxpr_token* tok) {
    char* s = (char*)malloc(tok->length + 1);
    if (!s) return NULL;
    memcpy(s, tok->start, tok->length);
    s[tok->length] = '\0';
    return s;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Recursive descent parser functions (forward declarations)
 * ═══════════════════════════════════════════════════════════════════════════ */

static cxpr_ast* parse_expression(cxpr_parser* p);
static cxpr_ast* parse_ternary(cxpr_parser* p);
static cxpr_ast* parse_or(cxpr_parser* p);
static cxpr_ast* parse_and(cxpr_parser* p);
static cxpr_ast* parse_not(cxpr_parser* p);
static cxpr_ast* parse_equality(cxpr_parser* p);
static cxpr_ast* parse_relational(cxpr_parser* p);
static cxpr_ast* parse_arithmetic(cxpr_parser* p);
static cxpr_ast* parse_term(cxpr_parser* p);
static cxpr_ast* parse_unary(cxpr_parser* p);
static cxpr_ast* parse_power(cxpr_parser* p);
static cxpr_ast* parse_primary(cxpr_parser* p);

/* ═══════════════════════════════════════════════════════════════════════════
 * expression → ternary
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Top-level rule: expression → ternary.
 * @param p Parser instance.
 * @return AST node or NULL on error.
 */
static cxpr_ast* parse_expression(cxpr_parser* p) {
    return parse_ternary(p);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ternary → or_expr [ "?" expression ":" expression ]
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Ternary rule: or_expr [ "?" expression ":" expression ].
 * @param p Parser instance.
 * @return AST node or NULL on error.
 */
static cxpr_ast* parse_ternary(cxpr_parser* p) {
    cxpr_ast* condition = parse_or(p);
    if (!condition || p->had_error) return condition;

    if (parser_match(p, CXPR_TOK_QUESTION)) {
        cxpr_ast* true_branch = parse_expression(p);
        if (!true_branch || p->had_error) { cxpr_ast_free(condition); return NULL; }

        if (!parser_expect(p, CXPR_TOK_COLON, "Expected ':' in ternary expression")) {
            cxpr_ast_free(condition);
            cxpr_ast_free(true_branch);
            return NULL;
        }

        cxpr_ast* false_branch = parse_expression(p);
        if (!false_branch || p->had_error) {
            cxpr_ast_free(condition);
            cxpr_ast_free(true_branch);
            return NULL;
        }

        return cxpr_ast_new_ternary(condition, true_branch, false_branch);
    }

    return condition;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * or_expr → and_expr { ("||"|"or") and_expr }
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Logical OR rule: and_expr { ("||"|"or") and_expr }.
 * @param p Parser instance.
 * @return AST node or NULL on error.
 */
static cxpr_ast* parse_or(cxpr_parser* p) {
    cxpr_ast* left = parse_and(p);
    if (!left || p->had_error) return left;

    while (parser_check(p, CXPR_TOK_OR)) {
        parser_advance(p);
        cxpr_ast* right = parse_and(p);
        if (!right || p->had_error) { cxpr_ast_free(left); return NULL; }
        left = cxpr_ast_new_binary_op(CXPR_TOK_OR, left, right);
    }
    return left;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * and_expr → not_expr { ("&&"|"and") not_expr }
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Logical AND rule: not_expr { ("&&"|"and") not_expr }.
 * @param p Parser instance.
 * @return AST node or NULL on error.
 */
static cxpr_ast* parse_and(cxpr_parser* p) {
    cxpr_ast* left = parse_not(p);
    if (!left || p->had_error) return left;

    while (parser_check(p, CXPR_TOK_AND)) {
        parser_advance(p);
        cxpr_ast* right = parse_not(p);
        if (!right || p->had_error) { cxpr_ast_free(left); return NULL; }
        left = cxpr_ast_new_binary_op(CXPR_TOK_AND, left, right);
    }
    return left;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * not_expr → ("!"|"not") not_expr | equality
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Logical NOT rule: ("!"|"not") not_expr | equality.
 * @param p Parser instance.
 * @return AST node or NULL on error.
 */
static cxpr_ast* parse_not(cxpr_parser* p) {
    if (parser_check(p, CXPR_TOK_NOT)) {
        parser_advance(p);
        cxpr_ast* operand = parse_not(p);
        if (!operand || p->had_error) return NULL;
        return cxpr_ast_new_unary_op(CXPR_TOK_NOT, operand);
    }
    return parse_equality(p);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * equality → relational [ ("==" | "!=") relational ]
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Equality rule: relational [ ("==" | "!=") relational ].
 *
 * Equality operators have lower precedence than relational operators,
 * following C/C++ convention. This allows: a < b == c < d → (a<b) == (c<d).
 *
 * @param p Parser instance.
 * @return AST node or NULL on error.
 */
static cxpr_ast* parse_equality(cxpr_parser* p) {
    cxpr_ast* left = parse_relational(p);
    if (!left || p->had_error) return left;

    if (parser_check(p, CXPR_TOK_EQ) || parser_check(p, CXPR_TOK_NEQ)) {
        int op = p->current.type;
        parser_advance(p);
        cxpr_ast* right = parse_relational(p);
        if (!right || p->had_error) { cxpr_ast_free(left); return NULL; }
        return cxpr_ast_new_binary_op(op, left, right);
    }
    return left;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * relational → arithmetic [ ("<" | "<=" | ">" | ">=") arithmetic ]
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Relational rule: arithmetic [ ("<" | "<=" | ">" | ">=") arithmetic ].
 *
 * Relational operators have higher precedence than equality operators,
 * following C/C++ convention.
 *
 * @param p Parser instance.
 * @return AST node or NULL on error.
 */
static cxpr_ast* parse_relational(cxpr_parser* p) {
    cxpr_ast* left = parse_arithmetic(p);
    if (!left || p->had_error) return left;

    if (parser_check(p, CXPR_TOK_LT) || parser_check(p, CXPR_TOK_GT) ||
        parser_check(p, CXPR_TOK_LTE) || parser_check(p, CXPR_TOK_GTE)) {
        int op = p->current.type;
        parser_advance(p);
        cxpr_ast* right = parse_arithmetic(p);
        if (!right || p->had_error) { cxpr_ast_free(left); return NULL; }
        return cxpr_ast_new_binary_op(op, left, right);
    }
    return left;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * arithmetic → term { ("+"|"-") term }
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Addition/subtraction rule: term { ("+"|"-") term }.
 * @param p Parser instance.
 * @return AST node or NULL on error.
 */
static cxpr_ast* parse_arithmetic(cxpr_parser* p) {
    cxpr_ast* left = parse_term(p);
    if (!left || p->had_error) return left;

    while (parser_check(p, CXPR_TOK_PLUS) || parser_check(p, CXPR_TOK_MINUS)) {
        int op = p->current.type;
        parser_advance(p);
        cxpr_ast* right = parse_term(p);
        if (!right || p->had_error) { cxpr_ast_free(left); return NULL; }
        left = cxpr_ast_new_binary_op(op, left, right);
    }
    return left;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * term → unary { ("*"|"/"|"%") unary }
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Multiplication/division/modulo rule: unary { ("*"|"/"|"%") unary }.
 * @param p Parser instance.
 * @return AST node or NULL on error.
 */
static cxpr_ast* parse_term(cxpr_parser* p) {
    cxpr_ast* left = parse_unary(p);
    if (!left || p->had_error) return left;

    while (parser_check(p, CXPR_TOK_STAR) || parser_check(p, CXPR_TOK_SLASH) ||
           parser_check(p, CXPR_TOK_PERCENT)) {
        int op = p->current.type;
        parser_advance(p);
        cxpr_ast* right = parse_unary(p);
        if (!right || p->had_error) { cxpr_ast_free(left); return NULL; }
        left = cxpr_ast_new_binary_op(op, left, right);
    }
    return left;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * unary → ("-"|"+") unary | power
 *
 * Unary minus is LOWER precedence than power, so:
 *   -3^2 = -(3^2) = -9  (standard math convention, same as Python)
 *   (-3)^2 = 9           (explicit parentheses needed)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Unary rule: ("-"|"+") unary | power.
 * @param p Parser instance.
 * @return AST node or NULL on error.
 */
static cxpr_ast* parse_unary(cxpr_parser* p) {
    if (parser_check(p, CXPR_TOK_MINUS)) {
        parser_advance(p);
        cxpr_ast* operand = parse_unary(p);
        if (!operand || p->had_error) return NULL;
        return cxpr_ast_new_unary_op(CXPR_TOK_MINUS, operand);
    }
    if (parser_check(p, CXPR_TOK_PLUS)) {
        parser_advance(p); /* Unary plus is a no-op */
        return parse_unary(p);
    }
    return parse_power(p);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * power → primary [ ("^"|"**") power ]  (right-associative via recursion)
 *
 * Power has HIGHER precedence than unary minus (standard math convention):
 *   -2^2 = -(2^2) = -4   (not (-2)^2 = 4)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Exponentiation rule: primary [ ("^"|"**") power ] (right-associative).
 *
 * Power binds tighter than unary minus, matching mathematical convention.
 * @param p Parser instance.
 * @return AST node or NULL on error.
 */
static cxpr_ast* parse_power(cxpr_parser* p) {
    cxpr_ast* left = parse_primary(p);
    if (!left || p->had_error) return left;

    if (parser_check(p, CXPR_TOK_POWER)) {
        parser_advance(p);
        cxpr_ast* right = parse_power(p); /* Recurse for right-assoc */
        if (!right || p->had_error) { cxpr_ast_free(left); return NULL; }
        return cxpr_ast_new_binary_op(CXPR_TOK_POWER, left, right);
    }
    return left;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * primary → number | variable | func_call | field_access | ident | "(" expr ")"
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Primary rule: number | variable | func_call | field_access | ident | "(" expr ")".
 * @param p Parser instance.
 * @return AST node or NULL on error.
 */
static cxpr_ast* parse_primary(cxpr_parser* p) {
    /* Number literal */
    if (parser_check(p, CXPR_TOK_NUMBER)) {
        double val = p->current.number_value;
        parser_advance(p);
        return cxpr_ast_new_number(val);
    }

    if (parser_check(p, CXPR_TOK_TRUE) || parser_check(p, CXPR_TOK_FALSE)) {
        bool value = (p->current.type == CXPR_TOK_TRUE);
        parser_advance(p);
        return cxpr_ast_new_bool(value);
    }

    /* Variable ($name) */
    if (parser_check(p, CXPR_TOK_VARIABLE)) {
        char* name = token_to_string(&p->current);
        parser_advance(p);
        if (!name) return NULL;
        cxpr_ast* node = cxpr_ast_new_variable(name);
        free(name);
        return node;
    }

    /* Identifier → might be function call or field access */
    if (parser_check(p, CXPR_TOK_IDENTIFIER)) {
        char* name = token_to_string(&p->current);
        parser_advance(p);
        if (!name) return NULL;

        /* Function call: identifier "(" [args] ")" */
        if (parser_check(p, CXPR_TOK_LPAREN)) {
            parser_advance(p); /* consume '(' */

            /* Parse argument list */
            size_t argc = 0;
            size_t args_capacity = 8;
            cxpr_ast** args = (cxpr_ast**)malloc(args_capacity * sizeof(cxpr_ast*));
            if (!args) { free(name); return NULL; }

            if (!parser_check(p, CXPR_TOK_RPAREN)) {
                /* At least one argument */
                args[argc] = parse_expression(p);
                if (!args[argc] || p->had_error) {
                    free(name);
                    for (size_t i = 0; i < argc; i++) cxpr_ast_free(args[i]);
                    free(args);
                    return NULL;
                }
                argc++;

                while (parser_match(p, CXPR_TOK_COMMA)) {
                    if (argc >= args_capacity) {
                        args_capacity *= 2;
                        cxpr_ast** new_args = (cxpr_ast**)realloc(args, args_capacity * sizeof(cxpr_ast*));
                        if (!new_args) {
                            free(name);
                            for (size_t i = 0; i < argc; i++) cxpr_ast_free(args[i]);
                            free(args);
                            return NULL;
                        }
                        args = new_args;
                    }
                    args[argc] = parse_expression(p);
                    if (!args[argc] || p->had_error) {
                        free(name);
                        for (size_t i = 0; i < argc; i++) cxpr_ast_free(args[i]);
                        free(args);
                        return NULL;
                    }
                    argc++;
                }
            }

            if (!parser_expect(p, CXPR_TOK_RPAREN, "Expected ')' after function arguments")) {
                free(name);
                for (size_t i = 0; i < argc; i++) cxpr_ast_free(args[i]);
                free(args);
                return NULL;
            }

            /* Optional producer field access: name(args).field */
            if (parser_check(p, CXPR_TOK_DOT)) {
                parser_advance(p); /* consume '.' */
                if (!parser_check(p, CXPR_TOK_IDENTIFIER)) {
                    p->had_error = true;
                    p->last_error.code = CXPR_ERR_SYNTAX;
                    p->last_error.message = "Expected field name after '.'";
                    p->last_error.position = p->current.position;
                    p->last_error.line = p->current.line;
                    p->last_error.column = p->current.column;
                    free(name);
                    for (size_t i = 0; i < argc; i++) cxpr_ast_free(args[i]);
                    free(args);
                    return NULL;
                }

                char* field = token_to_string(&p->current);
                parser_advance(p);
                if (!field) {
                    free(name);
                    for (size_t i = 0; i < argc; i++) cxpr_ast_free(args[i]);
                    free(args);
                    return NULL;
                }

                cxpr_ast* node = cxpr_ast_new_producer_access(name, args, argc, field);
                free(name);
                free(field);
                return node;
            }

            cxpr_ast* node = cxpr_ast_new_function_call(name, args, argc);
            free(name);
            return node;
        }

        /* Field access: identifier "." identifier [ "." identifier ]* */
        if (parser_check(p, CXPR_TOK_DOT)) {
            char** segments = NULL;
            size_t depth = 0;
            size_t capacity = 4;
            cxpr_ast* node = NULL;

            segments = (char**)calloc(capacity, sizeof(char*));
            if (!segments) {
                free(name);
                return NULL;
            }
            segments[depth++] = name;

            while (parser_check(p, CXPR_TOK_DOT)) {
                parser_advance(p);
                if (!parser_check(p, CXPR_TOK_IDENTIFIER)) {
                    p->had_error = true;
                    p->last_error.code = CXPR_ERR_SYNTAX;
                    p->last_error.message = "Expected field name after '.'";
                    p->last_error.position = p->current.position;
                    p->last_error.line = p->current.line;
                    p->last_error.column = p->current.column;
                    for (size_t i = 0; i < depth; i++) free(segments[i]);
                    free(segments);
                    return NULL;
                }
                if (depth == capacity) {
                    char** new_segments;
                    capacity *= 2;
                    new_segments = (char**)realloc(segments, capacity * sizeof(char*));
                    if (!new_segments) {
                        for (size_t i = 0; i < depth; i++) free(segments[i]);
                        free(segments);
                        return NULL;
                    }
                    segments = new_segments;
                }
                segments[depth] = token_to_string(&p->current);
                if (!segments[depth]) {
                    for (size_t i = 0; i < depth; i++) free(segments[i]);
                    free(segments);
                    return NULL;
                }
                depth++;
                parser_advance(p);
            }

            if (depth == 2) {
                node = cxpr_ast_new_field_access(segments[0], segments[1]);
            } else {
                node = cxpr_ast_new_chain_access((const char* const*)segments, depth);
            }

            for (size_t i = 0; i < depth; i++) free(segments[i]);
            free(segments);
            return node;
        }

        /* Plain identifier */
        cxpr_ast* node = cxpr_ast_new_identifier(name);
        free(name);
        return node;
    }

    /* Parenthesized expression */
    if (parser_match(p, CXPR_TOK_LPAREN)) {
        cxpr_ast* expr = parse_expression(p);
        if (!expr || p->had_error) return NULL;

        if (!parser_expect(p, CXPR_TOK_RPAREN, "Expected closing ')'")) {
            cxpr_ast_free(expr);
            return NULL;
        }
        return expr;
    }

    /* Error: unexpected token */
    p->had_error = true;
    p->last_error.code = CXPR_ERR_SYNTAX;
    p->last_error.message = "Unexpected token";
    p->last_error.position = p->current.position;
    p->last_error.line = p->current.line;
    p->last_error.column = p->current.column;
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Create a new parser instance.
 * @return New parser, or NULL on allocation failure.
 */
cxpr_parser* cxpr_parser_new(void) {
    cxpr_parser* p = (cxpr_parser*)calloc(1, sizeof(cxpr_parser));
    return p;
}

/**
 * @brief Free a parser instance.
 * @param p Parser to free; safe to pass NULL.
 */
void cxpr_parser_free(cxpr_parser* p) {
    free(p);
}

/**
 * @brief Parse expression string into AST, reporting errors.
 * @param p Parser instance (must not be NULL).
 * @param expression Source string to parse.
 * @param err Optional error output; receives details on parse failure.
 * @return AST root on success, NULL on error (and err filled if non-NULL).
 */
cxpr_ast* cxpr_parse(cxpr_parser* p, const char* expression, cxpr_error* err) {
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
    parser_advance(p); /* Load first token */

    if (p->had_error) {
        if (err) *err = p->last_error;
        return NULL;
    }

    cxpr_ast* ast = parse_expression(p);

    if (p->had_error) {
        cxpr_ast_free(ast);
        if (err) *err = p->last_error;
        return NULL;
    }

    /* Ensure we consumed all input */
    if (!parser_check(p, CXPR_TOK_EOF)) {
        p->had_error = true;
        p->last_error.code = CXPR_ERR_SYNTAX;
        p->last_error.message = "Unexpected token after expression";
        p->last_error.position = p->current.position;
        p->last_error.line = p->current.line;
        p->last_error.column = p->current.column;
        cxpr_ast_free(ast);
        if (err) *err = p->last_error;
        return NULL;
    }

    if (err) err->code = CXPR_OK;
    return ast;
}
