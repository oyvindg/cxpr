#ifndef CXPR_MODEL_OPTIMIZE_H
#define CXPR_MODEL_OPTIMIZE_H

#include <cxpr/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CXPR_OPT_DIM_RANGE = 0,
    CXPR_OPT_DIM_VALUES = 1
} cxpr_model_optimize_dim_kind;

typedef enum {
    CXPR_OPT_MINIMIZE = 0,
    CXPR_OPT_MAXIMIZE = 1
} cxpr_model_optimize_direction;

typedef enum {
    CXPR_OPT_OBJECTIVE_BINDING = 0,
    CXPR_OPT_OBJECTIVE_OUTPUT = 1,
    CXPR_OPT_OBJECTIVE_STATE = 2
} cxpr_model_optimize_objective_source;

typedef struct {
    const char* name;
    cxpr_model_optimize_dim_kind kind;
    double min;
    double max;
    double step;
    const double* values;
    size_t value_count;
} cxpr_model_optimize_dimension;

typedef struct {
    const char* name;
    cxpr_model_optimize_direction direction;
    cxpr_model_optimize_objective_source source;
} cxpr_model_optimize_objective;

typedef struct {
    const char* const* names;
    const double* values; /* Tick-major: values[tick * input_count + input]. */
    size_t input_count;
    size_t tick_count;
} cxpr_model_optimize_inputs;

typedef struct {
    size_t max_candidates; /* Zero means no explicit limit. */
} cxpr_model_optimize_options;

typedef struct {
    double* params;
    double* objectives;
} cxpr_model_optimize_candidate_result;

typedef struct {
    cxpr_model_optimize_candidate_result* candidates;
    size_t candidate_count;
    size_t param_count;
    size_t objective_count;
    size_t best_index;
} cxpr_model_optimize_result;

size_t cxpr_model_optimize_dimension_count(const cxpr_model_compiled* program);
cxpr_model_optimize_dimension cxpr_model_optimize_dimension_at(
    const cxpr_model_compiled* program, size_t index);
size_t cxpr_model_optimize_constraint_count(const cxpr_model_compiled* program);
const cxpr_expr_ast* cxpr_model_optimize_constraint_at(
    const cxpr_model_compiled* program, size_t index);
size_t cxpr_model_optimize_objective_count(const cxpr_model_compiled* program);
cxpr_model_optimize_objective cxpr_model_optimize_objective_at(
    const cxpr_model_compiled* program, size_t index);
bool cxpr_model_optimize(const cxpr_model_compiled* program,
                         const cxpr_model_optimize_inputs* inputs,
                         const cxpr_model_optimize_options* options,
                         cxpr_model_optimize_result* result,
                         cxpr_error* err);
void cxpr_model_optimize_result_free(cxpr_model_optimize_result* result);

#ifdef __cplusplus
}
#endif
#endif
