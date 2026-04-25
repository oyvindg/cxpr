/**
 * @file expression_internal.h
 * @brief Internal helpers shared by expression-related cxpr modules.
 */

#ifndef CXPR_EXPRESSION_INTERNAL_H
#define CXPR_EXPRESSION_INTERNAL_H

#include "../core.h"

/**
 * @brief A single expression entry in the evaluator.
 */
typedef struct {
    char* name;               /**< Expression name, owned */
    char* expression;         /**< Original expression string, owned */
    cxpr_ast* ast;            /**< Parsed AST (NULL until compiled) */
    cxpr_program* program;    /**< Compiled program cache (NULL until compiled) */
    cxpr_value result;        /**< Evaluation result */
    bool evaluated;
} cxpr_expression_entry;

/** @brief Initial expression storage capacity for new evaluators. */
#define CXPR_EXPRESSION_INITIAL_CAPACITY 32

/** @brief Internal owned evaluator state backing the public `cxpr_evaluator` handle. */
struct cxpr_evaluator {
    cxpr_expression_entry* expressions;
    size_t capacity;
    size_t count;
    size_t* eval_order;       /**< Indices in topological order */
    size_t eval_order_count;
    bool compiled;
    const cxpr_registry* registry; /**< Borrowed reference */
    cxpr_parser* parser;           /**< Internal parser for expression expressions */
};

/** @brief Check whether one runtime reference resolves to the given expression name. */
bool cxpr_expression_reference_matches_name(const char* reference, const char* name);
/** @brief Release any owned storage held by one cached expression result. */
void cxpr_expression_result_dispose(cxpr_value* value);
/** @brief Deep-clone one cached expression result. */
cxpr_value cxpr_expression_result_clone(const cxpr_value* value, cxpr_error* err);
/** @brief Recompute evaluator dependency order using topological sorting. */
bool cxpr_expression_topo_sort(cxpr_evaluator* evaluator, cxpr_error* err);
/** @brief Ensure space for at least one more expression entry. */
bool cxpr_evaluator_reserve_for_entry(cxpr_evaluator* evaluator);
/** @brief Look up one previously evaluated expression result by name. */
cxpr_value cxpr_expression_lookup_typed_result(const cxpr_evaluator* evaluator,
                                               const char* name, bool* found);

#endif
