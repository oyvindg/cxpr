#include "core.h"
#include "model/internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool cxpr_optimize_add_dimension(const cxpr_model* model,
                                        size_t metadata_index,
                                        cxpr_model_compiled* program,
                                        cxpr_error* err) {
    const char* name = cxpr_model_metadata_target_name(model, metadata_index);
    const char* values_field = cxpr_model_metadata_field_value(
        model, metadata_index, "optimize.values");
    const char* min_field = cxpr_model_metadata_field_value(
        model, metadata_index, "optimize.min");
    const char* max_field = cxpr_model_metadata_field_value(
        model, metadata_index, "optimize.max");
    const char* step_field = cxpr_model_metadata_field_value(
        model, metadata_index, "optimize.step");
    bool has_range = min_field || max_field || step_field;
    cxpr_model_optimize_dimension* dim;

    if (!values_field && !has_range) return true;
    if (cxpr_model_metadata_target_kind_at(model, metadata_index) !=
        CXPR_MODEL_METADATA_TARGET_PARAM) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Optimize search dimensions are only valid on $params", 0, 0);
        return false;
    }
    if ((values_field && has_range) ||
        (!values_field && (!min_field || !max_field || !step_field))) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Optimize dimension requires values or min/max/step", 0, 0);
        return false;
    }

    dim = &program->optimize_dimensions[program->optimize_dimension_count];
    dim->name = cxpr_strdup(name ? name : "");
    if (!dim->name) goto oom;
    if (values_field) {
        double* values = NULL;
        if (!cxpr_model_metadata_field_number_list(
                model, metadata_index, "optimize.values", &values, &dim->value_count) ||
            dim->value_count == 0u) {
            free(values);
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Optimize values must be a non-empty number list", 0, 0);
            return false;
        }
        dim->kind = CXPR_OPT_DIM_VALUES;
        dim->values = values;
    } else {
        double intervals;
        if (!cxpr_model_metadata_field_number(model, metadata_index,
                                              "optimize.min", &dim->min) ||
            !cxpr_model_metadata_field_number(model, metadata_index,
                                              "optimize.max", &dim->max) ||
            !cxpr_model_metadata_field_number(model, metadata_index,
                                              "optimize.step", &dim->step) ||
            dim->step <= 0.0 || dim->min > dim->max) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Invalid optimize min/max/step range", 0, 0);
            return false;
        }
        intervals = (dim->max - dim->min) / dim->step;
        if (fabs(intervals - round(intervals)) > 1e-9) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Optimize range must contain an integer step count", 0, 0);
            return false;
        }
        dim->kind = CXPR_OPT_DIM_RANGE;
    }
    program->optimize_dimension_count++;
    return true;
oom:
    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
    return false;
}

static bool cxpr_optimize_add_objective(const cxpr_model* model,
                                        size_t metadata_index,
                                        cxpr_model_compiled* program,
                                        cxpr_error* err) {
    const char* minimize = cxpr_model_metadata_field_value(
        model, metadata_index, "optimize.minimize");
    const char* maximize = cxpr_model_metadata_field_value(
        model, metadata_index, "optimize.maximize");
    const char* name = cxpr_model_metadata_target_name(model, metadata_index);
    cxpr_model_metadata_target_kind kind =
        cxpr_model_metadata_target_kind_at(model, metadata_index);

    if (!minimize && !maximize) return true;
    if (minimize && maximize) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Optimize objective cannot both minimize and maximize", 0, 0);
        return false;
    }
    if (kind != CXPR_MODEL_METADATA_TARGET_BINDING &&
        kind != CXPR_MODEL_METADATA_TARGET_OUTPUT &&
        kind != CXPR_MODEL_METADATA_TARGET_STATE) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Optimize objectives require a named binding, state, or output", 0, 0);
        return false;
    }
    for (size_t i = 0u; i < program->optimize_objective_count; ++i) {
        if (strcmp(program->optimize_objectives[i].name, name ? name : "") == 0) {
            cxpr_model_optimize_direction direction =
                maximize ? CXPR_OPT_MAXIMIZE : CXPR_OPT_MINIMIZE;
            if (program->optimize_objectives[i].direction != direction) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Duplicate optimize objective has conflicting directions",
                                     0, 0);
                return false;
            }
            return true;
        }
    }
    {
        cxpr_model_optimize_objective* objective =
            &program->optimize_objectives[program->optimize_objective_count++];
        objective->name = cxpr_strdup(name ? name : "");
        objective->direction = maximize ? CXPR_OPT_MAXIMIZE : CXPR_OPT_MINIMIZE;
        objective->source = kind == CXPR_MODEL_METADATA_TARGET_OUTPUT
            ? CXPR_OPT_OBJECTIVE_OUTPUT
            : kind == CXPR_MODEL_METADATA_TARGET_STATE
                ? CXPR_OPT_OBJECTIVE_STATE : CXPR_OPT_OBJECTIVE_BINDING;
        if (!objective->name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
    }
    return true;
}

bool cxpr_model_prepare_optimize(const cxpr_model* model,
                                 cxpr_model_compiled* program,
                                 cxpr_error* err) {
    size_t count = cxpr_model_metadata_count(model);
    if (count == 0u) return true;
    program->optimize_dimensions = (cxpr_model_optimize_dimension*)calloc(
        count, sizeof(*program->optimize_dimensions));
    program->optimize_objectives = (cxpr_model_optimize_objective*)calloc(
        count, sizeof(*program->optimize_objectives));
    if (!program->optimize_dimensions || !program->optimize_objectives) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    for (size_t i = 0u; i < count; ++i) {
        if (!cxpr_optimize_add_dimension(model, i, program, err) ||
            !cxpr_optimize_add_objective(model, i, program, err)) return false;
    }
    return true;
}
