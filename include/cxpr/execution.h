/**
 * @file execution.h
 * @brief Host-neutral execution intent for CXPR artifacts and tools.
 */

#ifndef CXPR_EXECUTION_H
#define CXPR_EXECUTION_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Execution backend requested for a cxpr operation. */
typedef enum cxpr_execution_mode {
    CXPR_EXECUTION_GENERATED_C = 1,
    CXPR_EXECUTION_AST_ANALYSIS = 2,
    CXPR_EXECUTION_IR_REFERENCE = 3,
} cxpr_execution_mode;

/** @brief Host-selected execution policy. */
typedef struct cxpr_execution_policy {
    cxpr_execution_mode mode;  /**< Backend allowed by the host. */
} cxpr_execution_policy;

/** @brief Counters describing which execution paths were used. */
typedef struct cxpr_execution_report {
    size_t generated_c_calls;    /**< Calls through generated C artifacts. */
    size_t ast_analysis_calls;   /**< Calls performing AST-only analysis. */
    size_t ir_reference_calls;   /**< Calls through the reference IR evaluator. */
    size_t runtime_compiles;     /**< Runtime compilation operations performed. */
} cxpr_execution_report;

/**
 * @brief Check whether an execution policy selects a known mode.
 * @param policy Policy to validate.
 * @return Non-zero when valid, otherwise zero.
 */
static inline int cxpr_execution_policy_valid(
    const cxpr_execution_policy* policy) {
    return policy &&
           (policy->mode == CXPR_EXECUTION_GENERATED_C ||
            policy->mode == CXPR_EXECUTION_AST_ANALYSIS ||
            policy->mode == CXPR_EXECUTION_IR_REFERENCE);
}

/**
 * @brief Check whether a policy permits runtime compilation.
 * @param policy Policy to inspect.
 * @return Non-zero when runtime compilation is permitted, otherwise zero.
 */
static inline int cxpr_execution_policy_allows_runtime_compile(
    const cxpr_execution_policy* policy) {
    return cxpr_execution_policy_valid(policy) &&
           policy->mode != CXPR_EXECUTION_GENERATED_C;
}

/**
 * @brief Check whether a report contains generated-C execution only.
 * @param report Execution report to inspect.
 * @return Non-zero when no reference or runtime compilation path was used.
 */
static inline int cxpr_execution_report_is_generated_only(
    const cxpr_execution_report* report) {
    return report &&
           report->ast_analysis_calls == 0u &&
           report->ir_reference_calls == 0u &&
           report->runtime_compiles == 0u;
}

#ifdef __cplusplus
}
#endif

#endif
