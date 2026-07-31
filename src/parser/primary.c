/**
 * @file parser_primary.c
 * @brief Primary-expression parsing rules.
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>

static const char* cxpr_window_reduction_name(const char* name) {
    if (!name) return NULL;
    if (strcmp(name, "max") == 0) return "__cxpr_window_highest";
    if (strcmp(name, "min") == 0) return "__cxpr_window_lowest";
    if (strcmp(name, "sum") == 0) return "__cxpr_window_sum";
    if (strcmp(name, "mean") == 0) return "__cxpr_window_mean";
    if (strcmp(name, "stddev") == 0) return "__cxpr_window_stddev";
    if (strcmp(name, "wma") == 0) return "__cxpr_window_wma";
    if (strcmp(name, "roc") == 0) return "__cxpr_window_roc";
    return NULL;
}

/*
 * A window is a compile-time expression view, not a runtime value. Lower the
 * public reduction form to the established window builtin so every evaluator
 * and code generator shares the same offset and history semantics.
 */
static cxpr_expr_ast* cxpr_parse_lower_window_reduction(cxpr_expr_ast* node) {
    cxpr_expr_ast* window;
    const char* lowered_name;
    char* owned_name;

    if (!node || node->type != CXPR_NODE_FUNCTION_CALL) {
        return node;
    }
    if (strcmp(node->data.function_call.name, "mean_absdev") == 0 &&
        node->data.function_call.argc == 2u) {
        lowered_name = "__cxpr_window_mean_absdev";
    } else {
        if (node->data.function_call.argc != 1u) return node;
        lowered_name = cxpr_window_reduction_name(
            node->data.function_call.name);
    }
    window = node->data.function_call.args[0];
    if (!lowered_name || !window || window->type != CXPR_NODE_FUNCTION_CALL ||
        strcmp(window->data.function_call.name, "window") != 0 ||
        window->data.function_call.argc != 2u) {
        return node;
    }

    owned_name = (char*)malloc(strlen(lowered_name) + 1u);
    if (!owned_name) {
        cxpr_expr_ast_free(node);
        return NULL;
    }
    strcpy(owned_name, lowered_name);

    free(node->data.function_call.name);
    node->data.function_call.name = owned_name;
    {
        size_t tail_count = node->data.function_call.argc - 1u;
        size_t lowered_argc = window->data.function_call.argc + tail_count;
        cxpr_expr_ast** lowered_args =
            (cxpr_expr_ast**)calloc(lowered_argc, sizeof(cxpr_expr_ast*));
        char** lowered_names =
            (char**)calloc(lowered_argc, sizeof(char*));
        if (!lowered_args || !lowered_names) {
            free(lowered_args);
            free(lowered_names);
            cxpr_expr_ast_free(node);
            return NULL;
        }
        for (size_t i = 0u; i < window->data.function_call.argc; ++i) {
            lowered_args[i] = window->data.function_call.args[i];
            lowered_names[i] = window->data.function_call.arg_names
                ? window->data.function_call.arg_names[i] : NULL;
        }
        for (size_t i = 0u; i < tail_count; ++i) {
            lowered_args[window->data.function_call.argc + i] =
                node->data.function_call.args[i + 1u];
            lowered_names[window->data.function_call.argc + i] =
                node->data.function_call.arg_names
                    ? node->data.function_call.arg_names[i + 1u] : NULL;
        }
        if (node->data.function_call.arg_names) {
            free(node->data.function_call.arg_names[0]);
        }
        free(node->data.function_call.args);
        free(node->data.function_call.arg_names);
        free(window->data.function_call.args);
        free(window->data.function_call.arg_names);
        node->data.function_call.args = lowered_args;
        node->data.function_call.arg_names = lowered_names;
        node->data.function_call.argc = lowered_argc;
    }
    if (node->data.function_call.arg_names &&
        node->data.function_call.arg_names[0] &&
        strcmp(node->data.function_call.arg_names[0], "expr") == 0) {
        char* value_name = (char*)malloc(sizeof("value"));
        if (!value_name) {
            window->data.function_call.args = NULL;
            window->data.function_call.arg_names = NULL;
            window->data.function_call.argc = 0u;
            cxpr_expr_ast_free(window);
            cxpr_expr_ast_free(node);
            return NULL;
        }
        memcpy(value_name, "value", sizeof("value"));
        free(node->data.function_call.arg_names[0]);
        node->data.function_call.arg_names[0] = value_name;
    }

    window->data.function_call.args = NULL;
    window->data.function_call.arg_names = NULL;
    window->data.function_call.argc = 0u;
    cxpr_expr_ast_free(window);
    return node;
}

static bool cxpr_parse_call_argument(cxpr_expr_parser* p, cxpr_expr_ast** out_arg, char** out_name) {
    cxpr_expr_ast* arg = NULL;
    char* name = NULL;
    if (!out_arg || !out_name) return false;
    *out_arg = NULL;
    *out_name = NULL;
    if (cxpr_expr_parser_check(p, CXPR_TOK_IDENTIFIER) &&
        cxpr_expr_parser_peek_next(p).type == CXPR_TOK_ASSIGN) {
        name = cxpr_expr_parser_token_to_string(&p->current);
        if (!name) return false;
        cxpr_expr_parser_advance(p);
        if (!cxpr_expr_parser_expect(p, CXPR_TOK_ASSIGN, "Expected '=' after named argument")) {
            free(name);
            return false;
        }
    }
    arg = cxpr_parse_expression(p);
    if (!arg || p->had_error) {
        free(name);
        cxpr_expr_ast_free(arg);
        return false;
    }
    *out_arg = arg;
    *out_name = name;
    return true;
}

static cxpr_expr_ast* cxpr_parse_array_literal(cxpr_expr_parser* p) {
    size_t count = 0;
    size_t capacity = 4;
    cxpr_expr_ast** elements;

    if (!cxpr_expr_parser_expect(p, CXPR_TOK_LBRACKET, "Expected '['")) return NULL;

    elements = (cxpr_expr_ast**)calloc(capacity, sizeof(cxpr_expr_ast*));
    if (!elements) return NULL;

    if (!cxpr_expr_parser_check(p, CXPR_TOK_RBRACKET)) {
        do {
            if (count >= capacity) {
                cxpr_expr_ast** grown;
                capacity *= 2;
                grown = (cxpr_expr_ast**)realloc(elements, capacity * sizeof(cxpr_expr_ast*));
                if (!grown) goto fail;
                elements = grown;
            }
            elements[count] = cxpr_parse_expression(p);
            if (!elements[count] || p->had_error) {
                cxpr_expr_ast_free(elements[count]);
                elements[count] = NULL;
                goto fail;
            }
            count++;
        } while (cxpr_expr_parser_match(p, CXPR_TOK_COMMA));
    }

    if (!cxpr_expr_parser_expect(p, CXPR_TOK_RBRACKET, "Expected ']' to close array")) goto fail;
    return cxpr_expr_ast_array_new(elements, count);

fail:
    for (size_t i = 0; i < count; ++i) cxpr_expr_ast_free(elements[i]);
    free(elements);
    return NULL;
}

static cxpr_expr_ast* cxpr_parse_record_literal(cxpr_expr_parser* p) {
    size_t count = 0;
    size_t capacity = 4;
    char** names;
    cxpr_expr_ast** values;

    if (!cxpr_expr_parser_expect(p, CXPR_TOK_LBRACE, "Expected '{'")) return NULL;

    names = (char**)calloc(capacity, sizeof(char*));
    values = (cxpr_expr_ast**)calloc(capacity, sizeof(cxpr_expr_ast*));
    if (!names || !values) {
        free(names);
        free(values);
        return NULL;
    }

    if (!cxpr_expr_parser_check(p, CXPR_TOK_RBRACE)) {
        do {
            if (count >= capacity) {
                size_t old_capacity = capacity;
                char** grown_names;
                cxpr_expr_ast** grown_values;
                size_t new_capacity = capacity * 2;
                grown_names = (char**)realloc(names, new_capacity * sizeof(char*));
                if (!grown_names) goto fail;
                names = grown_names;
                grown_values = (cxpr_expr_ast**)realloc(values, new_capacity * sizeof(cxpr_expr_ast*));
                if (!grown_values) {
                    goto fail;
                }
                values = grown_values;
                capacity = new_capacity;
                memset(names + old_capacity, 0, (capacity - old_capacity) * sizeof(char*));
                memset(values + old_capacity, 0, (capacity - old_capacity) * sizeof(cxpr_expr_ast*));
            }
            if (!cxpr_expr_parser_check(p, CXPR_TOK_IDENTIFIER)) {
                p->had_error = true;
                p->last_error.code = CXPR_ERR_SYNTAX;
                p->last_error.message = "Expected record field name";
                p->last_error.position = p->current.position;
                p->last_error.line = p->current.line;
                p->last_error.column = p->current.column;
                goto fail;
            }
            names[count] = cxpr_expr_parser_token_to_string(&p->current);
            if (!names[count]) goto fail;
            cxpr_expr_parser_advance(p);
            if (cxpr_expr_parser_match(p, CXPR_TOK_ASSIGN) ||
                cxpr_expr_parser_match(p, CXPR_TOK_COLON)) {
                values[count] = cxpr_parse_expression(p);
            } else {
                values[count] = cxpr_expr_ast_identifier_new(names[count]);
            }
            if (!values[count]) {
                p->had_error = true;
                p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
                p->last_error.message = "Out of memory";
                goto fail;
            }
            if (!values[count] || p->had_error) goto fail;
            count++;
        } while (cxpr_expr_parser_match(p, CXPR_TOK_COMMA));
    }

    if (!cxpr_expr_parser_expect(p, CXPR_TOK_RBRACE, "Expected '}' to close record")) goto fail;
    {
        cxpr_expr_ast* record = cxpr_expr_ast_record_new((const char* const*)names, values, count);
        for (size_t i = 0u; i < count; ++i) free(names[i]);
        free(names);
        if (!record) {
            for (size_t i = 0u; i < count; ++i) cxpr_expr_ast_free(values[i]);
            free(values);
        }
        return record;
    }

fail:
    for (size_t i = 0; i < capacity; ++i) {
        free(names[i]);
        cxpr_expr_ast_free(values[i]);
    }
    free(names);
    free(values);
    return NULL;
}

static char* cxpr_expr_parser_join_segments(char** segments, size_t depth) {
    size_t len = 1u;
    char* out;
    char* cursor;
    if (!segments || depth == 0u) return NULL;
    for (size_t i = 0u; i < depth; ++i) len += strlen(segments[i]) + (i > 0u ? 1u : 0u);
    out = (char*)malloc(len);
    if (!out) return NULL;
    cursor = out;
    for (size_t i = 0u; i < depth; ++i) {
        size_t n = strlen(segments[i]);
        if (i > 0u) *cursor++ = '.';
        memcpy(cursor, segments[i], n);
        cursor += n;
    }
    *cursor = '\0';
    return out;
}

static void cxpr_expr_parser_free_segments(char** segments, size_t depth) {
    if (!segments) return;
    for (size_t i = 0u; i < depth; ++i) free(segments[i]);
    free(segments);
}

static bool cxpr_expr_parser_parse_arg_list(cxpr_expr_parser* p,
                                       cxpr_expr_ast*** out_args,
                                       char*** out_arg_names,
                                       size_t* out_argc) {
    size_t argc = 0;
    size_t args_capacity = 8;
    cxpr_expr_ast** args = (cxpr_expr_ast**)malloc(args_capacity * sizeof(cxpr_expr_ast*));
    char** arg_names = (char**)calloc(args_capacity, sizeof(char*));
    if (!out_args || !out_arg_names || !out_argc) return false;
    *out_args = NULL;
    *out_arg_names = NULL;
    *out_argc = 0u;
    if (!args || !arg_names) {
        free(arg_names);
        free(args);
        return false;
    }
    cxpr_expr_parser_advance(p);
    if (!cxpr_expr_parser_check(p, CXPR_TOK_RPAREN)) {
        if (!cxpr_parse_call_argument(p, &args[argc], &arg_names[argc])) goto fail;
        argc++;
        while (cxpr_expr_parser_match(p, CXPR_TOK_COMMA)) {
            if (argc >= args_capacity) {
                size_t old_capacity = args_capacity;
                cxpr_expr_ast** new_args;
                char** new_arg_names;
                args_capacity *= 2;
                new_args = (cxpr_expr_ast**)realloc(args, args_capacity * sizeof(cxpr_expr_ast*));
                if (!new_args) goto fail;
                args = new_args;
                new_arg_names = (char**)realloc(arg_names, args_capacity * sizeof(char*));
                if (!new_arg_names) goto fail;
                arg_names = new_arg_names;
                memset(arg_names + old_capacity, 0,
                       (args_capacity - old_capacity) * sizeof(char*));
            }
            if (!cxpr_parse_call_argument(p, &args[argc], &arg_names[argc])) goto fail;
            argc++;
        }
    }
    if (!cxpr_expr_parser_expect(p, CXPR_TOK_RPAREN, "Expected ')' after function arguments")) goto fail;
    *out_args = args;
    *out_arg_names = arg_names;
    *out_argc = argc;
    return true;

fail:
    for (size_t i = 0u; i < argc; ++i) cxpr_expr_ast_free(args[i]);
    for (size_t i = 0u; i <= argc; ++i) free(arg_names[i]);
    free(arg_names);
    free(args);
    return false;
}

cxpr_expr_ast* cxpr_parse_primary(cxpr_expr_parser* p) {
    cxpr_expr_ast* node = NULL;
    if (cxpr_expr_parser_check(p, CXPR_TOK_NUMBER)) {
        const double val = p->current.number_value;
        cxpr_expr_parser_advance(p);
        node = cxpr_expr_ast_number_new(val);
    } else if (cxpr_expr_parser_check(p, CXPR_TOK_TRUE) || cxpr_expr_parser_check(p, CXPR_TOK_FALSE)) {
        const bool value = (p->current.type == CXPR_TOK_TRUE);
        cxpr_expr_parser_advance(p);
        node = cxpr_expr_ast_bool_new(value);
    } else if (cxpr_expr_parser_check(p, CXPR_TOK_STRING)) {
        const size_t len = p->current.length;
        char* value = (char*)malloc(len + 1);
        if (!value) return NULL;
        memcpy(value, p->current.start, len);
        value[len] = '\0';
        cxpr_expr_parser_advance(p);
        node = cxpr_expr_ast_new_string(value);
        free(value);
    } else if (cxpr_expr_parser_check(p, CXPR_TOK_VARIABLE)) {
        char* name = cxpr_expr_parser_token_to_string(&p->current);
        cxpr_expr_parser_advance(p);
        if (!name) return NULL;
        if (cxpr_expr_parser_check(p, CXPR_TOK_DOT)) {
            char** segments = NULL;
            size_t depth = 0u;
            size_t capacity = 4u;

            segments = (char**)calloc(capacity, sizeof(char*));
            if (!segments) {
                free(name);
                return NULL;
            }
            segments[depth++] = name;
            name = NULL;
            while (cxpr_expr_parser_check(p, CXPR_TOK_DOT)) {
                cxpr_expr_parser_advance(p);
                if (!cxpr_expr_parser_check(p, CXPR_TOK_IDENTIFIER)) {
                    cxpr_expr_parser_free_segments(segments, depth);
                    p->had_error = true;
                    p->last_error.code = CXPR_ERR_SYNTAX;
                    p->last_error.message = "Expected parameter segment after '.'";
                    p->last_error.position = p->current.position;
                    p->last_error.line = p->current.line;
                    p->last_error.column = p->current.column;
                    return NULL;
                }
                if (depth == capacity) {
                    char** grown;
                    capacity *= 2u;
                    grown = (char**)realloc(segments, capacity * sizeof(char*));
                    if (!grown) {
                        cxpr_expr_parser_free_segments(segments, depth);
                        return NULL;
                    }
                    segments = grown;
                }
                segments[depth] = cxpr_expr_parser_token_to_string(&p->current);
                if (!segments[depth]) {
                    cxpr_expr_parser_free_segments(segments, depth);
                    return NULL;
                }
                depth++;
                cxpr_expr_parser_advance(p);
            }
            name = cxpr_expr_parser_join_segments(segments, depth);
            cxpr_expr_parser_free_segments(segments, depth);
            if (!name) return NULL;
        }
        node = cxpr_expr_ast_param_new(name);
        free(name);
    } else if (cxpr_expr_parser_check(p, CXPR_TOK_IDENTIFIER)) {
        char* name = cxpr_expr_parser_token_to_string(&p->current);
        cxpr_expr_parser_advance(p);
        if (!name) return NULL;
        if (cxpr_expr_parser_check(p, CXPR_TOK_LPAREN)) {
            size_t argc = 0;
            cxpr_expr_ast** args = NULL;
            char** arg_names = NULL;
            if (!cxpr_expr_parser_parse_arg_list(p, &args, &arg_names, &argc)) {
                free(name);
                return NULL;
            }
            if (cxpr_expr_parser_check(p, CXPR_TOK_DOT)) {
                char* field = NULL;
                cxpr_expr_parser_advance(p);
                if (!cxpr_expr_parser_check(p, CXPR_TOK_IDENTIFIER)) goto fail_field;
                field = cxpr_expr_parser_token_to_string(&p->current);
                cxpr_expr_parser_advance(p);
                if (!field) goto fail_field;
                node = cxpr_expr_ast_producer_field_named_new(name, args, arg_names, argc, field);
                free(name);
                free(field);
            } else {
                node = cxpr_expr_ast_call_named_new(name, args, arg_names, argc);
                free(name);
                if (node) node = cxpr_parse_lower_window_reduction(node);
            }
            if (!node) return NULL;
            goto primary_done;
        fail_field:
            free(name);
            for (size_t i = 0; i < argc; ++i) cxpr_expr_ast_free(args[i]);
            for (size_t i = 0; i < argc; ++i) free(arg_names[i]);
            free(arg_names);
            free(args);
            p->had_error = true;
            p->last_error.code = CXPR_ERR_SYNTAX;
            p->last_error.message = "Expected field name after '.'";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return NULL;
        } else if (cxpr_expr_parser_check(p, CXPR_TOK_DOT)) {
            char** segments = NULL;
            size_t depth = 0;
            size_t capacity = 4;
            segments = (char**)calloc(capacity, sizeof(char*));
            if (!segments) {
                free(name);
                return NULL;
            }
            segments[depth++] = name;
            while (cxpr_expr_parser_check(p, CXPR_TOK_DOT)) {
                cxpr_expr_parser_advance(p);
                if (!cxpr_expr_parser_check(p, CXPR_TOK_IDENTIFIER)) {
                    for (size_t i = 0; i < depth; ++i) free(segments[i]);
                    free(segments);
                    p->had_error = true;
                    p->last_error.code = CXPR_ERR_SYNTAX;
                    p->last_error.message = "Expected field name after '.'";
                    p->last_error.position = p->current.position;
                    p->last_error.line = p->current.line;
                    p->last_error.column = p->current.column;
                    return NULL;
                }
                if (depth == capacity) {
                    char** new_segments = (char**)realloc(segments, (capacity * 2) * sizeof(char*));
                    if (!new_segments) {
                        for (size_t i = 0; i < depth; ++i) free(segments[i]);
                        free(segments);
                        return NULL;
                    }
                    capacity *= 2;
                    segments = new_segments;
                }
                segments[depth] = cxpr_expr_parser_token_to_string(&p->current);
                if (!segments[depth]) {
                    for (size_t i = 0; i < depth; ++i) free(segments[i]);
                    free(segments);
                    return NULL;
                }
                depth++;
                cxpr_expr_parser_advance(p);
            }
            if (cxpr_expr_parser_check(p, CXPR_TOK_LPAREN)) {
                char* fn_name = cxpr_expr_parser_join_segments(segments, depth);
                size_t argc = 0;
                cxpr_expr_ast** args = NULL;
                char** arg_names = NULL;
                if (!fn_name) {
                    cxpr_expr_parser_free_segments(segments, depth);
                    return NULL;
                }
                if (!cxpr_expr_parser_parse_arg_list(p, &args, &arg_names, &argc)) {
                    free(fn_name);
                    cxpr_expr_parser_free_segments(segments, depth);
                    return NULL;
                }
                if (cxpr_expr_parser_check(p, CXPR_TOK_DOT)) {
                    char* field = NULL;
                    cxpr_expr_parser_advance(p);
                    if (!cxpr_expr_parser_check(p, CXPR_TOK_IDENTIFIER)) {
                        free(fn_name);
                        for (size_t i = 0u; i < argc; ++i) cxpr_expr_ast_free(args[i]);
                        for (size_t i = 0u; i < argc; ++i) free(arg_names[i]);
                        free(arg_names);
                        free(args);
                        cxpr_expr_parser_free_segments(segments, depth);
                        p->had_error = true;
                        p->last_error.code = CXPR_ERR_SYNTAX;
                        p->last_error.message = "Expected field name after '.'";
                        p->last_error.position = p->current.position;
                        p->last_error.line = p->current.line;
                        p->last_error.column = p->current.column;
                        return NULL;
                    }
                    field = cxpr_expr_parser_token_to_string(&p->current);
                    cxpr_expr_parser_advance(p);
                    node = cxpr_expr_ast_producer_field_named_new(fn_name, args, arg_names, argc, field);
                    free(field);
                } else {
                    node = cxpr_expr_ast_call_named_new(fn_name, args, arg_names, argc);
                }
                free(fn_name);
            } else {
                node = depth == 2 ? cxpr_expr_ast_field_new(segments[0], segments[1])
                                  : cxpr_expr_ast_new_chain_access((const char* const*)segments, depth);
            }
            cxpr_expr_parser_free_segments(segments, depth);
        } else {
            node = cxpr_expr_ast_identifier_new(name);
            free(name);
        }
    } else if (cxpr_expr_parser_match(p, CXPR_TOK_LPAREN)) {
        node = cxpr_parse_expression(p);
        if (!node || p->had_error) { cxpr_expr_ast_free(node); return NULL; }
        if (!cxpr_expr_parser_expect(p, CXPR_TOK_RPAREN, "Expected closing ')'")) {
            cxpr_expr_ast_free(node);
            return NULL;
        }
        if (cxpr_expr_parser_check(p, CXPR_TOK_DOT)) {
            char* field;
            cxpr_expr_parser_advance(p);
            if (!cxpr_expr_parser_check(p, CXPR_TOK_IDENTIFIER)) {
                p->had_error = true;
                p->last_error.code = CXPR_ERR_SYNTAX;
                p->last_error.message = "Expected field name after '.'";
                p->last_error.position = p->current.position;
                p->last_error.line = p->current.line;
                p->last_error.column = p->current.column;
                cxpr_expr_ast_free(node);
                return NULL;
            }
            field = cxpr_expr_parser_token_to_string(&p->current);
            cxpr_expr_parser_advance(p);
            if (!field) {
                cxpr_expr_ast_free(node);
                return NULL;
            }
            node = cxpr_expr_ast_field_expr_new(node, field);
            free(field);
            if (!node) return NULL;
        }
    } else if (cxpr_expr_parser_check(p, CXPR_TOK_LBRACKET)) {
        node = cxpr_parse_array_literal(p);
    } else if (cxpr_expr_parser_check(p, CXPR_TOK_LBRACE)) {
        node = cxpr_parse_record_literal(p);
    } else {
        p->had_error = true;
        p->last_error.code = CXPR_ERR_SYNTAX;
        p->last_error.message = "Unexpected token";
        p->last_error.position = p->current.position;
        p->last_error.line = p->current.line;
        p->last_error.column = p->current.column;
        return NULL;
    }
primary_done:
    while (node && cxpr_expr_parser_match(p, CXPR_TOK_LBRACKET)) {
        cxpr_expr_ast* index_expr = cxpr_parse_expression(p);
        if (!index_expr || p->had_error) {
            cxpr_expr_ast_free(node);
            cxpr_expr_ast_free(index_expr);
            return NULL;
        }
        if (!cxpr_expr_parser_expect(p, CXPR_TOK_RBRACKET, "Expected closing ']' after lookback expression")) {
            cxpr_expr_ast_free(node);
            cxpr_expr_ast_free(index_expr);
            return NULL;
        }
        node = cxpr_expr_ast_lookback_new(node, index_expr);
        if (!node) {
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            return NULL;
        }
    }
    return node;
}
