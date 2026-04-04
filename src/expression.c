/**
 * @file expression.c
 * @brief Named expression registration and query helpers for cxpr.
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
    cxpr_evaluator_reserve_for_entry(evaluator);

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
