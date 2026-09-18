#include "core.h"
#include "model/internal.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>

static size_t cxpr_optimize_dimension_size(const cxpr_model_optimize_dimension* dim) {
    if (dim->kind == CXPR_OPT_DIM_VALUES) return dim->value_count;
    return (size_t)(((dim->max - dim->min) / dim->step) + 0.5) + 1u;
}

static double cxpr_optimize_dimension_value(const cxpr_model_optimize_dimension* dim,
                                            size_t index) {
    return dim->kind == CXPR_OPT_DIM_VALUES
        ? dim->values[index] : dim->min + dim->step * (double)index;
}

void cxpr_model_optimize_grid_free(cxpr_model_optimize_grid* grid) {
    if (!grid) return;
    free(grid->params);
    free(grid->active);
    *grid = (cxpr_model_optimize_grid){0};
}

bool cxpr_model_optimize_prepare_grid(
    const cxpr_model_compiled* program,
    const cxpr_model_optimize_inputs* inputs,
    cxpr_model_optimize_grid* out,
    cxpr_error* err) {
    size_t* indices = NULL;
    size_t total = 1u;
    if (err) *err = (cxpr_error){0};
    if (!program || !inputs || !out || program->optimize_dimension_count == 0u ||
        inputs->input_count != program->input_count) {
        cxpr_model_set_error(err, CXPR_ERR_TYPE_MISMATCH,
                             "Optimize grid input shape mismatch", 0, 0);
        return false;
    }
    *out = (cxpr_model_optimize_grid){0};
    out->param_count = program->optimize_dimension_count;
    for (size_t i = 0u; i < out->param_count; ++i) {
        const size_t size = cxpr_optimize_dimension_size(&program->optimize_dimensions[i]);
        if (size == 0u || total > SIZE_MAX / size) goto oom;
        total *= size;
    }
    if (out->param_count && total > SIZE_MAX / out->param_count) goto oom;
    out->candidate_count = total;
    out->params = (cxpr_value*)calloc(total * out->param_count, sizeof(*out->params));
    out->active = (unsigned char*)calloc(total, sizeof(*out->active));
    indices = (size_t*)calloc(out->param_count, sizeof(*indices));
    if (!out->params || !out->active || !indices) goto oom;

    for (size_t candidate = 0u; candidate < total; ++candidate) {
        cxpr_context* context = cxpr_context_new();
        bool allowed = context != NULL;
        if (!context) goto oom;
        if (!cxpr_model_compiled_seed_defaults(
                program, context, program->registry, err)) {
            cxpr_context_free(context);
            goto fail;
        }
        for (size_t i = 0u; i < out->param_count; ++i) {
            const double value = cxpr_optimize_dimension_value(
                &program->optimize_dimensions[i], indices[i]);
            out->params[candidate * out->param_count + i] = cxpr_num(value);
            cxpr_context_set_param(context, program->optimize_dimensions[i].name, value);
        }
        if (!cxpr_model_compiled_check_asserts(program, context, program->registry, err)) {
            cxpr_context_free(context);
            goto fail;
        }
        for (size_t i = 0u; allowed && i < program->optimize_constraint_count; ++i) {
            bool value = false;
            if (!cxpr_eval_ast_bool(program->optimize_constraints[i].expr,
                                    context, program->registry, &value, err)) {
                cxpr_context_free(context);
                goto fail;
            }
            allowed = value;
        }
        out->active[candidate] = allowed ? 1u : 0u;
        if (allowed) ++out->active_count;
        cxpr_context_free(context);
        for (size_t i = out->param_count; i-- > 0u;) {
            if (++indices[i] < cxpr_optimize_dimension_size(
                    &program->optimize_dimensions[i])) break;
            indices[i] = 0u;
        }
    }
    free(indices);
    if (out->active_count == 0u) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "No candidates satisfied optimize constraints", 0, 0);
        cxpr_model_optimize_grid_free(out);
        return false;
    }
    return true;
oom:
    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                         "Optimize candidate space is too large", 0, 0);
fail:
    free(indices);
    cxpr_model_optimize_grid_free(out);
    return false;
}

bool cxpr_model_optimize_finalize_results(
    const cxpr_model_compiled* program, const cxpr_model_optimize_grid* grid,
    const cxpr_value* outputs, size_t output_count,
    cxpr_model_optimize_result* out, cxpr_error* err) {
    size_t* objective_outputs = NULL;
    size_t row = 0u;
    if (err) *err = (cxpr_error){0};
    if (!program || !grid || !outputs || !out ||
        output_count != program->output_count || grid->active_count == 0u) {
        cxpr_model_set_error(err, CXPR_ERR_TYPE_MISMATCH,
                             "Optimize result shape mismatch", 0, 0);
        return false;
    }
    *out = (cxpr_model_optimize_result){0};
    objective_outputs = (size_t*)calloc(program->optimize_objective_count,
                                         sizeof(*objective_outputs));
    out->candidates = (cxpr_model_optimize_candidate_result*)calloc(
        grid->active_count, sizeof(*out->candidates));
    if (!objective_outputs || !out->candidates) goto oom;
    out->param_count = grid->param_count;
    out->objective_count = program->optimize_objective_count;
    for (size_t objective = 0u; objective < out->objective_count; ++objective) {
        size_t output = 0u;
        if (program->optimize_objectives[objective].source != CXPR_OPT_OBJECTIVE_OUTPUT) {
            cxpr_model_set_error(err, CXPR_ERR_UNAVAILABLE,
                "Backend result finalization requires objectives exposed as outputs", 0, 0);
            goto fail;
        }
        while (output < output_count && strcmp(cxpr_model_compiled_output_name(program, output),
               program->optimize_objectives[objective].name) != 0) ++output;
        if (output == output_count) {
            cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                 "Optimize output objective was not generated", 0, 0);
            goto fail;
        }
        objective_outputs[objective] = output;
    }
    for (size_t candidate = 0u; candidate < grid->candidate_count; ++candidate) {
        double score;
        if (!grid->active[candidate]) continue;
        out->candidates[row].params = (double*)calloc(grid->param_count, sizeof(double));
        out->candidates[row].objectives = (double*)calloc(
            out->objective_count, sizeof(double));
        if (!out->candidates[row].params || !out->candidates[row].objectives) goto oom;
        for (size_t i = 0u; i < grid->param_count; ++i)
            out->candidates[row].params[i] = grid->params[
                candidate * grid->param_count + i].d;
        for (size_t i = 0u; i < out->objective_count; ++i)
            out->candidates[row].objectives[i] = outputs[
                candidate * output_count + objective_outputs[i]].d;
        score = out->candidates[row].objectives[0];
        if (row == 0u ||
            (program->optimize_objectives[0].direction == CXPR_OPT_MAXIMIZE
                 ? score > out->candidates[out->best_index].objectives[0]
                 : score < out->candidates[out->best_index].objectives[0]))
            out->best_index = row;
        out->candidate_count = ++row;
    }
    free(objective_outputs);
    return true;
oom:
    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
fail:
    free(objective_outputs);
    cxpr_model_optimize_result_free(out);
    return false;
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
        found = cxpr_model_session_get_number(
            session, program->optimize_objectives[i].name, &objectives[i]);
        if (!found) {
            objectives[i] = cxpr_context_get(
                session->ctx, program->optimize_objectives[i].name, &found);
        }
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

static bool cxpr_model_optimize_cpu(const cxpr_model_compiled* program,
                                    const cxpr_model_optimize_inputs* inputs,
                                    const cxpr_model_optimize_options* options,
                                    cxpr_model_optimize_result* result,
                                    cxpr_error* err) {
    cxpr_model_optimize_grid grid = {0};
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
    if (!cxpr_model_optimize_prepare_grid(program, inputs, &grid, err)) return false;
    result->param_count = grid.param_count;
    result->objective_count = program->optimize_objective_count;
    if (options && options->max_candidates &&
        grid.candidate_count > options->max_candidates) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                             "Optimize candidate limit exceeded", 0, 0);
        cxpr_model_optimize_grid_free(&grid);
        return false;
    }
    result->candidates = (cxpr_model_optimize_candidate_result*)calloc(
        grid.active_count, sizeof(*result->candidates));
    if (!result->candidates) goto oom;

    for (size_t candidate = 0u; candidate < grid.candidate_count; ++candidate) {
        cxpr_model_session* session;
        cxpr_model_optimize_candidate_result current = {0};
        if (!grid.active[candidate]) continue;
        session = cxpr_model_session_new(program, program->registry, err);
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
            current.params[i] = grid.params[candidate * grid.param_count + i].d;
            if (!cxpr_model_session_set_param(
                    session, program->optimize_dimensions[i].name,
                    current.params[i], err)) {
                free(current.params);
                free(current.objectives);
                cxpr_model_session_free(session);
                goto fail;
            }
        }
        {
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
        }
        cxpr_model_session_free(session);
    }
    if (!has_best) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "No candidates satisfied optimize constraints", 0, 0);
        cxpr_model_optimize_result_free(result);
        cxpr_model_optimize_grid_free(&grid);
        return false;
    }
    {
        bool output_objectives = true;
        for (size_t i = 0u; i < program->optimize_objective_count; ++i)
            output_objectives = output_objectives &&
                program->optimize_objectives[i].source == CXPR_OPT_OBJECTIVE_OUTPUT;
        if (output_objectives) {
            cxpr_value* outputs = (cxpr_value*)calloc(
                grid.candidate_count * program->output_count, sizeof(*outputs));
            cxpr_model_optimize_result finalized = {0};
            size_t row = 0u;
            if (!outputs) goto oom;
            for (size_t candidate = 0u; candidate < grid.candidate_count; ++candidate) {
                if (!grid.active[candidate]) continue;
                for (size_t objective = 0u;
                     objective < program->optimize_objective_count; ++objective) {
                    size_t output = 0u;
                    while (output < program->output_count && strcmp(
                        cxpr_model_compiled_output_name(program, output),
                        program->optimize_objectives[objective].name) != 0) ++output;
                    if (output < program->output_count)
                        outputs[candidate * program->output_count + output] = cxpr_num(
                            result->candidates[row].objectives[objective]);
                }
                ++row;
            }
            if (!cxpr_model_optimize_finalize_results(
                    program, &grid, outputs, program->output_count,
                    &finalized, err)) {
                free(outputs);
                goto fail;
            }
            free(outputs);
            cxpr_model_optimize_result_free(result);
            *result = finalized;
        }
    }
    cxpr_model_optimize_grid_free(&grid);
    return true;
oom:
    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
fail:
    cxpr_model_optimize_grid_free(&grid);
    cxpr_model_optimize_result_free(result);
    return false;
}

bool cxpr_model_optimize_with_backend(
    const cxpr_model_compiled* program,
    const cxpr_model_optimize_inputs* inputs,
    const cxpr_model_optimize_options* options,
    const cxpr_model_optimize_backend* backend,
    cxpr_model_optimize_result* result,
    cxpr_error* err) {
    const cxpr_model_optimize_backend_kind requested =
        options ? options->backend : CXPR_OPT_BACKEND_AUTO;
    const bool valid_backend = backend &&
        backend->api_version == CXPR_OPTIMIZE_BACKEND_API_VERSION && backend->run;
    const bool available = valid_backend &&
        (!backend->available || backend->available(backend->userdata));

    if (err) *err = (cxpr_error){0};
    if (!program || !inputs || !result || program->optimize_dimension_count == 0u ||
        program->optimize_objective_count == 0u) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Optimization requires dimensions and objectives", 0, 0);
        return false;
    }
    *result = (cxpr_model_optimize_result){0};
    if (requested != CXPR_OPT_BACKEND_AUTO &&
        requested != CXPR_OPT_BACKEND_CPU &&
        requested != CXPR_OPT_BACKEND_HOST) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Unknown optimize backend", 0, 0);
        return false;
    }
    if (requested == CXPR_OPT_BACKEND_HOST && !available) {
        cxpr_model_set_error(err, CXPR_ERR_UNAVAILABLE,
                             "Requested optimize host backend is unavailable", 0, 0);
        return false;
    }
    if (requested != CXPR_OPT_BACKEND_CPU && available) {
        return backend->run(backend->userdata, program, inputs, options, result, err);
    }
    return cxpr_model_optimize_cpu(program, inputs, options, result, err);
}

bool cxpr_model_optimize(const cxpr_model_compiled* program,
                         const cxpr_model_optimize_inputs* inputs,
                         const cxpr_model_optimize_options* options,
                         cxpr_model_optimize_result* result,
                         cxpr_error* err) {
    return cxpr_model_optimize_with_backend(
        program, inputs, options, NULL, result, err);
}
