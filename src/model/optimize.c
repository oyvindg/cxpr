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

size_t cxpr_model_optimize_constraint_count(const cxpr_model_compiled* program) {
    return program ? program->optimize_constraint_count : 0u;
}

const cxpr_expr_ast* cxpr_model_optimize_constraint_at(
    const cxpr_model_compiled* program, size_t index) {
    return program && index < program->optimize_constraint_count
        ? program->optimize_constraints[index].expr : NULL;
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
