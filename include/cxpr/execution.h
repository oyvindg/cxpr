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

typedef enum cxpr_execution_mode {
    CXPR_EXECUTION_GENERATED_C = 1,
    CXPR_EXECUTION_AST_ANALYSIS = 2,
    CXPR_EXECUTION_IR_REFERENCE = 3,
} cxpr_execution_mode;

typedef struct cxpr_execution_policy {
    cxpr_execution_mode mode;
} cxpr_execution_policy;

typedef struct cxpr_execution_report {
    size_t generated_c_calls;
    size_t ast_analysis_calls;
    size_t ir_reference_calls;
    size_t runtime_compiles;
} cxpr_execution_report;

static inline int cxpr_execution_policy_valid(
    const cxpr_execution_policy* policy) {
    return policy &&
           (policy->mode == CXPR_EXECUTION_GENERATED_C ||
            policy->mode == CXPR_EXECUTION_AST_ANALYSIS ||
            policy->mode == CXPR_EXECUTION_IR_REFERENCE);
}

static inline int cxpr_execution_policy_allows_runtime_compile(
    const cxpr_execution_policy* policy) {
    return cxpr_execution_policy_valid(policy) &&
           policy->mode != CXPR_EXECUTION_GENERATED_C;
}

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
