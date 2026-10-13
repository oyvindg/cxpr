#include "model/internal.h"

size_t cxpr_model_optimize_dimension_count(const cxpr_model_compiled* program) {
    return program ? program->optimize_dimension_count : 0u;
}

cxpr_model_optimize_dimension cxpr_model_optimize_dimension_at(
    const cxpr_model_compiled* program, size_t index) {
    if (!program || index >= program->optimize_dimension_count)
        return (cxpr_model_optimize_dimension){0};
    return program->optimize_dimensions[index];
}

/* Optimize constraints are the subset of asserts that reference optimize
   $params; they are introspected as a derived view over the assert list. */
size_t cxpr_model_optimize_constraint_count(const cxpr_model_compiled* program) {
    size_t total = 0u;
    if (!program) return 0u;
    for (size_t i = 0u; i < program->assert_count; ++i)
        if (program->asserts[i].optimize_scoped) ++total;
    return total;
}

const cxpr_expr_ast* cxpr_model_optimize_constraint_at(
    const cxpr_model_compiled* program, size_t index) {
    size_t seen = 0u;
    if (!program) return NULL;
    for (size_t i = 0u; i < program->assert_count; ++i) {
        if (!program->asserts[i].optimize_scoped) continue;
        if (seen == index) return program->asserts[i].expr;
        ++seen;
    }
    return NULL;
}

size_t cxpr_model_optimize_objective_count(const cxpr_model_compiled* program) {
    return program ? program->optimize_objective_count : 0u;
}

cxpr_model_optimize_objective cxpr_model_optimize_objective_at(
    const cxpr_model_compiled* program, size_t index) {
    if (!program || index >= program->optimize_objective_count)
        return (cxpr_model_optimize_objective){0};
    return program->optimize_objectives[index];
}
