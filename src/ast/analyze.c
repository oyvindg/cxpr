/**
 * @file analyze.c
 * @brief AST semantic analysis and metadata collection.
 */

#include "internal.h"
#include "core.h"
#include "registry/internal.h"
#include "call/args.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    cxpr_analysis* out;
    const cxpr_registry* reg;
    cxpr_error* err;
} cxpr_expr_ast_analyze_state;

static void cxpr_expr_ast_set_error(cxpr_error* err, cxpr_error_code code, const char* message) {
    if (!err) return;
    err->code = code;
    err->message = message;
    err->position = 0;
    err->line = 0;
    err->column = 0;
}

static const char* cxpr_expr_ast_unknown_function_message(const char* name) {
    static CXPR_THREAD_LOCAL char message[256];
    if (!name || name[0] == '\0') return "Unknown function";
    snprintf(message, sizeof(message), "Unknown function '%s'", name);
    return message;
}

static cxpr_expr_type cxpr_expr_type_from_value(cxpr_value_type type) {
    if (type == CXPR_VALUE_BOOL) return CXPR_EXPR_BOOL;
    if (type == CXPR_VALUE_STRUCT) return CXPR_EXPR_STRUCT;
    return CXPR_EXPR_NUMBER;
}

static void cxpr_expr_ast_note_unsupported_codegen(cxpr_analysis* out, const char* node_kind) {
    if (!out || out->has_unsupported_codegen_nodes) return;
    out->has_unsupported_codegen_nodes = true;
    out->first_unsupported_codegen_node = node_kind;
}

static cxpr_expr_type cxpr_expr_ast_analyze_node(const cxpr_expr_ast* ast,
                                            cxpr_expr_ast_analyze_state* state,
                                            unsigned depth,
                                            unsigned lookback_depth,
                                            bool* ok) {
    cxpr_analysis* out;
    cxpr_expr_type left_type;
    cxpr_expr_type right_type;

    if (!ast || !state || !state->out || !ok) return CXPR_EXPR_UNKNOWN;
    out = state->out;

    if (depth > out->max_depth) out->max_depth = depth;
    out->node_count++;

    switch (ast->type) {
        case CXPR_NODE_NUMBER:
            return CXPR_EXPR_NUMBER;
        case CXPR_NODE_BOOL:
            return CXPR_EXPR_BOOL;
        case CXPR_NODE_ARRAY:
            cxpr_expr_ast_note_unsupported_codegen(out, "array");
            for (size_t i = 0; i < ast->data.array.count; ++i) {
                (void)cxpr_expr_ast_analyze_node(
                    ast->data.array.elements[i], state, depth + 1, lookback_depth, ok);
                if (!*ok) return CXPR_EXPR_UNKNOWN;
            }
            return CXPR_EXPR_UNKNOWN;
        case CXPR_NODE_RECORD:
            cxpr_expr_ast_note_unsupported_codegen(out, "record");
            for (size_t i = 0; i < ast->data.record.field_count; ++i) {
                (void)cxpr_expr_ast_analyze_node(
                    ast->data.record.field_values[i], state, depth + 1, lookback_depth, ok);
                if (!*ok) return CXPR_EXPR_UNKNOWN;
            }
            return CXPR_EXPR_UNKNOWN;
        case CXPR_NODE_STRING:
            return CXPR_EXPR_UNKNOWN;
        case CXPR_NODE_IDENTIFIER:
            out->uses_variables = true;
            out->is_constant = false;
            return CXPR_EXPR_NUMBER;
        case CXPR_NODE_VARIABLE:
            out->uses_parameters = true;
            out->is_constant = false;
            return CXPR_EXPR_NUMBER;
        case CXPR_NODE_FIELD_ACCESS:
        case CXPR_NODE_CHAIN_ACCESS:
            cxpr_expr_ast_note_unsupported_codegen(
                out, ast->type == CXPR_NODE_FIELD_ACCESS ? "field_access" : "chain_access");
            out->uses_field_access = true;
            out->is_constant = false;
            if (ast->type == CXPR_NODE_FIELD_ACCESS && ast->data.field_access.base) {
                (void)cxpr_expr_ast_analyze_node(
                    ast->data.field_access.base, state, depth + 1, lookback_depth, ok);
            }
            return CXPR_EXPR_UNKNOWN;
        case CXPR_NODE_PRODUCER_ACCESS: {
            cxpr_func_entry* entry = NULL;
            cxpr_expr_ast_note_unsupported_codegen(out, "producer_access");
            out->uses_functions = true;
            out->uses_field_access = true;
            out->is_constant = false;

            for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
                (void)cxpr_expr_ast_analyze_node(
                    ast->data.producer_access.args[i], state, depth + 1, lookback_depth, ok);
                if (!*ok) return CXPR_EXPR_UNKNOWN;
            }

            if (state->reg) {
                entry = cxpr_registry_find(state->reg, ast->data.producer_access.name);
                if (!entry) {
                    out->has_unknown_functions = true;
                    out->first_unknown_function = ast->data.producer_access.name;
                    cxpr_expr_ast_set_error(
                        state->err,
                        CXPR_ERR_UNKNOWN_FUNCTION,
                        cxpr_expr_ast_unknown_function_message(ast->data.producer_access.name));
                    *ok = false;
                    return CXPR_EXPR_UNKNOWN;
                }
                if (ast->data.producer_access.argc < entry->min_args ||
                    ast->data.producer_access.argc > entry->max_args) {
                    cxpr_expr_ast_set_error(state->err, CXPR_ERR_WRONG_ARITY, "Wrong number of arguments");
                    *ok = false;
                    return CXPR_EXPR_UNKNOWN;
                }
                if (cxpr_expr_ast_call_uses_named_args(ast)) {
                    cxpr_error_code code = CXPR_OK;
                    const char* message = NULL;
                    if (!cxpr_call_bind_args(ast, entry, NULL, &code, &message)) {
                        cxpr_expr_ast_set_error(state->err, code, message);
                        *ok = false;
                        return CXPR_EXPR_UNKNOWN;
                    }
                }
                if (entry->defined_body) out->uses_expressions = true;
            }
            return CXPR_EXPR_UNKNOWN;
        }
        case CXPR_NODE_BINARY_OP:
            left_type = cxpr_expr_ast_analyze_node(
                ast->data.binary_op.left, state, depth + 1, lookback_depth, ok);
            if (!*ok) return CXPR_EXPR_UNKNOWN;
            right_type = cxpr_expr_ast_analyze_node(
                ast->data.binary_op.right, state, depth + 1, lookback_depth, ok);
            if (!*ok) return CXPR_EXPR_UNKNOWN;
            switch (ast->data.binary_op.op) {
                case CXPR_TOK_AND:
                case CXPR_TOK_OR:
                    out->can_short_circuit = true;
                    return CXPR_EXPR_BOOL;
                case CXPR_TOK_EQ:
                case CXPR_TOK_NEQ:
                case CXPR_TOK_LT:
                case CXPR_TOK_GT:
                case CXPR_TOK_LTE:
                case CXPR_TOK_GTE:
                case CXPR_TOK_IN:
                    return CXPR_EXPR_BOOL;
                default:
                    (void)left_type;
                    (void)right_type;
                    return CXPR_EXPR_NUMBER;
            }
        case CXPR_NODE_UNARY_OP:
            left_type = cxpr_expr_ast_analyze_node(
                ast->data.unary_op.operand, state, depth + 1, lookback_depth, ok);
            if (!*ok) return CXPR_EXPR_UNKNOWN;
            return (ast->data.unary_op.op == CXPR_TOK_NOT) ? CXPR_EXPR_BOOL : left_type;
        case CXPR_NODE_FUNCTION_CALL: {
            cxpr_func_entry* entry = NULL;
            out->uses_functions = true;
            out->is_constant = false;

            for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
                (void)cxpr_expr_ast_analyze_node(
                    ast->data.function_call.args[i], state, depth + 1, lookback_depth, ok);
                if (!*ok) return CXPR_EXPR_UNKNOWN;
            }

            if (state->reg) {
                entry = cxpr_registry_find(state->reg, ast->data.function_call.name);
                if (!entry) {
                    out->has_unknown_functions = true;
                    out->first_unknown_function = ast->data.function_call.name;
                    cxpr_expr_ast_set_error(
                        state->err,
                        CXPR_ERR_UNKNOWN_FUNCTION,
                        cxpr_expr_ast_unknown_function_message(ast->data.function_call.name));
                    *ok = false;
                    return CXPR_EXPR_UNKNOWN;
                }
                if (ast->data.function_call.argc < entry->min_args ||
                    ast->data.function_call.argc > entry->max_args) {
                    cxpr_expr_ast_set_error(state->err, CXPR_ERR_WRONG_ARITY, "Wrong number of arguments");
                    *ok = false;
                    return CXPR_EXPR_UNKNOWN;
                }
                if (cxpr_expr_ast_call_uses_named_args(ast)) {
                    cxpr_error_code code = CXPR_OK;
                    const char* message = NULL;
                    if (!cxpr_call_bind_args(ast, entry, NULL, &code, &message)) {
                        cxpr_expr_ast_set_error(state->err, code, message);
                        *ok = false;
                        return CXPR_EXPR_UNKNOWN;
                    }
                }
                if (entry->defined_body) out->uses_expressions = true;
                if (entry->has_return_type) return cxpr_expr_type_from_value(entry->return_type);
            }
            return CXPR_EXPR_UNKNOWN;
        }
        case CXPR_NODE_LOOKBACK:
            out->is_constant = false;
            if (ast->data.lookback.index &&
                ast->data.lookback.index->type == CXPR_NODE_NUMBER &&
                isfinite(ast->data.lookback.index->data.number.value) &&
                ast->data.lookback.index->data.number.value >= 0.0 &&
                floor(ast->data.lookback.index->data.number.value) ==
                    ast->data.lookback.index->data.number.value) {
                const unsigned max_unsigned = ~0u;
                double value = ast->data.lookback.index->data.number.value;
                if (value <= (double)(max_unsigned - lookback_depth)) {
                    lookback_depth += (unsigned)value;
                    if (lookback_depth > out->max_lookback_depth) {
                        out->max_lookback_depth = lookback_depth;
                    }
                }
            } else {
                /* Target backends may lower dynamic indexes directly. */
                if (out->max_lookback_depth < 512u) out->max_lookback_depth = 512u;
            }
            (void)cxpr_expr_ast_analyze_node(
                ast->data.lookback.target, state, depth + 1, lookback_depth, ok);
            if (!*ok) return CXPR_EXPR_UNKNOWN;
            (void)cxpr_expr_ast_analyze_node(
                ast->data.lookback.index, state, depth + 1, 0u, ok);
            return CXPR_EXPR_UNKNOWN;
        case CXPR_NODE_TERNARY:
            out->can_short_circuit = true;
            (void)cxpr_expr_ast_analyze_node(
                ast->data.ternary.condition, state, depth + 1, lookback_depth, ok);
            if (!*ok) return CXPR_EXPR_UNKNOWN;
            left_type = cxpr_expr_ast_analyze_node(
                ast->data.ternary.true_branch, state, depth + 1, lookback_depth, ok);
            if (!*ok) return CXPR_EXPR_UNKNOWN;
            right_type = cxpr_expr_ast_analyze_node(
                ast->data.ternary.false_branch, state, depth + 1, lookback_depth, ok);
            if (!*ok) return CXPR_EXPR_UNKNOWN;
            return (left_type == right_type) ? left_type : CXPR_EXPR_UNKNOWN;
    }

    return CXPR_EXPR_UNKNOWN;
}

bool cxpr_analyze(const cxpr_expr_ast* ast, const cxpr_registry* reg,
                  cxpr_analysis* out_analysis, cxpr_error* err) {
    cxpr_expr_ast_analyze_state state;
    const char* refs[256];
    const char* funcs[256];
    const char* vars[256];
    bool ok = true;
    cxpr_expr_type result_type;

    if (!ast || !out_analysis) {
        cxpr_expr_ast_set_error(err, CXPR_ERR_SYNTAX, "Invalid AST");
        return false;
    }

    memset(out_analysis, 0, sizeof(*out_analysis));
    if (err) *err = (cxpr_error){0};
    out_analysis->is_constant = true;

    state.out = out_analysis;
    state.reg = reg;
    state.err = err;

    result_type = cxpr_expr_ast_analyze_node(ast, &state, 1, 0u, &ok);
    out_analysis->result_type = result_type;
    out_analysis->is_predicate = (result_type == CXPR_EXPR_BOOL);
    out_analysis->reference_count = cxpr_expr_ast_references(ast, refs, 256);
    out_analysis->function_count = cxpr_expr_ast_functions_used(ast, funcs, 256);
    out_analysis->parameter_count = cxpr_expr_ast_variables_used(ast, vars, 256);
    out_analysis->field_path_count = 0;
    for (size_t i = 0; i < out_analysis->reference_count && i < 256; ++i) {
        if (strchr(refs[i], '.')) out_analysis->field_path_count++;
    }

    if (err && ok) err->code = CXPR_OK;
    return ok;
}

bool cxpr_analyze_expr(const char* expression, const cxpr_registry* reg,
                       cxpr_analysis* out_analysis, cxpr_error* err) {
    cxpr_expr_parser* parser;
    cxpr_expr_ast* ast;
    bool ok;

    parser = cxpr_expr_parser_new();
    if (!parser) {
        cxpr_expr_ast_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return false;
    }

    ast = cxpr_expr_ast_parse(parser, expression, err);
    cxpr_expr_parser_free(parser);
    if (!ast) return false;

    ok = cxpr_analyze(ast, reg, out_analysis, err);
    cxpr_expr_ast_free(ast);
    return ok;
}
