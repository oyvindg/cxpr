/**
 * @file formula.c
 * @brief Formula engine with dependency resolution.
 *
 * Manages multiple named formulas, resolves dependencies via
 * topological sort (Kahn's algorithm), and detects circular
 * dependencies (DFS). Used by codegen for build-time analysis.
 */

#include "internal.h"
#include <stdio.h>

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
 * Formula engine helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Find a formula by name.
 * @return Index of the formula, or (size_t)-1 if not found.
 */
static size_t cxpr_formula_find(const cxpr_formula_engine* engine, const char* name) {
    for (size_t i = 0; i < engine->count; i++) {
        if (strcmp(engine->formulas[i].name, name) == 0) {
            return i;
        }
    }
    return (size_t)-1;
}

static void cxpr_formula_result_dispose(cxpr_field_value* value) {
    if (!value) return;
    if (value->type == CXPR_FIELD_STRUCT) {
        cxpr_struct_value_free(value->s);
    }
    *value = cxpr_fv_double(0.0);
}

static cxpr_field_value cxpr_formula_result_clone(const cxpr_field_value* value,
                                                  cxpr_error* err) {
    cxpr_struct_value* copy;

    if (!value) return cxpr_fv_double(0.0);

    switch (value->type) {
    case CXPR_FIELD_DOUBLE:
        return cxpr_fv_double(value->d);
    case CXPR_FIELD_BOOL:
        return cxpr_fv_bool(value->b);
    case CXPR_FIELD_STRUCT:
        copy = cxpr_struct_value_new(
            value->s ? (const char* const*)value->s->field_names : NULL,
            value->s ? value->s->field_values : NULL,
            value->s ? value->s->field_count : 0);
        if (!copy && value->s) {
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
        }
        return cxpr_fv_struct(copy);
    default:
        return cxpr_fv_double(0.0);
    }
}

cxpr_field_value cxpr_formula_lookup_typed_result(const cxpr_formula_engine* engine,
                                                  const char* name, bool* found) {
    size_t idx;

    if (!engine || !name) {
        if (found) *found = false;
        return cxpr_fv_double(0.0);
    }

    idx = cxpr_formula_find(engine, name);
    if (idx == (size_t)-1 || !engine->formulas[idx].evaluated) {
        if (found) *found = false;
        return cxpr_fv_double(0.0);
    }

    if (found) *found = true;
    return engine->formulas[idx].result;
}

/** @brief Double the formula engine's capacity. */
static void cxpr_formula_engine_grow(cxpr_formula_engine* engine) {
    size_t new_cap = engine->capacity * 2;
    cxpr_formula_entry* new_formulas = (cxpr_formula_entry*)calloc(new_cap, sizeof(cxpr_formula_entry));
    if (!new_formulas) return;
    memcpy(new_formulas, engine->formulas, engine->count * sizeof(cxpr_formula_entry));
    free(engine->formulas);
    engine->formulas = new_formulas;
    engine->capacity = new_cap;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Topological sort (Kahn's algorithm) with DFS cycle detection
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Check if a formula references another formula by name.
 */
static bool cxpr_formula_depends_on(const cxpr_formula_engine* engine, size_t formula_idx,
                                   size_t dep_idx) {
    const cxpr_formula_entry* f = &engine->formulas[formula_idx];
    if (!f->ast) return false;

    const char* dep_name = engine->formulas[dep_idx].name;
    const char* refs[256];
    size_t nrefs = cxpr_ast_references(f->ast, refs, 256);

    for (size_t i = 0; i < nrefs && i < 256; i++) {
        if (strcmp(refs[i], dep_name) == 0) return true;
    }
    return false;
}

/**
 * @brief DFS cycle detection.
 * @param visited 0=unvisited, 1=in-progress, 2=done
 * @return true if cycle detected.
 */
static bool cxpr_formula_dfs_cycle(const cxpr_formula_engine* engine,
                                  size_t node, int* visited) {
    visited[node] = 1; /* in-progress */

    /* Check self-reference */
    if (cxpr_formula_depends_on(engine, node, node)) return true;

    for (size_t i = 0; i < engine->count; i++) {
        if (i == node) continue;
        if (!cxpr_formula_depends_on(engine, node, i)) continue;

        if (visited[i] == 1) return true;  /* cycle: back-edge */
        if (visited[i] == 0) {
            if (cxpr_formula_dfs_cycle(engine, i, visited)) return true;
        }
    }

    visited[node] = 2; /* done */
    return false;
}

/**
 * @brief Kahn's algorithm for topological sort.
 * @return true on success, false on cycle.
 */
static bool cxpr_formula_topo_sort(cxpr_formula_engine* engine, cxpr_error* err) {
    size_t n = engine->count;
    if (n == 0) return true;

    /* Build adjacency: in_degree[i] = number of formulas that i depends on */
    size_t* in_degree = (size_t*)calloc(n, sizeof(size_t));
    if (!in_degree) {
        if (err) { err->code = CXPR_ERR_OUT_OF_MEMORY; err->message = "Out of memory"; }
        return false;
    }

    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) {
            if (i != j && cxpr_formula_depends_on(engine, i, j)) {
                in_degree[i]++;
            }
        }
    }

    /* DFS cycle detection first */
    int* visited = (int*)calloc(n, sizeof(int));
    if (!visited) { free(in_degree); return false; }

    for (size_t i = 0; i < n; i++) {
        if (visited[i] == 0) {
            if (cxpr_formula_dfs_cycle(engine, i, visited)) {
                free(in_degree);
                free(visited);
                if (err) {
                    err->code = CXPR_ERR_CIRCULAR_DEPENDENCY;
                    err->message = "Circular dependency detected";
                }
                return false;
            }
        }
    }
    free(visited);

    /* Kahn's algorithm */
    size_t* queue = (size_t*)malloc(n * sizeof(size_t));
    size_t* order = (size_t*)malloc(n * sizeof(size_t));
    if (!queue || !order) {
        free(in_degree); free(queue); free(order);
        return false;
    }

    size_t front = 0, back = 0, order_count = 0;

    /* Enqueue nodes with in-degree 0 */
    for (size_t i = 0; i < n; i++) {
        if (in_degree[i] == 0) {
            queue[back++] = i;
        }
    }

    while (front < back) {
        size_t node = queue[front++];
        order[order_count++] = node;

        /* For each formula that depends on this node, decrease in-degree */
        for (size_t i = 0; i < n; i++) {
            if (i != node && cxpr_formula_depends_on(engine, i, node)) {
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
        /* Should not happen since we already checked for cycles */
        free(order);
        if (err) {
            err->code = CXPR_ERR_CIRCULAR_DEPENDENCY;
            err->message = "Circular dependency detected";
        }
        return false;
    }

    /* Store evaluation order */
    free(engine->eval_order);
    engine->eval_order = order;
    engine->eval_order_count = order_count;
    return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Create a new formula engine.
 * @param[in] reg Function registry for evaluation (borrowed, not owned)
 * @return Engine handle, or NULL on allocation failure
 */
cxpr_formula_engine* cxpr_formula_engine_new(const cxpr_registry* reg) {
    cxpr_formula_engine* engine = (cxpr_formula_engine*)calloc(1, sizeof(cxpr_formula_engine));
    if (!engine) return NULL;
    engine->capacity = CXPR_FORMULA_INITIAL_CAPACITY;
    engine->formulas = (cxpr_formula_entry*)calloc(engine->capacity, sizeof(cxpr_formula_entry));
    if (!engine->formulas) { free(engine); return NULL; }
    engine->registry = reg;
    engine->parser = cxpr_parser_new();
    if (!engine->parser) {
        free(engine->formulas);
        free(engine);
        return NULL;
    }
    return engine;
}

/**
 * @brief Free a formula engine and all owned formulas/ASTs.
 * @param engine Engine to free (NULL-safe)
 */
void cxpr_formula_engine_free(cxpr_formula_engine* engine) {
    if (!engine) return;
    for (size_t i = 0; i < engine->count; i++) {
        cxpr_formula_result_dispose(&engine->formulas[i].result);
        free(engine->formulas[i].name);
        free(engine->formulas[i].expression);
        cxpr_ast_free(engine->formulas[i].ast);
        cxpr_program_free(engine->formulas[i].program);
    }
    free(engine->formulas);
    free(engine->eval_order);
    cxpr_parser_free(engine->parser);
    free(engine);
}

static void cxpr_formula_entry_reset(cxpr_formula_entry* entry) {
    cxpr_formula_result_dispose(&entry->result);
    free(entry->name);
    free(entry->expression);
    cxpr_ast_free(entry->ast);
    cxpr_program_free(entry->program);
    memset(entry, 0, sizeof(*entry));
}

static void cxpr_formula_engine_truncate(cxpr_formula_engine* engine, size_t count) {
    if (!engine || count >= engine->count) return;
    for (size_t i = count; i < engine->count; i++) {
        cxpr_formula_entry_reset(&engine->formulas[i]);
    }
    engine->count = count;
    engine->compiled = false;
}

/**
 * @brief Add a named formula to the engine.
 * @param[in] engine   Formula engine
 * @param[in] name     Formula name (used as variable for dependents)
 * @param[in] expression Expression string to parse
 * @param[out] err     Error output (can be NULL)
 * @return true on success, false on parse error
 */
bool cxpr_formula_add(cxpr_formula_engine* engine, const char* name,
                    const char* expression, cxpr_error* err) {
    if (!engine || !name || !expression) {
        if (err) { err->code = CXPR_ERR_SYNTAX; err->message = "NULL argument"; }
        return false;
    }

    /* Parse the expression */
    cxpr_error parse_err = {0};
    cxpr_ast* ast = cxpr_parse(engine->parser, expression, &parse_err);
    if (!ast) {
        if (err) *err = parse_err;
        return false;
    }

    /* Grow if needed */
    if (engine->count >= engine->capacity) {
        cxpr_formula_engine_grow(engine);
    }

    cxpr_formula_entry* entry = &engine->formulas[engine->count++];
    entry->name = cxpr_strdup(name);
    entry->expression = cxpr_strdup(expression);
    entry->ast = ast;
    entry->program = NULL;
    entry->result = cxpr_fv_double(0.0);
    entry->evaluated = false;

    /* Invalidate compilation */
    engine->compiled = false;

    if (err) err->code = CXPR_OK;
    return true;
}

bool cxpr_formulas_add(cxpr_formula_engine* engine, const cxpr_formula_def* defs,
                       size_t count, cxpr_error* err) {
    if (!engine || (!defs && count > 0)) {
        if (err) { err->code = CXPR_ERR_SYNTAX; err->message = "NULL argument"; }
        return false;
    }

    const size_t start_count = engine->count;
    for (size_t i = 0; i < count; i++) {
        if (!cxpr_formula_add(engine, defs[i].name, defs[i].expression, err)) {
            cxpr_formula_engine_truncate(engine, start_count);
            return false;
        }
    }

    if (err) err->code = CXPR_OK;
    return true;
}

/**
 * @brief Compile the formula engine (resolve dependencies via topological sort).
 * @param[in] engine   Formula engine
 * @param[out] err     Error output (can be NULL)
 * @return true on success, false on circular dependency
 */
bool cxpr_formula_compile(cxpr_formula_engine* engine, cxpr_error* err) {
    if (!engine) {
        if (err) { err->code = CXPR_ERR_SYNTAX; err->message = "NULL engine"; }
        return false;
    }

    bool ok = cxpr_formula_topo_sort(engine, err);
    if (!ok) return false;

    for (size_t i = 0; i < engine->count; i++) {
        cxpr_formula_entry* f = &engine->formulas[i];
        cxpr_program_free(f->program);
        f->program = cxpr_compile(f->ast, engine->registry, err);
        if (!f->program) {
            engine->compiled = false;
            return false;
        }
    }

    engine->compiled = true;
    if (err) err->code = CXPR_OK;
    return true;
}

/**
 * @brief Evaluate all formulas in dependency order, storing results in context.
 * @param[in] engine   Compiled formula engine
 * @param[in,out] ctx  Context (results are written as variables)
 * @param[out] err     Error output (can be NULL)
 */
void cxpr_formula_eval_all(cxpr_formula_engine* engine, cxpr_context* ctx, cxpr_error* err) {
    const cxpr_formula_engine* previous_scope;

    if (!engine || !ctx) {
        if (err) { err->code = CXPR_ERR_SYNTAX; err->message = "NULL argument"; }
        return;
    }

    if (!engine->compiled) {
        if (err) { err->code = CXPR_ERR_SYNTAX; err->message = "Engine not compiled"; }
        return;
    }

    previous_scope = ctx->formula_scope;
    cxpr_context_set_formula_scope(ctx, engine);

    /* Reset evaluation state */
    for (size_t i = 0; i < engine->count; i++) {
        engine->formulas[i].evaluated = false;
        cxpr_formula_result_dispose(&engine->formulas[i].result);
    }

    /* Evaluate in topological order */
    for (size_t o = 0; o < engine->eval_order_count; o++) {
        size_t idx = engine->eval_order[o];
        cxpr_formula_entry* f = &engine->formulas[idx];

        cxpr_error eval_err = {0};
        cxpr_field_value value = {0};
        if (f->program) {
            value = cxpr_ir_eval(f->program, ctx, engine->registry, &eval_err);
        } else {
            value = cxpr_ast_eval(f->ast, ctx, engine->registry, &eval_err);
        }
        if (eval_err.code != CXPR_OK) {
            cxpr_context_set_formula_scope(ctx, previous_scope);
            if (err) *err = eval_err;
            return;
        }

        if (value.type != CXPR_FIELD_DOUBLE &&
            value.type != CXPR_FIELD_BOOL &&
            value.type != CXPR_FIELD_STRUCT) {
            cxpr_context_set_formula_scope(ctx, previous_scope);
            if (err) {
                err->code = CXPR_ERR_TYPE_MISMATCH;
                err->message = "Formula result must be double, bool, or struct";
            }
            return;
        }
        f->result = cxpr_formula_result_clone(&value, &eval_err);
        if (eval_err.code != CXPR_OK) {
            cxpr_context_set_formula_scope(ctx, previous_scope);
            if (err) *err = eval_err;
            return;
        }
        f->evaluated = true;
    }

    cxpr_context_set_formula_scope(ctx, previous_scope);
    if (err) err->code = CXPR_OK;
}

/**
 * @brief Get the result of a named formula after evaluation.
 * @param[in] engine  Formula engine
 * @param[in] name    Formula name
 * @param[out] found  Set to true if found (can be NULL)
 * @return Formula result, or cxpr_fv_double(0.0) if not found/evaluated
 */
cxpr_field_value cxpr_formula_get(const cxpr_formula_engine* engine, const char* name, bool* found) {
    return cxpr_formula_lookup_typed_result(engine, name, found);
}

double cxpr_formula_get_double(const cxpr_formula_engine* engine, const char* name, bool* found) {
    cxpr_field_value value = cxpr_formula_get(engine, name, found);
    if (found && !*found) return 0.0;
    if (value.type != CXPR_FIELD_DOUBLE) {
        if (found) *found = false;
        return 0.0;
    }
    return value.d;
}

bool cxpr_formula_get_bool(const cxpr_formula_engine* engine, const char* name, bool* found) {
    cxpr_field_value value = cxpr_formula_get(engine, name, found);
    if (found && !*found) return false;
    if (value.type != CXPR_FIELD_BOOL) {
        if (found) *found = false;
        return false;
    }
    return value.b;
}

/**
 * @brief Get the evaluation order after compilation.
 *
 * codegen uses this to generate code that evaluates formulas
 * in the correct dependency order.
 *
 * @param[in] engine     Compiled formula engine
 * @param[out] names     Output array for formula names (caller provides)
 * @param[in] max_names  Maximum names to return
 * @return Total number of formulas in evaluation order
 */
size_t cxpr_formula_eval_order(const cxpr_formula_engine* engine,
                              const char** names, size_t max_names) {
    if (!engine || !engine->compiled) return 0;

    size_t count = 0;
    for (size_t i = 0; i < engine->eval_order_count && count < max_names; i++) {
        names[count++] = engine->formulas[engine->eval_order[i]].name;
    }
    return engine->eval_order_count;
}
