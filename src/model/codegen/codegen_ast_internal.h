#ifndef CXPR_MODEL_CODEGEN_AST_INTERNAL_H
#define CXPR_MODEL_CODEGEN_AST_INTERNAL_H

#include "model/codegen/internal.h"

/**
 * @file model/codegen/codegen_ast_internal.h
 * @brief Internal helpers for model AST-based C code generation.
 *
 * This header contains private APIs used by the AST emitter and C-generation
 * passes inside the cxpr model codegen module. It is not intended as a public
 * interface.
 */

/** @brief CSE book-keeping for a single resample expression slot. */
typedef struct {
    size_t slot;
    unsigned lookback;
    size_t uses;
} cxpr_model_resample_cse;

/**
 * @brief Aggregated context for emitting one model-derived target function.
 */
typedef struct {
    const cxpr_model_compiled* program;
    char** param_names;
    char** param_exprs;
    size_t param_count;
    const char* inline_fn_name;
    const char* function_prefix;
    const double* literal_param_values;
    size_t literal_param_count;
    bool inline_defined_functions;
    char** child_call_keys;
    size_t* child_call_child_indices;
    size_t child_call_count;
    cxpr_model_resample_cse* resample_cse;
    size_t resample_cse_count;
} cxpr_model_ast_c_target;

/** @brief Temporary emission state used while converting AST to C. */
typedef struct {
    cxpr_model_c_buf* declarations;
    const cxpr_c_target* target;
    size_t next_temp;
} cxpr_model_ast_temp_emit;

/** Collect resample CSE candidates for one AST subtree. */
bool cxpr_model_collect_resample_cse(cxpr_model_ast_c_target* target,
                                     const cxpr_expr_ast* ast,
                                     unsigned offset);

/**
 * @brief Collect child-model calls referenced from one AST subtree.
 *
 * Populates dynamic arrays with child names and their binding indices.
 */
bool cxpr_model_c_collect_child_calls_from_ast(const cxpr_model_compiled* program,
                                             const cxpr_expr_ast* ast,
                                             char*** keys,
                                             size_t** child_call_child_indices,
                                             size_t* child_call_count,
                                             size_t* child_call_capacity,
                                             cxpr_error* err);

/** Check whether symbol name resolves to state, returning state slot when found. */
bool cxpr_model_c_symbol_is_state(const cxpr_model_compiled* program,
                                const char* name,
                                size_t* out_slot);

/** Resolve a named binding in compiled bindings. */
const cxpr_model_compiled_binding* cxpr_model_c_binding_for_name(
    const cxpr_model_compiled* program,
    const char* name);

/** Resolve a named compile-time constant in compiled bindings. */
const cxpr_model_compiled_binding* cxpr_model_c_constant_for_name(
    const cxpr_model_compiled* program,
    const char* name);

/** Resolve a source alias by name for generated code emission. */
const char* cxpr_model_c_source_for_name(const cxpr_model_compiled* program,
                                        const char* name);

/** Emit a comment block for generated function/source provenance. */
void cxpr_model_c_emit_source_comment(cxpr_model_c_buf* b,
                                     const char* label,
                                     const char* source);

/** Test whether AST can be emitted as a record-like value for this context. */
bool cxpr_model_ast_is_record_like(const cxpr_model_compiled* program,
                                  const cxpr_expr_ast* ast,
                                  unsigned depth);

char* cxpr_model_ast_c_emit_leaf(const cxpr_expr_ast* ast,
                                 unsigned lookback_offset,
                                 void* userdata,
                                 cxpr_error* err);

char* cxpr_model_ast_c_emit_call(const cxpr_expr_ast* ast,
                                 unsigned lookback_offset,
                                 void* userdata,
                                 bool* handled,
                                 cxpr_error* err);

char* cxpr_model_ast_c_emit_lookback(const cxpr_expr_ast* ast,
                                     unsigned lookback_offset,
                                     void* userdata,
                                     cxpr_error* err);

/** Emit access to a field returned by a model producer call. */
char* cxpr_model_ast_producer_access_to_c(
    const cxpr_model_compiled* program,
    const cxpr_expr_ast* ast,
    const char* function_prefix,
    const double* literal_param_values,
    size_t literal_param_count,
    char** child_call_keys,
    size_t* child_call_child_indices,
    size_t child_call_count,
    cxpr_error* err);

/** Resolve the generated reduction opcode for a window function. */
const char* cxpr_model_c_window_op(const char* name);

/** Evaluate an AST expression that is constant for code generation. */
bool cxpr_model_c_constant_param_expr(const cxpr_model_compiled* program,
                                      const cxpr_expr_ast* ast,
                                      double* out);

/** Resolve the maximum storage capacity for a window period expression. */
bool cxpr_model_c_window_period_capacity(const cxpr_model_compiled* program,
                                         const cxpr_expr_ast* period_ast,
                                         size_t* out_capacity,
                                         cxpr_error* err);

/** Match a scaled high/low midpoint expression. */
bool cxpr_model_c_match_scaled_high_low_midpoint(
    const cxpr_expr_ast* ast,
    const cxpr_expr_ast** out_high_ast,
    const cxpr_expr_ast** out_low_ast,
    const cxpr_expr_ast** out_period_ast);

/** Emit a midpoint binding from matched high/low window expressions. */
bool cxpr_model_c_emit_midpoint_binding(cxpr_model_c_buf* b,
                                        const char* name,
                                        const cxpr_expr_ast* high_ast,
                                        const cxpr_expr_ast* low_ast,
                                        const cxpr_expr_ast* period_ast,
                                        const cxpr_c_target* target,
                                        const cxpr_model_compiled* program,
                                        cxpr_error* err);

/** Emit a binding backed by one supported window reduction. */
bool cxpr_model_c_emit_simple_window_binding(cxpr_model_c_buf* b,
                                             const char* name,
                                             const cxpr_expr_ast* ast,
                                             const cxpr_c_target* target,
                                             const cxpr_model_compiled* program,
                                             cxpr_error* err);

/** Check whether two expressions form a mean/stddev optimization pair. */
bool cxpr_model_c_match_mean_stddev_pair(const cxpr_expr_ast* mean_ast,
                                         const cxpr_expr_ast* stddev_ast);

/** Emit a shared mean/stddev binding pair. */
bool cxpr_model_c_emit_mean_stddev_bindings(cxpr_model_c_buf* b,
                                            const char* mean_name,
                                            const char* stddev_name,
                                            const cxpr_expr_ast* mean_ast,
                                            const cxpr_c_target* target,
                                            const cxpr_model_compiled* program,
                                            cxpr_error* err);

/**
 * @brief Convert AST to C source text.
 *
 * Returns heap-allocated string owned by the caller.
 */
char* cxpr_model_ast_expr_to_c(const cxpr_model_compiled* program,
                               const cxpr_expr_ast* ast,
                               const char* function_prefix,
                               const double* literal_param_values,
                               size_t literal_param_count,
                               char** child_call_keys,
                               size_t* child_call_child_indices,
                               size_t child_call_count,
                               cxpr_error* err);

/** Emit runtime state typedef declarations from the window plan. */
bool cxpr_model_c_emit_runtime_state_typedef(
    cxpr_model_c_buf* b,
    const cxpr_model_compiled* program,
    const cxpr_model_window_plan* window_plan,
    const char* safe_name,
    const size_t* child_call_child_indices,
    size_t child_call_count,
    cxpr_error* err);

/** Emit all generated state typedef declarations. */
bool cxpr_model_c_emit_state_typedefs(cxpr_model_c_buf* b,
                                    const cxpr_model_compiled* program,
                                    const cxpr_model_window_plan* window_plan,
                                    const char* safe_name,
                                    cxpr_error* err);

/** Emit slot initialization function for generated model runtime state. */
bool cxpr_model_c_emit_slot_init_function(
    cxpr_model_c_buf* b,
    const cxpr_model_compiled* program,
    const cxpr_model_window_plan* window_plan,
    const char* qualifiers,
    const char* safe_name,
    cxpr_error* err);

/** Emit helpers required to call child model entrypoints. */
bool cxpr_model_c_emit_child_model_helpers(
    const cxpr_model_compiled* program,
    const char* function_prefix,
    const size_t* child_call_child_indices,
    size_t child_call_count,
    cxpr_model_c_buf* b,
    cxpr_error* err);

/** Emit shared helper functions required by generated model code. */
void cxpr_model_c_emit_common_helpers(cxpr_model_c_buf* b);

/** Emit user-defined functions present in AST bodies. */
bool cxpr_model_c_emit_defined_functions_ast(const cxpr_model_compiled* program,
                                          const char* function_prefix,
                                          cxpr_model_c_buf* b,
                                          cxpr_error* err);

/** Emit a pair of bindings when both can be emitted as an optimized pair. */
bool cxpr_model_c_emit_optimized_binding_pair(
    cxpr_model_c_buf* b,
    const char* first_name,
    const char* second_name,
    const cxpr_expr_ast* first_ast,
    const cxpr_expr_ast* second_ast,
    const cxpr_c_target* target,
    const cxpr_model_compiled* program,
    cxpr_error* err);

/** Emit one binding when it can be handled as a single optimized path. */
bool cxpr_model_c_emit_optimized_single_binding(
    cxpr_model_c_buf* b,
    const char* name,
    const cxpr_expr_ast* ast,
    const cxpr_c_target* target,
    const cxpr_model_compiled* program,
    cxpr_error* err);

/** Emit planned ROC aggregate update binding implementation. */
bool cxpr_model_c_emit_planned_roc_aggregate_binding(
    cxpr_model_c_buf* b,
    const char* name,
    const cxpr_model_window_plan* plan,
    const cxpr_model_window_plan_node* node,
    const cxpr_c_target* target,
    const cxpr_model_compiled* program,
    cxpr_error* err);

/** Emit planned simple aggregate update binding implementation. */
bool cxpr_model_c_emit_planned_simple_aggregate_binding(
    cxpr_model_c_buf* b,
    const char* name,
    const cxpr_model_window_plan* plan,
    const cxpr_model_window_plan_node* node,
    const cxpr_c_target* target,
    const cxpr_model_compiled* program,
    cxpr_error* err);

/**
 * @brief Find a common binding expression suitable for shared emission.
 *
 * Returns expression pointer already present in emitted set or NULL.
 */
const char* cxpr_model_c_find_common_binding_expr(
    const cxpr_model_compiled* program,
    size_t binding_index,
    const bool* needed_bindings,
    const bool* skip_bindings,
    char* const* emitted_names);

/** Convert a current symbol name into the generated current-state expression. */
char* cxpr_model_c_current_symbol_expr(const cxpr_model_compiled* program,
                                      char** state_next_names,
                                      const char* name,
                                      cxpr_error* err);

const char* cxpr_model_c_history_counter_type(size_t capacity);

/** Return the first fused slot reserved by a planned window node. */
size_t cxpr_model_c_window_plan_base(
    const cxpr_model_compiled* program,
    const cxpr_model_window_plan_node* node);

/** Return the smallest generated counter type suitable for a window node. */
const char* cxpr_model_c_window_counter_type(
    const cxpr_model_window_plan_node* node);

/** Initialize generated sentinel slot used for missing-history signaling. */
bool cxpr_model_c_init_sentinel_slot(const cxpr_model_compiled* program,
                                    const cxpr_model_window_plan* window_plan,
                                    size_t* out_slot);

#endif /* CXPR_MODEL_CODEGEN_AST_INTERNAL_H */
