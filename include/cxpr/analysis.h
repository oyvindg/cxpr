/**
 * @file analysis.h
 * @brief Public AST analysis API for cxpr.
 */

#ifndef CXPR_ANALYSIS_H
#define CXPR_ANALYSIS_H

#include <cxpr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CXPR_EXPR_UNKNOWN = 0,
    CXPR_EXPR_BOOL,
    CXPR_EXPR_NUMBER,
    CXPR_EXPR_STRUCT
} cxpr_expr_type;

typedef struct {
    cxpr_expr_type result_type;          /**< Best-effort root result type of the expression. */
    bool is_constant;                    /**< True if the expression depends on no runtime inputs or parameters. */
    bool is_predicate;                   /**< True if the root expression evaluates to a boolean predicate. */
    bool uses_variables;                 /**< True if plain identifier/context lookups such as `rsi` are used. */
    bool uses_parameters;                /**< True if `$param` lookups are used. */
    bool uses_functions;                 /**< True if function or producer calls appear in the AST. */
    bool uses_expressions;               /**< True if semantic analysis resolved at least one registry-defined expression. */
    bool uses_field_access;              /**< True if dotted or producer-style field access appears in the AST. */
    bool can_short_circuit;              /**< True if evaluation may short-circuit (`and`, `or`, ternary). */
    unsigned node_count;                 /**< Total number of AST nodes in the expression tree. */
    unsigned max_depth;                  /**< Maximum AST depth, with the root counted as depth 1. */
    unsigned max_lookback_depth;         /**< Maximum accumulated literal lookback offset (`x[n]`) in the AST. */
    size_t reference_count;              /**< Unique runtime references used by the AST: plain identifiers and full field paths. */
    size_t function_count;               /**< Unique function or producer names referenced by the AST. */
    size_t parameter_count;              /**< Unique `$param` names referenced by the AST. */
    size_t field_path_count;             /**< Unique dotted or field-style reference paths. */
    bool has_unknown_functions;          /**< True if registry-backed analysis found unresolved calls. */
    const char* first_unknown_function;  /**< First unresolved function/producer name, or NULL if none. */
    bool has_unsupported_codegen_nodes;  /**< True if the AST contains nodes with no standalone C codegen form. */
    const char* first_unsupported_codegen_node; /**< First unsupported codegen node kind, or NULL if none. */
} cxpr_analysis;

/**
 * @brief Perform structural and registry-backed semantic analysis on an AST.
 * @param ast AST to inspect.
 * @param reg Optional registry used to resolve functions and expressions.
 * @param out_analysis Output analysis struct to fill.
 * @param err Optional error output.
 * @return True on success, false on semantic-analysis failure.
 */
bool cxpr_analyze(const cxpr_ast* ast, const cxpr_registry* reg,
                  cxpr_analysis* out_analysis, cxpr_error* err);
/**
 * @brief Parse and analyze one expression string in a single call.
 * @param expression NUL-terminated expression source.
 * @param reg Optional registry used to resolve functions and expressions.
 * @param out_analysis Output analysis struct to fill.
 * @param err Optional error output.
 * @return True on success, false on parse or semantic-analysis failure.
 */
bool cxpr_analyze_expr(const char* expression, const cxpr_registry* reg,
                       cxpr_analysis* out_analysis, cxpr_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_ANALYSIS_H */
