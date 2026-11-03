#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <cxpr/cxpr.h>

int main(void) {
    static const char source[] =
        "model view_gain\n"
        "in sample: number\n"
        "in gain: number\n"
        "out result: number = sample * gain\n";
    double samples[] = {0.25, -0.5, 0.75, 1.0};
    double gains[] = {2.0, 1.0, 0.5, -1.0};
    double results[4] = {0};
    cxpr_error error = {0};
    cxpr_model* model = cxpr_model_parse(source, &error);
    cxpr_model_compiled* program;
    cxpr_model_session* session;
    cxpr_model_view_binding inputs[] = {
        {"sample", {samples, CXPR_VALUE_NUMBER, 1u, {4u}, {(ptrdiff_t)sizeof(double)},
                    sizeof(samples), CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS}},
        {"gain", {gains, CXPR_VALUE_NUMBER, 1u, {4u}, {(ptrdiff_t)sizeof(double)},
                  sizeof(gains), CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS}}
    };
    cxpr_model_view_binding outputs[] = {
        {"result", {results, CXPR_VALUE_NUMBER, 1u, {4u}, {(ptrdiff_t)sizeof(double)},
                    sizeof(results), CXPR_VIEW_WRITABLE | CXPR_VIEW_CONTIGUOUS}}
    };

    assert(model != NULL);
    program = cxpr_model_compile(model, NULL, &error);
    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &error);
    assert(session != NULL);
    assert(cxpr_model_session_tick_views(program, session, NULL,
                                         inputs, 2u, outputs, 1u, &error));
    assert(fabs(results[0] - 0.5) < 1e-12);
    assert(fabs(results[1] + 0.5) < 1e-12);
    assert(fabs(results[2] - 0.375) < 1e-12);
    assert(fabs(results[3] + 1.0) < 1e-12);

    outputs[0].view.capacity_bytes = sizeof(double);
    assert(!cxpr_model_session_tick_views(program, session, NULL,
                                          inputs, 2u, outputs, 1u, &error));
    assert(error.code == CXPR_ERR_INDEX_OUT_OF_RANGE);

    outputs[0].view.capacity_bytes = sizeof(samples);
    outputs[0].view.data = samples;
    assert(!cxpr_model_session_tick_views(program, session, NULL,
                                          inputs, 2u, outputs, 1u, &error));
    assert(error.code == CXPR_ERR_TYPE_MISMATCH);

    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
    puts("model_views: OK");
    return 0;
}
