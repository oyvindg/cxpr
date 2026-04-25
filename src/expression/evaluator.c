/**
 * @file evaluator.c
 * @brief Evaluator lifecycle and execution for cxpr.
 */

#include "../context/internal.h"
#include "internal.h"

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
    evaluator->parser = cxpr_parser_new();
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
        cxpr_ast_free(evaluator->expressions[i].ast);
        cxpr_program_free(evaluator->expressions[i].program);
    }
    free(evaluator->expressions);
    free(evaluator->eval_order);
    cxpr_parser_free(evaluator->parser);
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
        cxpr_program_free(entry->program);
        entry->program = cxpr_compile(entry->ast, evaluator->registry, err);
        if (!entry->program) {
            evaluator->compiled = false;
            return false;
        }
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

    for (size_t i = 0; i < evaluator->count; i++) {
        evaluator->expressions[i].evaluated = false;
        cxpr_expression_result_dispose(&evaluator->expressions[i].result);
    }

    for (size_t i = 0; i < evaluator->eval_order_count; i++) {
        size_t idx = evaluator->eval_order[i];
        cxpr_expression_entry* entry = &evaluator->expressions[idx];
        cxpr_error eval_err = {0};
        cxpr_value value = {0};

        if (entry->program) {
            (void)cxpr_eval_program(entry->program, ctx, evaluator->registry, &value, &eval_err);
        } else {
            (void)cxpr_eval_ast(entry->ast, ctx, evaluator->registry, &value, &eval_err);
        }
        if (eval_err.code != CXPR_OK) {
            cxpr_context_set_expression_scope(ctx, previous_scope);
            if (err) *err = eval_err;
            return;
        }

        if (value.type != CXPR_VALUE_NUMBER &&
            value.type != CXPR_VALUE_BOOL &&
            value.type != CXPR_VALUE_STRUCT) {
            cxpr_context_set_expression_scope(ctx, previous_scope);
            if (err) {
                err->code = CXPR_ERR_TYPE_MISMATCH;
                err->message = "Expression result must be double, bool, or struct";
            }
            return;
        }

        entry->result = cxpr_expression_result_clone(&value, &eval_err);
        if (eval_err.code != CXPR_OK) {
            cxpr_context_set_expression_scope(ctx, previous_scope);
            if (err) *err = eval_err;
            return;
        }
        entry->evaluated = true;
    }

    cxpr_context_set_expression_scope(ctx, previous_scope);
    if (err) err->code = CXPR_OK;
}

bool cxpr_expression_compile(cxpr_evaluator* evaluator, cxpr_error* err) {
    return cxpr_evaluator_compile(evaluator, err);
}

void cxpr_expression_eval_all(cxpr_evaluator* evaluator, cxpr_context* ctx, cxpr_error* err) {
    cxpr_evaluator_eval(evaluator, ctx, err);
}
