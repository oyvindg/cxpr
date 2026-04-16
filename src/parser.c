/**
 * @file parser.c
 * @brief Recursive descent parser for cxpr expressions.
 *
 * Parses token streams into AST using the EBNF grammar:
 *   expression  → pipe
 *   pipe        → ternary { "|>" ternary }
 *   ternary     → or_expr [ "?" expression ":" expression ]
 *   or_expr     → and_expr { ("||"|"or") and_expr }
 *   and_expr    → not_expr { ("&&"|"and") not_expr }
 *   not_expr    → ("!"|"not") not_expr | equality
 *   equality    → relational [ ("=="|"!=") relational ]
 *   relational  → arithmetic [ ("<"|"<="|">"|">=") arithmetic
 *                            | [ "not" ] "in" "[" expression "," expression "]" ]
 *   arithmetic  → term { ("+"|"-") term }
 *   term        → unary { ("*"|"/"|"%") unary }
 *   unary       → ("-"|"+") unary | power
 *   power       → primary [ ("^"|"**") power ]  (right-assoc)
 *   primary     → number | variable | func_call | field_access | ident | "(" expr ")" [ "." field ]
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

/* ═══════════════════════════════════════════════════════════════════════════
 * Recursive descent parser functions (forward declarations)
 * ═══════════════════════════════════════════════════════════════════════════ */

static cxpr_ast* parse_expression(cxpr_parser* p);
static cxpr_ast* parse_pipe(cxpr_parser* p);
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

static cxpr_token parser_peek_next(const cxpr_parser* p) {
    cxpr_lexer saved = p->lexer;
    return cxpr_lexer_peek(&saved);
}

static bool parse_call_argument(cxpr_parser* p, cxpr_ast** out_arg, char** out_name) {
    cxpr_ast* arg = NULL;
    char* name = NULL;

    if (!out_arg || !out_name) return false;
    *out_arg = NULL;
    *out_name = NULL;

    if (parser_check(p, CXPR_TOK_IDENTIFIER) &&
        parser_peek_next(p).type == CXPR_TOK_ASSIGN) {
        name = token_to_string(&p->current);
        if (!name) return false;
        parser_advance(p);
        if (!parser_expect(p, CXPR_TOK_ASSIGN, "Expected '=' after named argument")) {
            free(name);
            return false;
        }
    }

    arg = parse_expression(p);
    if (!arg || p->had_error) {
        free(name);
        return false;
    }

    *out_arg = arg;
    *out_name = name;
    return true;
}

static cxpr_ast* parser_clone_ast(const cxpr_ast* ast) {
    if (!ast) return NULL;

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        return cxpr_ast_new_number(ast->data.number.value);
    case CXPR_NODE_BOOL:
        return cxpr_ast_new_bool(ast->data.boolean.value);
    case CXPR_NODE_STRING:
        return cxpr_ast_new_string(ast->data.string.value);
    case CXPR_NODE_IDENTIFIER:
        return cxpr_ast_new_identifier(ast->data.identifier.name);
    case CXPR_NODE_VARIABLE:
        return cxpr_ast_new_variable(ast->data.variable.name);
    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_ast_new_field_access(ast->data.field_access.object, ast->data.field_access.field);
    case CXPR_NODE_CHAIN_ACCESS:
        return cxpr_ast_new_chain_access((const char* const*)ast->data.chain_access.path,
                                         ast->data.chain_access.depth);
    case CXPR_NODE_UNARY_OP: {
        cxpr_ast* operand = parser_clone_ast(ast->data.unary_op.operand);
        if (!operand) return NULL;
        return cxpr_ast_new_unary_op(ast->data.unary_op.op, operand);
    }
    case CXPR_NODE_BINARY_OP: {
        cxpr_ast* left = parser_clone_ast(ast->data.binary_op.left);
        cxpr_ast* right = parser_clone_ast(ast->data.binary_op.right);
        if (!left || !right) {
            cxpr_ast_free(left);
            cxpr_ast_free(right);
            return NULL;
        }
        return cxpr_ast_new_binary_op(ast->data.binary_op.op, left, right);
    }
    case CXPR_NODE_FUNCTION_CALL: {
        cxpr_ast** args = NULL;
        char** arg_names = NULL;
        if (ast->data.function_call.argc > 0) {
            args = (cxpr_ast**)calloc(ast->data.function_call.argc, sizeof(cxpr_ast*));
            arg_names = (char**)calloc(ast->data.function_call.argc, sizeof(char*));
            if (!args || !arg_names) {
                free(args);
                free(arg_names);
                return NULL;
            }
            for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
                args[i] = parser_clone_ast(ast->data.function_call.args[i]);
                if (!args[i]) {
                    for (size_t j = 0; j < i; ++j) cxpr_ast_free(args[j]);
                    for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                    free(args);
                    free(arg_names);
                    return NULL;
                }
                if (ast->data.function_call.arg_names &&
                    ast->data.function_call.arg_names[i]) {
                    arg_names[i] = cxpr_strdup(ast->data.function_call.arg_names[i]);
                    if (!arg_names[i]) {
                        for (size_t j = 0; j <= i; ++j) cxpr_ast_free(args[j]);
                        for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                        free(args);
                        free(arg_names);
                        return NULL;
                    }
                }
            }
        }
        return cxpr_ast_new_function_call_named(ast->data.function_call.name, args,
                                                arg_names, ast->data.function_call.argc);
    }
    case CXPR_NODE_PRODUCER_ACCESS: {
        cxpr_ast** args = NULL;
        char** arg_names = NULL;
        if (ast->data.producer_access.argc > 0) {
            args = (cxpr_ast**)calloc(ast->data.producer_access.argc, sizeof(cxpr_ast*));
            arg_names = (char**)calloc(ast->data.producer_access.argc, sizeof(char*));
            if (!args || !arg_names) {
                free(args);
                free(arg_names);
                return NULL;
            }
            for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
                args[i] = parser_clone_ast(ast->data.producer_access.args[i]);
                if (!args[i]) {
                    for (size_t j = 0; j < i; ++j) cxpr_ast_free(args[j]);
                    for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                    free(args);
                    free(arg_names);
                    return NULL;
                }
                if (ast->data.producer_access.arg_names &&
                    ast->data.producer_access.arg_names[i]) {
                    arg_names[i] = cxpr_strdup(ast->data.producer_access.arg_names[i]);
                    if (!arg_names[i]) {
                        for (size_t j = 0; j <= i; ++j) cxpr_ast_free(args[j]);
                        for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                        free(args);
                        free(arg_names);
                        return NULL;
                    }
                }
            }
        }
        return cxpr_ast_new_producer_access_named(ast->data.producer_access.name, args,
                                                  arg_names, ast->data.producer_access.argc,
                                                  ast->data.producer_access.field);
    }
    case CXPR_NODE_LOOKBACK: {
        cxpr_ast* target = parser_clone_ast(ast->data.lookback.target);
        cxpr_ast* index = parser_clone_ast(ast->data.lookback.index);
        if (!target || !index) {
            cxpr_ast_free(target);
            cxpr_ast_free(index);
            return NULL;
        }
        return cxpr_ast_new_lookback(target, index);
    }
    case CXPR_NODE_TERNARY: {
        cxpr_ast* condition = parser_clone_ast(ast->data.ternary.condition);
        cxpr_ast* yes = parser_clone_ast(ast->data.ternary.true_branch);
        cxpr_ast* no = parser_clone_ast(ast->data.ternary.false_branch);
        if (!condition || !yes || !no) {
            cxpr_ast_free(condition);
            cxpr_ast_free(yes);
            cxpr_ast_free(no);
            return NULL;
        }
        return cxpr_ast_new_ternary(condition, yes, no);
    }
    }

    return NULL;
}

static void parser_set_error(cxpr_parser* p, const char* message) {
    p->had_error = true;
    p->last_error.code = CXPR_ERR_SYNTAX;
    p->last_error.message = message;
    p->last_error.position = p->current.position;
    p->last_error.line = p->current.line;
    p->last_error.column = p->current.column;
}

static bool parse_interval(cxpr_parser* p,
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

    if (!parser_match(p, CXPR_TOK_LBRACKET)) {
        parser_set_error(p, "Expected '[' to start interval after 'in'");
        return false;
    }

    if (parser_check(p, CXPR_TOK_IDENTIFIER) && parser_peek_next(p).type == CXPR_TOK_ASSIGN) {
        first_named = true;
        first_name = token_to_string(&p->current);
        if (!first_name) {
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return false;
        }
        parser_advance(p);
        if (!parser_expect(p, CXPR_TOK_ASSIGN, "Expected '=' in interval bound")) goto fail;
    }
    first = parse_expression(p);
    if (!first || p->had_error) goto fail;

    if (!parser_expect(p, CXPR_TOK_COMMA, "Expected ',' in interval bounds")) goto fail;

    if (parser_check(p, CXPR_TOK_IDENTIFIER) && parser_peek_next(p).type == CXPR_TOK_ASSIGN) {
        second_named = true;
        second_name = token_to_string(&p->current);
        if (!second_name) {
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            goto fail;
        }
        parser_advance(p);
        if (!parser_expect(p, CXPR_TOK_ASSIGN, "Expected '=' in interval bound")) goto fail;
    }
    second = parse_expression(p);
    if (!second || p->had_error) goto fail;

    if (!parser_expect(p, CXPR_TOK_RBRACKET, "Expected ']' to close interval")) goto fail;

    if (first_named || second_named) {
        if (!first_named || !second_named || !first_name || !second_name) {
            parser_set_error(p, "Named interval bounds require both min=... and max=...");
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
            parser_set_error(p, "Named interval bounds must be min=..., max=...");
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

static cxpr_ast* pipe_inject_argument(cxpr_parser* p, cxpr_ast* stage, cxpr_ast* piped) {
    cxpr_ast* node = NULL;
    if (!stage || !piped) {
        cxpr_ast_free(stage);
        cxpr_ast_free(piped);
        return NULL;
    }

    switch (stage->type) {
    case CXPR_NODE_IDENTIFIER: {
        cxpr_ast** args = (cxpr_ast**)malloc(sizeof(cxpr_ast*));
        char** arg_names = (char**)calloc(1, sizeof(char*));
        if (!args || !arg_names) {
            free(args);
            free(arg_names);
            cxpr_ast_free(stage);
            cxpr_ast_free(piped);
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return NULL;
        }
        args[0] = piped;
        node = cxpr_ast_new_function_call_named(stage->data.identifier.name, args, arg_names, 1);
        if (!node) {
            free(args);
            free(arg_names);
            cxpr_ast_free(stage);
            cxpr_ast_free(piped);
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return NULL;
        }
        cxpr_ast_free(stage);
        return node;
    }
    case CXPR_NODE_FUNCTION_CALL: {
        const size_t old_argc = stage->data.function_call.argc;
        cxpr_ast** new_args = (cxpr_ast**)malloc((old_argc + 1u) * sizeof(cxpr_ast*));
        char** new_arg_names = (char**)calloc(old_argc + 1u, sizeof(char*));
        if (!new_args || !new_arg_names) {
            free(new_args);
            free(new_arg_names);
            cxpr_ast_free(stage);
            cxpr_ast_free(piped);
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return NULL;
        }

        new_args[0] = piped;
        for (size_t i = 0; i < old_argc; ++i) {
            new_args[i + 1u] = stage->data.function_call.args[i];
            new_arg_names[i + 1u] = stage->data.function_call.arg_names
                                        ? stage->data.function_call.arg_names[i]
                                        : NULL;
        }
        free(stage->data.function_call.args);
        free(stage->data.function_call.arg_names);
        stage->data.function_call.args = NULL;
        stage->data.function_call.arg_names = NULL;
        stage->data.function_call.argc = 0;

        node = cxpr_ast_new_function_call_named(stage->data.function_call.name,
                                                new_args,
                                                new_arg_names,
                                                old_argc + 1u);
        cxpr_ast_free(stage);
        if (!node) {
            for (size_t i = 0; i < old_argc + 1u; ++i) {
                free(new_arg_names[i]);
                cxpr_ast_free(new_args[i]);
            }
            free(new_args);
            free(new_arg_names);
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return NULL;
        }
        return node;
    }
    case CXPR_NODE_PRODUCER_ACCESS: {
        const size_t old_argc = stage->data.producer_access.argc;
        cxpr_ast** new_args = (cxpr_ast**)malloc((old_argc + 1u) * sizeof(cxpr_ast*));
        char** new_arg_names = (char**)calloc(old_argc + 1u, sizeof(char*));
        if (!new_args || !new_arg_names) {
            free(new_args);
            free(new_arg_names);
            cxpr_ast_free(stage);
            cxpr_ast_free(piped);
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return NULL;
        }

        new_args[0] = piped;
        for (size_t i = 0; i < old_argc; ++i) {
            new_args[i + 1u] = stage->data.producer_access.args[i];
            new_arg_names[i + 1u] = stage->data.producer_access.arg_names
                                        ? stage->data.producer_access.arg_names[i]
                                        : NULL;
        }
        free(stage->data.producer_access.args);
        free(stage->data.producer_access.arg_names);
        stage->data.producer_access.args = NULL;
        stage->data.producer_access.arg_names = NULL;
        stage->data.producer_access.argc = 0;

        node = cxpr_ast_new_producer_access_named(stage->data.producer_access.name,
                                                  new_args,
                                                  new_arg_names,
                                                  old_argc + 1u,
                                                  stage->data.producer_access.field);
        cxpr_ast_free(stage);
        if (!node) {
            for (size_t i = 0; i < old_argc + 1u; ++i) {
                free(new_arg_names[i]);
                cxpr_ast_free(new_args[i]);
            }
            free(new_args);
            free(new_arg_names);
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return NULL;
        }
        return node;
    }
    default:
        p->had_error = true;
        p->last_error.code = CXPR_ERR_SYNTAX;
        p->last_error.message = "Expected callable after '|>' (identifier or function call)";
        p->last_error.position = p->current.position;
        p->last_error.line = p->current.line;
        p->last_error.column = p->current.column;
        cxpr_ast_free(stage);
        cxpr_ast_free(piped);
        return NULL;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * expression → pipe
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Top-level rule: expression → pipe.
 * @param p Parser instance.
 * @return AST node or NULL on error.
 */
static cxpr_ast* parse_expression(cxpr_parser* p) {
    return parse_pipe(p);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * pipe → ternary { "|>" ternary }
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Forward pipe rule: ternary { "|>" ternary }.
 *
 * The parser desugars piping into regular function calls by injecting the
 * left expression as the first argument on the right callable.
 * @param p Parser instance.
 * @return AST node or NULL on error.
 */
static cxpr_ast* parse_pipe(cxpr_parser* p) {
    cxpr_ast* left = parse_ternary(p);
    if (!left || p->had_error) return left;

    while (parser_check(p, CXPR_TOK_PIPE)) {
        cxpr_ast* stage = NULL;
        parser_advance(p);
        stage = parse_ternary(p);
        if (!stage || p->had_error) {
            cxpr_ast_free(left);
            return NULL;
        }
        left = pipe_inject_argument(p, stage, left);
        if (!left || p->had_error) return NULL;
    }
    return left;
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
 * relational → arithmetic [ ("<" | "<=" | ">" | ">=") arithmetic
 *                        | [ "not" ] "in" "[" expression "," expression "]" ]
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

    if ((parser_check(p, CXPR_TOK_NOT) && parser_peek_next(p).type == CXPR_TOK_IN) ||
        parser_check(p, CXPR_TOK_IN)) {
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

        if (parser_check(p, CXPR_TOK_NOT)) {
            negated = true;
            parser_advance(p);
        }
        parser_advance(p); /* consume in */

        if (!parse_interval(p, &lower, &upper, &include_min, &include_max)) {
            cxpr_ast_free(left);
            return NULL;
        }

        left_clone = parser_clone_ast(left);
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
    cxpr_ast* node = NULL;

    if (parser_check(p, CXPR_TOK_NUMBER)) {
        const double val = p->current.number_value;
        parser_advance(p);
        node = cxpr_ast_new_number(val);
    } else if (parser_check(p, CXPR_TOK_TRUE) || parser_check(p, CXPR_TOK_FALSE)) {
        const bool value = (p->current.type == CXPR_TOK_TRUE);
        parser_advance(p);
        node = cxpr_ast_new_bool(value);
    } else if (parser_check(p, CXPR_TOK_STRING)) {
        /* String literal: token.start points to content (after opening '"') */
        const size_t len = p->current.length;
        char* value = (char*)malloc(len + 1);
        if (!value) return NULL;
        memcpy(value, p->current.start, len);
        value[len] = '\0';
        parser_advance(p);
        node = cxpr_ast_new_string(value);
        free(value);
    } else if (parser_check(p, CXPR_TOK_VARIABLE)) {
        char* name = token_to_string(&p->current);
        parser_advance(p);
        if (!name) return NULL;
        node = cxpr_ast_new_variable(name);
        free(name);
    } else if (parser_check(p, CXPR_TOK_IDENTIFIER)) {
        char* name = token_to_string(&p->current);
        parser_advance(p);
        if (!name) return NULL;

        if (parser_check(p, CXPR_TOK_LPAREN)) {
            size_t argc = 0;
            size_t args_capacity = 8;
            cxpr_ast** args = NULL;
            char** arg_names = NULL;

            parser_advance(p);
            args = (cxpr_ast**)malloc(args_capacity * sizeof(cxpr_ast*));
            arg_names = (char**)calloc(args_capacity, sizeof(char*));
            if (!args || !arg_names) {
                free(arg_names);
                free(args);
                free(name);
                return NULL;
            }

            if (!parser_check(p, CXPR_TOK_RPAREN)) {
                if (!parse_call_argument(p, &args[argc], &arg_names[argc])) {
                    free(name);
                    for (size_t i = 0; i < argc; ++i) cxpr_ast_free(args[i]);
                    for (size_t i = 0; i <= argc; ++i) free(arg_names[i]);
                    free(arg_names);
                    free(args);
                    return NULL;
                }
                argc++;

                while (parser_match(p, CXPR_TOK_COMMA)) {
                    if (argc >= args_capacity) {
                        cxpr_ast** new_args;
                        char** new_arg_names;
                        size_t old_capacity = args_capacity;
                        args_capacity *= 2;
                        new_args = (cxpr_ast**)realloc(args, args_capacity * sizeof(cxpr_ast*));
                        new_arg_names = (char**)realloc(arg_names, args_capacity * sizeof(char*));
                        if (!new_args || !new_arg_names) {
                            free(name);
                            for (size_t i = 0; i < argc; ++i) cxpr_ast_free(args[i]);
                            for (size_t i = 0; i < argc; ++i) free(arg_names[i]);
                            if (new_args && new_args != args) free(new_args);
                            if (new_arg_names && new_arg_names != arg_names) free(new_arg_names);
                            free(arg_names);
                            free(args);
                            return NULL;
                        }
                        args = new_args;
                        arg_names = new_arg_names;
                        memset(arg_names + old_capacity, 0,
                               (args_capacity - old_capacity) * sizeof(char*));
                    }
                    if (!parse_call_argument(p, &args[argc], &arg_names[argc])) {
                        free(name);
                        for (size_t i = 0; i < argc; ++i) cxpr_ast_free(args[i]);
                        for (size_t i = 0; i <= argc; ++i) free(arg_names[i]);
                        free(arg_names);
                        free(args);
                        return NULL;
                    }
                    argc++;
                }
            }

            if (!parser_expect(p, CXPR_TOK_RPAREN, "Expected ')' after function arguments")) {
                free(name);
                for (size_t i = 0; i < argc; ++i) cxpr_ast_free(args[i]);
                for (size_t i = 0; i < argc; ++i) free(arg_names[i]);
                free(arg_names);
                free(args);
                return NULL;
            }

            if (parser_check(p, CXPR_TOK_DOT)) {
                char* field = NULL;
                parser_advance(p);
                if (!parser_check(p, CXPR_TOK_IDENTIFIER)) {
                    p->had_error = true;
                    p->last_error.code = CXPR_ERR_SYNTAX;
                    p->last_error.message = "Expected field name after '.'";
                    p->last_error.position = p->current.position;
                    p->last_error.line = p->current.line;
                    p->last_error.column = p->current.column;
                    free(name);
                    for (size_t i = 0; i < argc; ++i) cxpr_ast_free(args[i]);
                    for (size_t i = 0; i < argc; ++i) free(arg_names[i]);
                    free(arg_names);
                    free(args);
                    return NULL;
                }
                field = token_to_string(&p->current);
                parser_advance(p);
                if (!field) {
                    free(name);
                    for (size_t i = 0; i < argc; ++i) cxpr_ast_free(args[i]);
                    for (size_t i = 0; i < argc; ++i) free(arg_names[i]);
                    free(arg_names);
                    free(args);
                    return NULL;
                }
                node = cxpr_ast_new_producer_access_named(name, args, arg_names, argc, field);
                free(name);
                free(field);
            } else {
                node = cxpr_ast_new_function_call_named(name, args, arg_names, argc);
                free(name);
            }
        } else if (parser_check(p, CXPR_TOK_DOT)) {
            char** segments = NULL;
            size_t depth = 0;
            size_t capacity = 4;

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
                    for (size_t i = 0; i < depth; ++i) free(segments[i]);
                    free(segments);
                    return NULL;
                }
                if (depth == capacity) {
                    char** new_segments;
                    capacity *= 2;
                    new_segments = (char**)realloc(segments, capacity * sizeof(char*));
                    if (!new_segments) {
                        for (size_t i = 0; i < depth; ++i) free(segments[i]);
                        free(segments);
                        return NULL;
                    }
                    segments = new_segments;
                }
                segments[depth] = token_to_string(&p->current);
                if (!segments[depth]) {
                    for (size_t i = 0; i < depth; ++i) free(segments[i]);
                    free(segments);
                    return NULL;
                }
                depth++;
                parser_advance(p);
            }

            node = depth == 2
                ? cxpr_ast_new_field_access(segments[0], segments[1])
                : cxpr_ast_new_chain_access((const char* const*)segments, depth);

            for (size_t i = 0; i < depth; ++i) free(segments[i]);
            free(segments);
        } else {
            node = cxpr_ast_new_identifier(name);
            free(name);
        }
    } else if (parser_match(p, CXPR_TOK_LPAREN)) {
        node = parse_expression(p);
        if (!node || p->had_error) return NULL;
        if (!parser_expect(p, CXPR_TOK_RPAREN, "Expected closing ')'")) {
            cxpr_ast_free(node);
            return NULL;
        }
        /* Support (func(...)).field — same semantics as func(...).field */
        if (parser_check(p, CXPR_TOK_DOT)) {
            char* fn_name;
            cxpr_ast** fn_args;
            char** fn_arg_names;
            size_t fn_argc;
            char* field;

            if (node->type != CXPR_NODE_FUNCTION_CALL) {
                p->had_error = true;
                p->last_error.code = CXPR_ERR_SYNTAX;
                p->last_error.message = "Field access via '.' requires a function call inside parentheses";
                p->last_error.position = p->current.position;
                p->last_error.line = p->current.line;
                p->last_error.column = p->current.column;
                cxpr_ast_free(node);
                return NULL;
            }

            fn_name      = node->data.function_call.name;
            fn_args      = node->data.function_call.args;
            fn_arg_names = node->data.function_call.arg_names;
            fn_argc      = node->data.function_call.argc;
            node->data.function_call.name     = NULL;
            node->data.function_call.args     = NULL;
            node->data.function_call.arg_names = NULL;
            node->data.function_call.argc     = 0;
            cxpr_ast_free(node);
            node = NULL;

            parser_advance(p); /* consume '.' */
            if (!parser_check(p, CXPR_TOK_IDENTIFIER)) {
                p->had_error = true;
                p->last_error.code = CXPR_ERR_SYNTAX;
                p->last_error.message = "Expected field name after '.'";
                p->last_error.position = p->current.position;
                p->last_error.line = p->current.line;
                p->last_error.column = p->current.column;
                free(fn_name);
                for (size_t i = 0; i < fn_argc; ++i) {
                    if (fn_arg_names) free(fn_arg_names[i]);
                    cxpr_ast_free(fn_args[i]);
                }
                free(fn_args);
                free(fn_arg_names);
                return NULL;
            }
            field = token_to_string(&p->current);
            parser_advance(p);
            if (!field) {
                free(fn_name);
                for (size_t i = 0; i < fn_argc; ++i) {
                    if (fn_arg_names) free(fn_arg_names[i]);
                    cxpr_ast_free(fn_args[i]);
                }
                free(fn_args);
                free(fn_arg_names);
                return NULL;
            }
            node = cxpr_ast_new_producer_access_named(fn_name, fn_args, fn_arg_names, fn_argc, field);
            free(fn_name);
            free(field);
            if (!node) return NULL;
        }
    } else {
        p->had_error = true;
        p->last_error.code = CXPR_ERR_SYNTAX;
        p->last_error.message = "Unexpected token";
        p->last_error.position = p->current.position;
        p->last_error.line = p->current.line;
        p->last_error.column = p->current.column;
        return NULL;
    }

    while (node && parser_match(p, CXPR_TOK_LBRACKET)) {
        cxpr_ast* index_expr = parse_expression(p);
        if (!index_expr || p->had_error) {
            cxpr_ast_free(node);
            return NULL;
        }
        if (!parser_expect(p, CXPR_TOK_RBRACKET, "Expected closing ']' after lookback expression")) {
            cxpr_ast_free(node);
            cxpr_ast_free(index_expr);
            return NULL;
        }
        node = cxpr_ast_new_lookback(node, index_expr);
        if (!node) {
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            return NULL;
        }
    }

    return node;
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
