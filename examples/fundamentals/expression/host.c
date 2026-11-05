/* Compare an authored .cxpr model with the public programmatic expression API. */
#include <cxpr/cxpr.h>

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define MODEL_PATH                                                          \
    CXPR_EXAMPLE_SOURCE_DIR "/fundamentals/expression/model.cxpr"

static void require_close(double actual, double expected) {
    const double scale = fmax(1.0, fmax(fabs(actual), fabs(expected)));
    assert(isfinite(actual));
    assert(fabs(actual - expected) <= 1e-12 * scale);
}

int main(void) {
    cxpr_error error = {0};
    cxpr_doc* document = cxpr_doc_load_model(MODEL_PATH, &error);
    const cxpr_model* model;
    cxpr_model_compiled* program;
    cxpr_model_session* session;
    cxpr_context* model_context;
    double model_energy = 0.0;
    bool model_over_speed = false;

    cxpr_registry* registry;
    cxpr_evaluator* evaluator;
    cxpr_context* expression_context;
    const cxpr_expression_def definitions[] = {
        {"kinetic_energy", "0.5 * mass * velocity^2"},
        {"over_speed", "abs(velocity) > $speed_limit"},
    };
    double expression_energy;
    bool expression_over_speed;

    assert(document);
    model = cxpr_doc_model(document);
    assert(model);
    program = cxpr_model_compile(model, NULL, &error);
    assert(program);
    session = cxpr_model_session_new(program, NULL, &error);
    assert(session);
    model_context = cxpr_model_session_context(session);
    assert(model_context);
    cxpr_context_set(model_context, "mass", 2.0);
    cxpr_context_set(model_context, "velocity", 3.0);
    assert(cxpr_model_session_tick(program, session, NULL, &error));
    assert(cxpr_model_session_get_number(session, "kinetic_energy",
                                         &model_energy));
    assert(cxpr_model_session_get_bool(session, "over_speed",
                                       &model_over_speed));

    registry = cxpr_registry_new();
    evaluator = cxpr_evaluator_new(registry);
    expression_context = cxpr_context_new();
    assert(registry && evaluator && expression_context);
    cxpr_register_defaults(registry);
    cxpr_context_set(expression_context, "mass", 2.0);
    cxpr_context_set(expression_context, "velocity", 3.0);
    cxpr_context_set_param(expression_context, "speed_limit", 4.0);
    assert(cxpr_expressions_add(evaluator, definitions,
                                CXPR_ARRAY_COUNT(definitions), &error));
    assert(cxpr_evaluator_compile(evaluator, &error));
    assert(cxpr_evaluator_eval(evaluator, expression_context, &error));
    expression_energy =
        cxpr_expression_get_double(evaluator, "kinetic_energy", NULL);
    expression_over_speed =
        cxpr_expression_get_bool(evaluator, "over_speed", NULL);

    require_close(model_energy, 9.0);
    require_close(expression_energy, model_energy);
    assert(!model_over_speed);
    assert(expression_over_speed == model_over_speed);

    printf("expression: energy=%.1f over_speed=%s; .cxpr/API parity OK\n",
           model_energy, model_over_speed ? "true" : "false");

    cxpr_context_free(expression_context);
    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(registry);
    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
    cxpr_doc_free(document);
    return 0;
}
