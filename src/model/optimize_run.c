#include "core.h"
#include "model/internal.h"
#include <float.h>
#include <stdlib.h>

static size_t cxpr_optimize_dimension_size(const cxpr_model_optimize_dimension* dim) {
    if (dim->kind == CXPR_OPT_DIM_VALUES) return dim->value_count;
    return (size_t)(((dim->max - dim->min) / dim->step) + 0.5) + 1u;
}

static double cxpr_optimize_dimension_value(const cxpr_model_optimize_dimension* dim,
                                            size_t index) {
    return dim->kind == CXPR_OPT_DIM_VALUES
        ? dim->values[index] : dim->min + dim->step * (double)index;
}

static bool cxpr_optimize_candidate_allowed(const cxpr_model_compiled* program,
                                            const cxpr_model_session* session,
                                            cxpr_error* err) {
    const cxpr_registry* registry = program->registry;
    for (size_t i = 0u; i < program->optimize_constraint_count; ++i) {
        bool allowed = false;
        if (!cxpr_eval_ast_bool(program->optimize_constraints[i].expr,
                                session->ctx, registry, &allowed, err)) return false;
        if (!allowed) return false;
    }
    return true;
}

static bool cxpr_optimize_replay(const cxpr_model_compiled* program,
                                 cxpr_model_session* session,
                                 const cxpr_model_optimize_inputs* inputs,
                                 double* objectives,
                                 cxpr_error* err) {
    for (size_t tick = 0u; tick < inputs->tick_count; ++tick) {
        for (size_t input = 0u; input < inputs->input_count; ++input) {
            cxpr_context_set(session->ctx, inputs->names[input],
                             inputs->values[tick * inputs->input_count + input]);
        }
        if (!cxpr_model_session_tick(program, session, program->registry, err)) return false;
    }
    for (size_t i = 0u; i < program->optimize_objective_count; ++i) {
        bool found = false;
        objectives[i] = cxpr_context_get(
            session->ctx, program->optimize_objectives[i].name, &found);
        if (!found) {
            cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                 "Optimize objective was not produced", 0, 0);
            return false;
        }
    }
    return true;
}

void cxpr_model_optimize_result_free(cxpr_model_optimize_result* result) {
    if (!result) return;
    for (size_t i = 0u; i < result->candidate_count; ++i) {
        free(result->candidates[i].params);
        free(result->candidates[i].objectives);
    }
    free(result->candidates);
    *result = (cxpr_model_optimize_result){0};
}

bool cxpr_model_optimize(const cxpr_model_compiled* program,
                         const cxpr_model_optimize_inputs* inputs,
                         const cxpr_model_optimize_options* options,
                         cxpr_model_optimize_result* result,
                         cxpr_error* err) {
    size_t total = 1u;
    size_t* indices = NULL;
    double best = 0.0;
    bool has_best = false;
    if (err) *err = (cxpr_error){0};
    if (!program || !inputs || !result || program->optimize_dimension_count == 0u ||
        program->optimize_objective_count == 0u) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Optimization requires dimensions and objectives", 0, 0);
        return false;
    }
    *result = (cxpr_model_optimize_result){0};
    result->param_count = program->optimize_dimension_count;
    result->objective_count = program->optimize_objective_count;
    for (size_t i = 0u; i < program->optimize_dimension_count; ++i) {
        size_t size = cxpr_optimize_dimension_size(&program->optimize_dimensions[i]);
        if (size == 0u || total > SIZE_MAX / size) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                 "Optimize candidate space is too large", 0, 0);
            return false;
        }
        total *= size;
    }
    if (options && options->max_candidates && total > options->max_candidates) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                             "Optimize candidate limit exceeded", 0, 0);
        return false;
    }
    result->candidates = (cxpr_model_optimize_candidate_result*)calloc(
        total, sizeof(*result->candidates));
    indices = (size_t*)calloc(program->optimize_dimension_count, sizeof(*indices));
    if (!result->candidates || !indices) goto oom;

    for (size_t candidate = 0u; candidate < total; ++candidate) {
        cxpr_model_session* session = cxpr_model_session_new(program, program->registry, err);
        cxpr_model_optimize_candidate_result current = {0};
        bool allowed;
        if (!session) goto fail;
        current.params = (double*)calloc(result->param_count, sizeof(double));
        current.objectives = (double*)calloc(result->objective_count, sizeof(double));
        if (!current.params || !current.objectives) {
            free(current.params);
            free(current.objectives);
            cxpr_model_session_free(session);
            goto oom;
        }
        for (size_t i = 0u; i < result->param_count; ++i) {
            current.params[i] = cxpr_optimize_dimension_value(
                &program->optimize_dimensions[i], indices[i]);
            if (!cxpr_model_session_set_param(
                    session, program->optimize_dimensions[i].name,
                    current.params[i], err)) {
                free(current.params);
                free(current.objectives);
                cxpr_model_session_free(session);
                goto fail;
            }
        }
        if (!cxpr_model_compiled_check_asserts(
                program, session->ctx, program->registry, err)) {
            free(current.params);
            free(current.objectives);
            cxpr_model_session_free(session);
            goto fail;
        }
        allowed = cxpr_optimize_candidate_allowed(program, session, err);
        if (err && err->code != CXPR_OK) {
            free(current.params);
            free(current.objectives);
            cxpr_model_session_free(session);
            goto fail;
        }
        if (allowed) {
            double score;
            if (!cxpr_optimize_replay(program, session, inputs, current.objectives, err)) {
                free(current.params);
                free(current.objectives);
                cxpr_model_session_free(session);
                goto fail;
            }
            score = current.objectives[0];
            result->candidates[result->candidate_count] = current;
            if (!has_best ||
                (program->optimize_objectives[0].direction == CXPR_OPT_MAXIMIZE
                     ? score > best : score < best)) {
                best = score;
                result->best_index = result->candidate_count;
                has_best = true;
            }
            result->candidate_count++;
        } else {
            free(current.params);
            free(current.objectives);
        }
        cxpr_model_session_free(session);
        for (size_t i = result->param_count; i-- > 0u;) {
            if (++indices[i] < cxpr_optimize_dimension_size(
                    &program->optimize_dimensions[i])) break;
            indices[i] = 0u;
        }
    }
    free(indices);
    if (!has_best) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "No candidates satisfied optimize constraints", 0, 0);
        cxpr_model_optimize_result_free(result);
        return false;
    }
    return true;
oom:
    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
fail:
    free(indices);
    cxpr_model_optimize_result_free(result);
    return false;
}
