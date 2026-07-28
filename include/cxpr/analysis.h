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

typedef enum {
    CXPR_CALL_SITE_FUNCTION = 1,
    CXPR_CALL_SITE_PRODUCER = 2,
} cxpr_call_site_kind;

/**
 * @brief One borrowed static named string argument discovered at a call site.
 *
 * CXPR reports syntax only. Hosts decide whether names such as `timeframe`,
 * `region`, or `warehouse` carry provider-specific scope semantics.
 */
typedef struct {
    cxpr_call_site_kind call_kind;
    const cxpr_expr_ast* call;
    const char* callee;
    const char* argument;
    const char* value;
} cxpr_static_named_string_arg;

/**
 * Return non-zero to continue traversal, or zero to stop.
 */
typedef int (*cxpr_static_named_string_arg_visitor)(
    const cxpr_static_named_string_arg* arg,
    void* userdata);

/**
 * @brief Visit static named string arguments in every nested call.
 *
 * Values passed to the visitor are borrowed from @p ast. No provider registry
 * is required, so imported/custom calls are reported as well as builtins.
 */
bool cxpr_visit_static_named_string_args(
    const cxpr_expr_ast* ast,
    cxpr_static_named_string_arg_visitor visitor,
    void* userdata);

/**
 * @brief Perform structural and registry-backed semantic analysis on an AST.
 * @param ast AST to inspect.
 * @param reg Optional registry used to resolve functions and expressions.
 * @param out_analysis Output analysis struct to fill.
 * @param err Optional error output.
 * @return True on success, false on semantic-analysis failure.
 */
bool cxpr_analyze(const cxpr_expr_ast* ast, const cxpr_registry* reg,
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
