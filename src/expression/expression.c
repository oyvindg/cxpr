/**
 * @file expression.c
 * @brief Named expression registration and query helpers for cxpr.
 */

#include "../ast/internal.h"
#include "internal.h"
#include <stdio.h>

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
                                      size_t node, int* visited,
                                      size_t depth, size_t max_depth) {
    if (depth > max_depth) return true;
    visited[node] = 1;

    if (cxpr_expression_depends_on(evaluator, node, node)) return true;

    for (size_t i = 0; i < evaluator->count; i++) {
        if (i == node) continue;
        if (!cxpr_expression_depends_on(evaluator, node, i)) continue;

        if (visited[i] == 1) return true;
        if (visited[i] == 0
            && cxpr_expression_dfs_cycle(evaluator, i, visited, depth + 1, max_depth)) {
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
        if (visited[i] == 0 && cxpr_expression_dfs_cycle(evaluator, i, visited, 0, n)) {
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

/* ═══════════════════════════════════════════════════════════════════════════
 * Error string helper
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Convert an error code to a human-readable string.
 * @param[in] code Error code
 * @return Static string describing the error
 */
const char* cxpr_error_string(cxpr_error_code code) {
    switch (code) {
    case CXPR_OK:                      return "OK";
    case CXPR_ERR_SYNTAX:              return "Syntax error";
    case CXPR_ERR_UNKNOWN_IDENTIFIER:  return "Unknown identifier";
    case CXPR_ERR_UNKNOWN_FUNCTION:    return "Unknown function";
    case CXPR_ERR_WRONG_ARITY:         return "Wrong number of arguments";
    case CXPR_ERR_DIVISION_BY_ZERO:    return "Division by zero";
    case CXPR_ERR_CIRCULAR_DEPENDENCY: return "Circular dependency";
    case CXPR_ERR_OUT_OF_MEMORY:       return "Out of memory";
    default:                         return "Unknown error";
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

static void cxpr_expression_entry_reset(cxpr_expression_entry* entry) {
    cxpr_expression_result_dispose(&entry->result);
    free(entry->name);
    free(entry->expression);
    cxpr_ast_free(entry->ast);
    cxpr_program_free(entry->program);
    memset(entry, 0, sizeof(*entry));
}

static void cxpr_evaluator_truncate(cxpr_evaluator* evaluator, size_t count) {
    if (!evaluator || count >= evaluator->count) return;
    for (size_t i = count; i < evaluator->count; i++) {
        cxpr_expression_entry_reset(&evaluator->expressions[i]);
    }
    evaluator->count = count;
    evaluator->compiled = false;
}

/**
 * @brief Add a named expression to the evaluator.
 * @param[in] evaluator Expression evaluator
 * @param[in] name     Expression name (used as variable for dependents)
 * @param[in] expression Expression string to parse
 * @param[out] err     Error output (can be NULL)
 * @return true on success, false on parse error
 */
bool cxpr_expression_add(cxpr_evaluator* evaluator, const char* name,
                         const char* expression, cxpr_error* err) {
    if (!evaluator || !name || !expression) {
        if (err) { err->code = CXPR_ERR_SYNTAX; err->message = "NULL argument"; }
        return false;
    }

    /* Parse the expression */
    cxpr_error parse_err = {0};
    cxpr_ast* ast = cxpr_parse(evaluator->parser, expression, &parse_err);
    if (!ast) {
        if (err) *err = parse_err;
        return false;
    }

    /* Grow if needed */
    if (!cxpr_evaluator_reserve_for_entry(evaluator)) {
        cxpr_ast_free(ast);
        if (err) { err->code = CXPR_ERR_OUT_OF_MEMORY; err->message = "Out of memory"; }
        return false;
    }

    cxpr_expression_entry* entry = &evaluator->expressions[evaluator->count++];
    entry->name = cxpr_strdup(name);
    entry->expression = cxpr_strdup(expression);
    entry->ast = ast;
    entry->program = NULL;
    entry->result = cxpr_fv_double(0.0);
    entry->evaluated = false;

    /* Invalidate compilation */
    evaluator->compiled = false;

    if (err) err->code = CXPR_OK;
    return true;
}

bool cxpr_expressions_add(cxpr_evaluator* evaluator, const cxpr_expression_def* defs,
                          size_t count, cxpr_error* err) {
    if (!evaluator || (!defs && count > 0)) {
        if (err) { err->code = CXPR_ERR_SYNTAX; err->message = "NULL argument"; }
        return false;
    }

    const size_t start_count = evaluator->count;
    for (size_t i = 0; i < count; i++) {
        if (!cxpr_expression_add(evaluator, defs[i].name, defs[i].expression, err)) {
            cxpr_evaluator_truncate(evaluator, start_count);
            return false;
        }
    }

    if (err) err->code = CXPR_OK;
    return true;
}

bool cxpr_analyze_expressions(const cxpr_expression_def* defs, size_t count,
                           const cxpr_registry* reg,
                           cxpr_analysis* out_analysis,
                           size_t* out_eval_order,
                           cxpr_error* err) {
    cxpr_evaluator* evaluator;
    bool ok;

    if (err) *err = (cxpr_error){0};
    if ((!defs && count > 0) || (!out_analysis && count > 0)) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "NULL argument";
        }
        return false;
    }

    if (count == 0) return true;

    evaluator = cxpr_evaluator_new(reg);
    if (!evaluator) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return false;
    }

    ok = cxpr_expressions_add(evaluator, defs, count, err);
    if (!ok) {
        cxpr_evaluator_free(evaluator);
        return false;
    }

    ok = cxpr_expression_topo_sort(evaluator, err);
    if (!ok) {
        cxpr_evaluator_free(evaluator);
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        const char* refs[256];
        size_t nrefs;

        ok = cxpr_analyze(evaluator->expressions[i].ast, reg, &out_analysis[i], err);
        if (!ok) {
            cxpr_evaluator_free(evaluator);
            return false;
        }

        nrefs = cxpr_ast_references(evaluator->expressions[i].ast, refs, 256);
        for (size_t r = 0; r < nrefs && r < 256; r++) {
            for (size_t d = 0; d < count; d++) {
                if (d == i) continue;
                if (cxpr_expression_reference_matches_name(refs[r], defs[d].name)) {
                    out_analysis[i].uses_expressions = true;
                    break;
                }
            }
            if (out_analysis[i].uses_expressions) break;
        }
    }

    if (out_eval_order) {
        for (size_t i = 0; i < evaluator->eval_order_count; i++) {
            out_eval_order[i] = evaluator->eval_order[i];
        }
    }

    cxpr_evaluator_free(evaluator);
    if (err) err->code = CXPR_OK;
    return true;
}

/**
 * @brief Get the result of a named expression after evaluation.
 * @param[in] evaluator  Expression evaluator
 * @param[in] name    Expression name
 * @param[out] found  Set to true if found (can be NULL)
 * @return Expression result, or cxpr_fv_double(0.0) if not found/evaluated
 */
cxpr_value cxpr_expression_get(const cxpr_evaluator* evaluator, const char* name, bool* found) {
    return cxpr_expression_lookup_typed_result(evaluator, name, found);
}

double cxpr_expression_get_double(const cxpr_evaluator* evaluator, const char* name, bool* found) {
    cxpr_value value = cxpr_expression_get(evaluator, name, found);
    if (found && !*found) return 0.0;
    if (value.type != CXPR_VALUE_NUMBER) {
        if (found) *found = false;
        return 0.0;
    }
    return value.d;
}

bool cxpr_expression_get_bool(const cxpr_evaluator* evaluator, const char* name, bool* found) {
    cxpr_value value = cxpr_expression_get(evaluator, name, found);
    if (found && !*found) return false;
    if (value.type != CXPR_VALUE_BOOL) {
        if (found) *found = false;
        return false;
    }
    return value.b;
}

/**
 * @brief Get the evaluation order after compilation.
 *
 * codegen uses this to generate code that evaluates expressions
 * in the correct dependency order.
 *
 * @param[in] evaluator  Compiled expression evaluator
 * @param[out] names     Output array for expression names (caller provides)
 * @param[in] max_names  Maximum names to return
 * @return Total number of expressions in evaluation order
 */
size_t cxpr_expression_eval_order(const cxpr_evaluator* evaluator,
                                  const char** names, size_t max_names) {
    if (!evaluator || !evaluator->compiled) return 0;

    size_t count = 0;
    for (size_t i = 0; i < evaluator->eval_order_count && count < max_names; i++) {
        names[count++] = evaluator->expressions[evaluator->eval_order[i]].name;
    }
    return evaluator->eval_order_count;
}
