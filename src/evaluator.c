/**
 * @file evaluator.c
 * @brief Evaluator lifecycle and execution for cxpr.
 */

#include "internal.h"

static size_t cxpr_expression_find(const cxpr_evaluator* evaluator, const char* name) {
    for (size_t i = 0; i < evaluator->count; i++) {
        if (strcmp(evaluator->expressions[i].name, name) == 0) {
            return i;
        }
    }
    return (size_t)-1;
}

bool cxpr_expression_reference_matches_name(const char* reference, const char* name) {
    size_t name_len;

    if (!reference || !name) return false;
    if (strcmp(reference, name) == 0) return true;

    name_len = strlen(name);
    return strncmp(reference, name, name_len) == 0 && reference[name_len] == '.';
}

void cxpr_expression_result_dispose(cxpr_value* value) {
    if (!value) return;
    if (value->type == CXPR_VALUE_STRUCT) {
        cxpr_struct_value_free(value->s);
    }
    *value = cxpr_fv_double(0.0);
}

cxpr_value cxpr_expression_result_clone(const cxpr_value* value, cxpr_error* err) {
    cxpr_struct_value* copy;

    if (!value) return cxpr_fv_double(0.0);

    switch (value->type) {
    case CXPR_VALUE_NUMBER:
        return cxpr_fv_double(value->d);
    case CXPR_VALUE_BOOL:
        return cxpr_fv_bool(value->b);
    case CXPR_VALUE_STRUCT:
        copy = cxpr_struct_value_new(
            value->s ? (const char* const*)value->s->field_names : NULL,
            value->s ? value->s->field_values : NULL,
            value->s ? value->s->field_count : 0);
        if (!copy && value->s && err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return cxpr_fv_struct(copy);
    default:
        return cxpr_fv_double(0.0);
    }
}

cxpr_value cxpr_expression_lookup_typed_result(const cxpr_evaluator* evaluator,
                                               const char* name, bool* found) {
    size_t idx;

    if (!evaluator || !name) {
        if (found) *found = false;
        return cxpr_fv_double(0.0);
    }

    idx = cxpr_expression_find(evaluator, name);
    if (idx == (size_t)-1 || !evaluator->expressions[idx].evaluated) {
        if (found) *found = false;
        return cxpr_fv_double(0.0);
    }

    if (found) *found = true;
    return evaluator->expressions[idx].result;
}

static void cxpr_evaluator_grow(cxpr_evaluator* evaluator) {
    size_t new_cap = evaluator->capacity * 2;
    cxpr_expression_entry* new_expressions =
        (cxpr_expression_entry*)calloc(new_cap, sizeof(cxpr_expression_entry));
    if (!new_expressions) return;
    memcpy(new_expressions, evaluator->expressions,
           evaluator->count * sizeof(cxpr_expression_entry));
    free(evaluator->expressions);
    evaluator->expressions = new_expressions;
    evaluator->capacity = new_cap;
}

static bool cxpr_expression_depends_on(const cxpr_evaluator* evaluator, size_t expression_idx,
                                       size_t dep_idx) {
    const cxpr_expression_entry* expression = &evaluator->expressions[expression_idx];
    if (!expression->ast) return false;

    const char* dep_name = evaluator->expressions[dep_idx].name;
    const char* refs[256];
    size_t nrefs = cxpr_ast_references(expression->ast, refs, 256);

    for (size_t i = 0; i < nrefs && i < 256; i++) {
        if (cxpr_expression_reference_matches_name(refs[i], dep_name)) return true;
    }
    return false;
}

static bool cxpr_expression_dfs_cycle(const cxpr_evaluator* evaluator,
                                      size_t node, int* visited) {
    visited[node] = 1;

    if (cxpr_expression_depends_on(evaluator, node, node)) return true;

    for (size_t i = 0; i < evaluator->count; i++) {
        if (i == node) continue;
        if (!cxpr_expression_depends_on(evaluator, node, i)) continue;

        if (visited[i] == 1) return true;
        if (visited[i] == 0 && cxpr_expression_dfs_cycle(evaluator, i, visited)) {
            return true;
        }
    }

    visited[node] = 2;
    return false;
}

bool cxpr_expression_topo_sort(cxpr_evaluator* evaluator, cxpr_error* err) {
    size_t n = evaluator->count;
    if (n == 0) return true;

    size_t* in_degree = (size_t*)calloc(n, sizeof(size_t));
    if (!in_degree) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return false;
    }

    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) {
            if (i != j && cxpr_expression_depends_on(evaluator, i, j)) {
                in_degree[i]++;
            }
        }
    }

    int* visited = (int*)calloc(n, sizeof(int));
    if (!visited) {
        free(in_degree);
        return false;
    }

    for (size_t i = 0; i < n; i++) {
        if (visited[i] == 0 && cxpr_expression_dfs_cycle(evaluator, i, visited)) {
            free(in_degree);
            free(visited);
            if (err) {
                err->code = CXPR_ERR_CIRCULAR_DEPENDENCY;
                err->message = "Circular dependency detected";
            }
            return false;
        }
    }
    free(visited);

    size_t* queue = (size_t*)malloc(n * sizeof(size_t));
    size_t* order = (size_t*)malloc(n * sizeof(size_t));
    if (!queue || !order) {
        free(in_degree);
        free(queue);
        free(order);
        return false;
    }

    size_t front = 0;
    size_t back = 0;
    size_t order_count = 0;

    for (size_t i = 0; i < n; i++) {
        if (in_degree[i] == 0) {
            queue[back++] = i;
        }
    }

    while (front < back) {
        size_t node = queue[front++];
        order[order_count++] = node;

        for (size_t i = 0; i < n; i++) {
            if (i != node && cxpr_expression_depends_on(evaluator, i, node)) {
                in_degree[i]--;
                if (in_degree[i] == 0) {
                    queue[back++] = i;
                }
            }
        }
    }

    free(in_degree);
    free(queue);

    if (order_count != n) {
        free(order);
        if (err) {
            err->code = CXPR_ERR_CIRCULAR_DEPENDENCY;
            err->message = "Circular dependency detected";
        }
        return false;
    }

    free(evaluator->eval_order);
    evaluator->eval_order = order;
    evaluator->eval_order_count = order_count;
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

void cxpr_evaluator_reserve_for_entry(cxpr_evaluator* evaluator) {
    if (evaluator->count >= evaluator->capacity) {
        cxpr_evaluator_grow(evaluator);
    }
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
