/**
 * @file parser_primary.c
 * @brief Primary-expression parsing rules.
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>

static bool cxpr_parse_call_argument(cxpr_parser* p, cxpr_ast** out_arg, char** out_name) {
    cxpr_ast* arg = NULL;
    char* name = NULL;
    if (!out_arg || !out_name) return false;
    *out_arg = NULL;
    *out_name = NULL;
    if (cxpr_parser_check(p, CXPR_TOK_IDENTIFIER) &&
        cxpr_parser_peek_next(p).type == CXPR_TOK_ASSIGN) {
        name = cxpr_parser_token_to_string(&p->current);
        if (!name) return false;
        cxpr_parser_advance(p);
        if (!cxpr_parser_expect(p, CXPR_TOK_ASSIGN, "Expected '=' after named argument")) {
            free(name);
            return false;
        }
    }
    arg = cxpr_parse_expression(p);
    if (!arg || p->had_error) {
        free(name);
        cxpr_ast_free(arg);
        return false;
    }
    *out_arg = arg;
    *out_name = name;
    return true;
}

static cxpr_ast* cxpr_parse_array_literal(cxpr_parser* p) {
    size_t count = 0;
    size_t capacity = 4;
    cxpr_ast** elements;

    if (!cxpr_parser_expect(p, CXPR_TOK_LBRACKET, "Expected '['")) return NULL;

    elements = (cxpr_ast**)calloc(capacity, sizeof(cxpr_ast*));
    if (!elements) return NULL;

    if (!cxpr_parser_check(p, CXPR_TOK_RBRACKET)) {
        do {
            if (count >= capacity) {
                cxpr_ast** grown;
                capacity *= 2;
                grown = (cxpr_ast**)realloc(elements, capacity * sizeof(cxpr_ast*));
                if (!grown) goto fail;
                elements = grown;
            }
            elements[count] = cxpr_parse_expression(p);
            if (!elements[count] || p->had_error) {
                cxpr_ast_free(elements[count]);
                elements[count] = NULL;
                goto fail;
            }
            count++;
        } while (cxpr_parser_match(p, CXPR_TOK_COMMA));
    }

    if (!cxpr_parser_expect(p, CXPR_TOK_RBRACKET, "Expected ']' to close array")) goto fail;
    return cxpr_ast_new_array(elements, count);

fail:
    for (size_t i = 0; i < count; ++i) cxpr_ast_free(elements[i]);
    free(elements);
    return NULL;
}

static cxpr_ast* cxpr_parse_record_literal(cxpr_parser* p) {
    size_t count = 0;
    size_t capacity = 4;
    char** names;
    cxpr_ast** values;

    if (!cxpr_parser_expect(p, CXPR_TOK_LBRACE, "Expected '{'")) return NULL;

    names = (char**)calloc(capacity, sizeof(char*));
    values = (cxpr_ast**)calloc(capacity, sizeof(cxpr_ast*));
    if (!names || !values) {
        free(names);
        free(values);
        return NULL;
    }

    if (!cxpr_parser_check(p, CXPR_TOK_RBRACE)) {
        do {
            if (count >= capacity) {
                size_t old_capacity = capacity;
                char** grown_names;
                cxpr_ast** grown_values;
                size_t new_capacity = capacity * 2;
                grown_names = (char**)realloc(names, new_capacity * sizeof(char*));
                if (!grown_names) goto fail;
                names = grown_names;
                grown_values = (cxpr_ast**)realloc(values, new_capacity * sizeof(cxpr_ast*));
                if (!grown_values) {
                    goto fail;
                }
                values = grown_values;
                capacity = new_capacity;
                memset(names + old_capacity, 0, (capacity - old_capacity) * sizeof(char*));
                memset(values + old_capacity, 0, (capacity - old_capacity) * sizeof(cxpr_ast*));
            }
            if (!cxpr_parser_check(p, CXPR_TOK_IDENTIFIER)) {
                p->had_error = true;
                p->last_error.code = CXPR_ERR_SYNTAX;
                p->last_error.message = "Expected record field name";
                p->last_error.position = p->current.position;
                p->last_error.line = p->current.line;
                p->last_error.column = p->current.column;
                goto fail;
            }
            names[count] = cxpr_parser_token_to_string(&p->current);
            if (!names[count]) goto fail;
            cxpr_parser_advance(p);
            if (cxpr_parser_match(p, CXPR_TOK_ASSIGN) ||
                cxpr_parser_match(p, CXPR_TOK_COLON)) {
                values[count] = cxpr_parse_expression(p);
            } else {
                values[count] = cxpr_ast_new_identifier(names[count]);
            }
            if (!values[count]) {
                p->had_error = true;
                p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
                p->last_error.message = "Out of memory";
                goto fail;
            }
            if (!values[count] || p->had_error) goto fail;
            count++;
        } while (cxpr_parser_match(p, CXPR_TOK_COMMA));
    }

    if (!cxpr_parser_expect(p, CXPR_TOK_RBRACE, "Expected '}' to close record")) goto fail;
    {
        cxpr_ast* record = cxpr_ast_new_record((const char* const*)names, values, count);
        for (size_t i = 0u; i < count; ++i) free(names[i]);
        free(names);
        if (!record) {
            for (size_t i = 0u; i < count; ++i) cxpr_ast_free(values[i]);
            free(values);
        }
        return record;
    }

fail:
    for (size_t i = 0; i < capacity; ++i) {
        free(names[i]);
        cxpr_ast_free(values[i]);
    }
    free(names);
    free(values);
    return NULL;
}

static char* cxpr_parser_join_segments(char** segments, size_t depth) {
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

static void cxpr_parser_free_segments(char** segments, size_t depth) {
    if (!segments) return;
    for (size_t i = 0u; i < depth; ++i) free(segments[i]);
    free(segments);
}

static bool cxpr_parser_parse_arg_list(cxpr_parser* p,
                                       cxpr_ast*** out_args,
                                       char*** out_arg_names,
                                       size_t* out_argc) {
    size_t argc = 0;
    size_t args_capacity = 8;
    cxpr_ast** args = (cxpr_ast**)malloc(args_capacity * sizeof(cxpr_ast*));
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
    cxpr_parser_advance(p);
    if (!cxpr_parser_check(p, CXPR_TOK_RPAREN)) {
        if (!cxpr_parse_call_argument(p, &args[argc], &arg_names[argc])) goto fail;
        argc++;
        while (cxpr_parser_match(p, CXPR_TOK_COMMA)) {
            if (argc >= args_capacity) {
                size_t old_capacity = args_capacity;
                cxpr_ast** new_args;
                char** new_arg_names;
                args_capacity *= 2;
                new_args = (cxpr_ast**)realloc(args, args_capacity * sizeof(cxpr_ast*));
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
    if (!cxpr_parser_expect(p, CXPR_TOK_RPAREN, "Expected ')' after function arguments")) goto fail;
    *out_args = args;
    *out_arg_names = arg_names;
    *out_argc = argc;
    return true;

fail:
    for (size_t i = 0u; i < argc; ++i) cxpr_ast_free(args[i]);
    for (size_t i = 0u; i <= argc; ++i) free(arg_names[i]);
    free(arg_names);
    free(args);
    return false;
}

cxpr_ast* cxpr_parse_primary(cxpr_parser* p) {
    cxpr_ast* node = NULL;
    if (cxpr_parser_check(p, CXPR_TOK_NUMBER)) {
        const double val = p->current.number_value;
        cxpr_parser_advance(p);
        node = cxpr_ast_new_number(val);
    } else if (cxpr_parser_check(p, CXPR_TOK_TRUE) || cxpr_parser_check(p, CXPR_TOK_FALSE)) {
        const bool value = (p->current.type == CXPR_TOK_TRUE);
        cxpr_parser_advance(p);
        node = cxpr_ast_new_bool(value);
    } else if (cxpr_parser_check(p, CXPR_TOK_STRING)) {
        const size_t len = p->current.length;
        char* value = (char*)malloc(len + 1);
        if (!value) return NULL;
        memcpy(value, p->current.start, len);
        value[len] = '\0';
        cxpr_parser_advance(p);
        node = cxpr_ast_new_string(value);
        free(value);
    } else if (cxpr_parser_check(p, CXPR_TOK_VARIABLE)) {
        char* name = cxpr_parser_token_to_string(&p->current);
        cxpr_parser_advance(p);
        if (!name) return NULL;
        if (cxpr_parser_check(p, CXPR_TOK_DOT)) {
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
            while (cxpr_parser_check(p, CXPR_TOK_DOT)) {
                cxpr_parser_advance(p);
                if (!cxpr_parser_check(p, CXPR_TOK_IDENTIFIER)) {
                    cxpr_parser_free_segments(segments, depth);
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
                        cxpr_parser_free_segments(segments, depth);
                        return NULL;
                    }
                    segments = grown;
                }
                segments[depth] = cxpr_parser_token_to_string(&p->current);
                if (!segments[depth]) {
                    cxpr_parser_free_segments(segments, depth);
                    return NULL;
                }
                depth++;
                cxpr_parser_advance(p);
            }
            name = cxpr_parser_join_segments(segments, depth);
            cxpr_parser_free_segments(segments, depth);
            if (!name) return NULL;
        }
        node = cxpr_ast_new_variable(name);
        free(name);
    } else if (cxpr_parser_check(p, CXPR_TOK_IDENTIFIER)) {
        char* name = cxpr_parser_token_to_string(&p->current);
        cxpr_parser_advance(p);
        if (!name) return NULL;
        if (cxpr_parser_check(p, CXPR_TOK_LPAREN)) {
            size_t argc = 0;
            cxpr_ast** args = NULL;
            char** arg_names = NULL;
            if (!cxpr_parser_parse_arg_list(p, &args, &arg_names, &argc)) {
                free(name);
                return NULL;
            }
            if (cxpr_parser_check(p, CXPR_TOK_DOT)) {
                char* field = NULL;
                cxpr_parser_advance(p);
                if (!cxpr_parser_check(p, CXPR_TOK_IDENTIFIER)) goto fail_field;
                field = cxpr_parser_token_to_string(&p->current);
                cxpr_parser_advance(p);
                if (!field) goto fail_field;
                node = cxpr_ast_new_producer_access_named(name, args, arg_names, argc, field);
                free(name);
                free(field);
            } else {
                node = cxpr_ast_new_function_call_named(name, args, arg_names, argc);
                free(name);
            }
            if (!node) return NULL;
            goto primary_done;
        fail_field:
            free(name);
            for (size_t i = 0; i < argc; ++i) cxpr_ast_free(args[i]);
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
        } else if (cxpr_parser_check(p, CXPR_TOK_DOT)) {
            char** segments = NULL;
            size_t depth = 0;
            size_t capacity = 4;
            segments = (char**)calloc(capacity, sizeof(char*));
            if (!segments) {
                free(name);
                return NULL;
            }
            segments[depth++] = name;
            while (cxpr_parser_check(p, CXPR_TOK_DOT)) {
                cxpr_parser_advance(p);
                if (!cxpr_parser_check(p, CXPR_TOK_IDENTIFIER)) {
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
                segments[depth] = cxpr_parser_token_to_string(&p->current);
                if (!segments[depth]) {
                    for (size_t i = 0; i < depth; ++i) free(segments[i]);
                    free(segments);
                    return NULL;
                }
                depth++;
                cxpr_parser_advance(p);
            }
            if (cxpr_parser_check(p, CXPR_TOK_LPAREN)) {
                char* fn_name = cxpr_parser_join_segments(segments, depth);
                size_t argc = 0;
                cxpr_ast** args = NULL;
                char** arg_names = NULL;
                if (!fn_name) {
                    cxpr_parser_free_segments(segments, depth);
                    return NULL;
                }
                if (!cxpr_parser_parse_arg_list(p, &args, &arg_names, &argc)) {
                    free(fn_name);
                    cxpr_parser_free_segments(segments, depth);
                    return NULL;
                }
                if (cxpr_parser_check(p, CXPR_TOK_DOT)) {
                    char* field = NULL;
                    cxpr_parser_advance(p);
                    if (!cxpr_parser_check(p, CXPR_TOK_IDENTIFIER)) {
                        free(fn_name);
                        for (size_t i = 0u; i < argc; ++i) cxpr_ast_free(args[i]);
                        for (size_t i = 0u; i < argc; ++i) free(arg_names[i]);
                        free(arg_names);
                        free(args);
                        cxpr_parser_free_segments(segments, depth);
                        p->had_error = true;
                        p->last_error.code = CXPR_ERR_SYNTAX;
                        p->last_error.message = "Expected field name after '.'";
                        p->last_error.position = p->current.position;
                        p->last_error.line = p->current.line;
                        p->last_error.column = p->current.column;
                        return NULL;
                    }
                    field = cxpr_parser_token_to_string(&p->current);
                    cxpr_parser_advance(p);
                    node = cxpr_ast_new_producer_access_named(fn_name, args, arg_names, argc, field);
                    free(field);
                } else {
                    node = cxpr_ast_new_function_call_named(fn_name, args, arg_names, argc);
                }
                free(fn_name);
            } else {
                node = depth == 2 ? cxpr_ast_new_field_access(segments[0], segments[1])
                                  : cxpr_ast_new_chain_access((const char* const*)segments, depth);
            }
            cxpr_parser_free_segments(segments, depth);
        } else {
            node = cxpr_ast_new_identifier(name);
            free(name);
        }
    } else if (cxpr_parser_match(p, CXPR_TOK_LPAREN)) {
        node = cxpr_parse_expression(p);
        if (!node || p->had_error) { cxpr_ast_free(node); return NULL; }
        if (!cxpr_parser_expect(p, CXPR_TOK_RPAREN, "Expected closing ')'")) {
            cxpr_ast_free(node);
            return NULL;
        }
        if (cxpr_parser_check(p, CXPR_TOK_DOT)) {
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
            fn_name = node->data.function_call.name;
            fn_args = node->data.function_call.args;
            fn_arg_names = node->data.function_call.arg_names;
            fn_argc = node->data.function_call.argc;
            node->data.function_call.name = NULL;
            node->data.function_call.args = NULL;
            node->data.function_call.arg_names = NULL;
            node->data.function_call.argc = 0;
            cxpr_ast_free(node);
            node = NULL;
            cxpr_parser_advance(p);
            if (!cxpr_parser_check(p, CXPR_TOK_IDENTIFIER)) {
                free(fn_name);
                for (size_t i = 0; i < fn_argc; ++i) {
                    if (fn_arg_names) free(fn_arg_names[i]);
                    cxpr_ast_free(fn_args[i]);
                }
                free(fn_args);
                free(fn_arg_names);
                p->had_error = true;
                p->last_error.code = CXPR_ERR_SYNTAX;
                p->last_error.message = "Expected field name after '.'";
                p->last_error.position = p->current.position;
                p->last_error.line = p->current.line;
                p->last_error.column = p->current.column;
                return NULL;
            }
            field = cxpr_parser_token_to_string(&p->current);
            cxpr_parser_advance(p);
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
    } else if (cxpr_parser_check(p, CXPR_TOK_LBRACKET)) {
        node = cxpr_parse_array_literal(p);
    } else if (cxpr_parser_check(p, CXPR_TOK_LBRACE)) {
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
    while (node && cxpr_parser_match(p, CXPR_TOK_LBRACKET)) {
        cxpr_ast* index_expr = cxpr_parse_expression(p);
        if (!index_expr || p->had_error) {
            cxpr_ast_free(node);
            cxpr_ast_free(index_expr);
            return NULL;
        }
        if (!cxpr_parser_expect(p, CXPR_TOK_RBRACKET, "Expected closing ']' after lookback expression")) {
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
