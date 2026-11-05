/* Stateful damped-oscillator example with an independent energy oracle. */
#include <cxpr/cxpr.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>

#define MODEL_PATH                                                          \
    CXPR_EXAMPLE_SOURCE_DIR "/domains/physics/oscillator/model.cxpr"

static double output_number(const cxpr_model_session* session,
                            const char* name) {
    double value = 0.0;
    assert(cxpr_model_session_get_number(session, name, &value));
    assert(isfinite(value));
    return value;
}

int main(void) {
    cxpr_error error = {0};
    cxpr_doc* document = cxpr_doc_load_model(MODEL_PATH, &error);
    const cxpr_model* model;
    cxpr_model_compiled* program;
    cxpr_model_session* session;
    cxpr_context* context;
    double previous_energy = 2.0;
    double first_position;
    size_t step;

    assert(document);
    model = cxpr_doc_model(document);
    assert(model);
    program = cxpr_model_compile(model, NULL, &error);
    assert(program);
    session = cxpr_model_session_new(program, NULL, &error);
    assert(session);
    context = cxpr_model_session_context(session);
    assert(context);
    cxpr_context_set(context, "force", 0.0);

    for (step = 0; step < 2000u; ++step) {
        double energy;
        assert(cxpr_model_session_tick(program, session, NULL, &error));
        energy = output_number(session, "total_energy");
        /* Semi-implicit Euler can oscillate by a small local amount, while
         * damping must reduce the envelope and keep the solution bounded. */
        assert(energy <= previous_energy + 0.0002);
        previous_energy = energy;
        assert(fabs(output_number(session, "position")) <= 1.01);
        assert(fabs(output_number(session, "velocity")) <= 2.01);
    }
    assert(previous_energy < 0.30);

    cxpr_model_session_free(session);
    session = cxpr_model_session_new(program, NULL, &error);
    assert(session);
    context = cxpr_model_session_context(session);
    assert(context);
    cxpr_context_set(context, "force", 0.0);
    assert(cxpr_model_session_tick(program, session, NULL, &error));
    first_position = output_number(session, "position");
    assert(fabs(first_position - 0.9999) <= 1e-12);

    printf("physics oscillator: 2000 ticks, final_energy=%.6f, reset OK\n",
           previous_energy);

    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
    cxpr_doc_free(document);
    return 0;
}
