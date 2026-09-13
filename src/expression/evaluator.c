/**
 * @file evaluator.c
 * @brief Evaluator lifecycle and execution for cxpr.
 */

#include "../context/internal.h"
#include "internal.h"
#include "../eval/internal.h"
#include "../limits.h"

#include <cxpr/analysis.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cxpr_expression_wrap_compile_error(const cxpr_expression_entry* entry,
                                               cxpr_error* err) {
    static CXPR_THREAD_LOCAL char message[512];
    char detail[384];

    if (!entry || !entry->name || !err || err->code == CXPR_OK) return;
    snprintf(detail, sizeof(detail), "%s", err->message ? err->message : cxpr_error_string(err->code));
    snprintf(message, sizeof(message), "Expression '%s': %s", entry->name, detail);
    err->message = message;
}

static void cxpr_expression_wrap_eval_error(const cxpr_expression_entry* entry,
                                            cxpr_error* err) {
    static CXPR_THREAD_LOCAL char message[1024];
    char detail[384];

    if (!entry || !entry->name || !err || err->code == CXPR_OK) return;
    snprintf(detail, sizeof(detail), "%s", err->message ? err->message : cxpr_error_string(err->code));
    snprintf(
        message,
        sizeof(message),
        "Expression '%s' eval failed: %s; expr: %s",
        entry->name,
        detail,
        entry->expression ? entry->expression : "");
    err->message = message;
}

static bool cxpr_expression_entry_used_as_struct_prefix(
    const cxpr_evaluator* evaluator,
    size_t entry_index) {
    const char* name;
    size_t name_len;

    if (!evaluator || entry_index >= evaluator->count) return false;
    name = evaluator->expressions[entry_index].name;
    if (!name) return false;
    name_len = strlen(name);
    if (name_len == 0u) return false;

    for (size_t i = 0; i < evaluator->count; ++i) {
        const char* refs[256];
        size_t nrefs;

        if (i == entry_index || !evaluator->expressions[i].ast) continue;
        nrefs = cxpr_expr_ast_references(evaluator->expressions[i].ast, refs, 256);
        for (size_t r = 0; r < nrefs && r < 256; ++r) {
            if (refs[r] &&
                strncmp(refs[r], name, name_len) == 0 &&
                refs[r][name_len] == '.') {
                return true;
            }
        }
    }
    return false;
}

static int cxpr_expression_eval_struct_alias(
    const cxpr_expression_entry* entry,
    const cxpr_context* ctx,
    const cxpr_registry* reg,
    cxpr_value* out,
    cxpr_error* err) {
    cxpr_func_entry* fn;
    const cxpr_expr_ast* ordered_args[CXPR_MAX_CALL_ARGS] = {0};
    const cxpr_struct_value* produced;

    if (!entry || !entry->ast || !ctx || !reg || !out) return 0;
    if (cxpr_expr_ast_kind_of(entry->ast) != CXPR_NODE_FUNCTION_CALL) return 0;

    fn = cxpr_eval_cached_function_entry(entry->ast, reg);
    if (!fn || !fn->struct_producer) return 0;
    if (!cxpr_eval_bind_call_args(entry->ast, fn, ordered_args, err)) return -1;

    produced = cxpr_eval_struct_result(
        fn,
        cxpr_expr_ast_call_name(entry->ast),
        ordered_args,
        cxpr_expr_ast_call_arg_count(entry->ast),
        NULL,
        ctx,
        reg,
        err);
    if (err && err->code != CXPR_OK) return -1;
    if (!produced) return -1;

    *out = cxpr_struct((cxpr_struct_value*)produced);
    return 1;
}

static bool cxpr_evaluator_grow(cxpr_evaluator* evaluator) {
    if (evaluator->capacity > SIZE_MAX / 2) return false;
    size_t new_cap = evaluator->capacity * 2;
    cxpr_expression_entry* new_expressions =
        (cxpr_expression_entry*)calloc(new_cap, sizeof(cxpr_expression_entry));
    if (!new_expressions) return false;
    memcpy(new_expressions, evaluator->expressions,
           evaluator->count * sizeof(cxpr_expression_entry));
    free(evaluator->expressions);
    evaluator->expressions = new_expressions;
    evaluator->capacity = new_cap;
    return true;
}

bool cxpr_evaluator_reserve_for_entry(cxpr_evaluator* evaluator) {
    if (evaluator->count >= evaluator->capacity) {
        return cxpr_evaluator_grow(evaluator);
    }
    return true;
}


cxpr_evaluator* cxpr_evaluator_new(const cxpr_registry* reg) {
    cxpr_evaluator* evaluator = (cxpr_evaluator*)calloc(1, sizeof(cxpr_evaluator));
    if (!evaluator) return NULL;
    evaluator->capacity = CXPR_EXPRESSION_INITIAL_CAPACITY;
    evaluator->expressions =
        (cxpr_expression_entry*)calloc(evaluator->capacity, sizeof(cxpr_expression_entry));
    if (!evaluator->expressions) {
        free(evaluator);
        return NULL;
    }
    evaluator->registry = reg;
    evaluator->parser = cxpr_expr_parser_new();
    if (!evaluator->parser) {
        free(evaluator->expressions);
        free(evaluator);
        return NULL;
    }
    return evaluator;
}

void cxpr_evaluator_free(cxpr_evaluator* evaluator) {
    if (!evaluator) return;
    for (size_t i = 0; i < evaluator->count; i++) {
        cxpr_expression_result_dispose(&evaluator->expressions[i].result);
        free(evaluator->expressions[i].name);
        free(evaluator->expressions[i].expression);
        cxpr_expr_ast_free(evaluator->expressions[i].ast);
        cxpr_expr_compiled_free(evaluator->expressions[i].program);
    }
    free(evaluator->expressions);
    free(evaluator->eval_order);
    cxpr_expr_parser_free(evaluator->parser);
    free(evaluator);
}

bool cxpr_evaluator_compile(cxpr_evaluator* evaluator, cxpr_error* err) {
    if (!evaluator) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "NULL evaluator";
        }
        return false;
    }

    if (!cxpr_expression_topo_sort(evaluator, err)) return false;

    for (size_t i = 0; i < evaluator->count; i++) {
        cxpr_expression_entry* entry = &evaluator->expressions[i];
        cxpr_analysis analysis = {0};
        cxpr_expr_compiled_free(entry->program);
        entry->program = NULL;
        if (!cxpr_analyze(entry->ast, evaluator->registry, &analysis, err)) {
            cxpr_expression_wrap_compile_error(entry, err);
            evaluator->compiled = false;
            return false;
        }
        entry->program = cxpr_expr_compile(entry->ast, evaluator->registry, err);
        if (!entry->program) {
            cxpr_expression_wrap_compile_error(entry, err);
            evaluator->compiled = false;
            return false;
        }
    }
    for (size_t i = 0; i < evaluator->count; i++) {
        evaluator->expressions[i].used_as_struct_prefix =
            cxpr_expression_entry_used_as_struct_prefix(evaluator, i);
    }

    evaluator->compiled = true;
    if (err) err->code = CXPR_OK;
    return true;
}

void cxpr_evaluator_eval(cxpr_evaluator* evaluator, cxpr_context* ctx, cxpr_error* err) {
    const cxpr_evaluator* previous_scope;

    if (!evaluator || !ctx) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "NULL argument";
        }
        return;
    }

    if (!evaluator->compiled) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Evaluator not compiled";
        }
        return;
    }

    previous_scope = ctx->expression_scope;
    cxpr_context_set_expression_scope(ctx, evaluator);
    cxpr_context_clear_cached_structs(ctx);
    cxpr_eval_memo_enter(ctx);

    for (size_t i = 0; i < evaluator->count; i++) {
        evaluator->expressions[i].evaluated = false;
        cxpr_expression_result_dispose(&evaluator->expressions[i].result);
    }

    for (size_t i = 0; i < evaluator->eval_order_count; i++) {
        size_t idx = evaluator->eval_order[i];
        cxpr_expression_entry* entry = &evaluator->expressions[idx];
        cxpr_error eval_err = {0};
        cxpr_value value = {0};

        if (entry->used_as_struct_prefix) {
            int struct_alias = cxpr_expression_eval_struct_alias(
                entry,
                ctx,
                evaluator->registry,
                &value,
                &eval_err);
            if (struct_alias < 0) {
                cxpr_eval_memo_leave(ctx);
                cxpr_context_set_expression_scope(ctx, previous_scope);
                if (err) *err = eval_err;
                return;
            }
            if (struct_alias == 0 && entry->program) {
                (void)cxpr_expr_compiled_eval(entry->program, ctx, evaluator->registry, &value, &eval_err);
            } else if (struct_alias == 0) {
                (void)cxpr_eval_ast(entry->ast, ctx, evaluator->registry, &value, &eval_err);
            }
        } else if (entry->program) {
            (void)cxpr_expr_compiled_eval(entry->program, ctx, evaluator->registry, &value, &eval_err);
        } else {
            (void)cxpr_eval_ast(entry->ast, ctx, evaluator->registry, &value, &eval_err);
        }
        if (eval_err.code != CXPR_OK) {
            cxpr_eval_memo_leave(ctx);
            cxpr_context_set_expression_scope(ctx, previous_scope);
            if (err) {
                *err = eval_err;
                cxpr_expression_wrap_eval_error(entry, err);
            }
            return;
        }

        if (value.type != CXPR_VALUE_NUMBER &&
            value.type != CXPR_VALUE_BOOL &&
            value.type != CXPR_VALUE_STRUCT &&
            value.type != CXPR_VALUE_STRING &&
            value.type != CXPR_VALUE_NULL &&
            value.type != CXPR_VALUE_TIMESTAMP &&
            value.type != CXPR_VALUE_DURATION &&
            value.type != CXPR_VALUE_ARRAY) {
            cxpr_eval_memo_leave(ctx);
            cxpr_context_set_expression_scope(ctx, previous_scope);
            if (err) {
                err->code = CXPR_ERR_TYPE_MISMATCH;
                err->message = "Expression result has unsupported value type";
                cxpr_expression_wrap_eval_error(entry, err);
            }
            return;
        }

        entry->result = cxpr_expression_result_clone(&value, &eval_err);
        if (eval_err.code != CXPR_OK) {
            cxpr_eval_memo_leave(ctx);
            cxpr_context_set_expression_scope(ctx, previous_scope);
            if (err) {
                *err = eval_err;
                cxpr_expression_wrap_eval_error(entry, err);
            }
            return;
        }
        entry->evaluated = true;
    }

    cxpr_eval_memo_leave(ctx);
    cxpr_context_set_expression_scope(ctx, previous_scope);
    if (err) err->code = CXPR_OK;
}

bool cxpr_expression_compile(cxpr_evaluator* evaluator, cxpr_error* err) {
    return cxpr_evaluator_compile(evaluator, err);
}

void cxpr_expression_eval_all(cxpr_evaluator* evaluator, cxpr_context* ctx, cxpr_error* err) {
    cxpr_evaluator_eval(evaluator, ctx, err);
}
