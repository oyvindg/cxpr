#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <cxpr/cxpr.h>

#define FRAMES 4096u
#define TRIALS 100u

static double now_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

int main(void) {
    static const char source[] =
        "model stream_bench\n"
        "in sample\n"
        "state memory = 0\n"
        "filtered = memory + 0.125 * (sample - memory)\n"
        "memory := filtered\n"
        "out result = filtered\n";
    double input[FRAMES];
    double output[FRAMES];
    cxpr_error error = {0};
    cxpr_model* model = cxpr_model_parse(source, &error);
    cxpr_model_compiled* program = model ? cxpr_model_compile(model, NULL, &error) : NULL;
    cxpr_model_session* scalar = program ? cxpr_model_session_new(program, NULL, &error) : NULL;
    cxpr_model_session* views = program ? cxpr_model_session_new(program, NULL, &error) : NULL;
    cxpr_model_view_binding in = {
        "sample", {input, CXPR_VALUE_NUMBER, 1u, {FRAMES}, {(ptrdiff_t)sizeof(double)},
                   sizeof(input), CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS}
    };
    cxpr_model_view_binding out = {
        "result", {output, CXPR_VALUE_NUMBER, 1u, {FRAMES}, {(ptrdiff_t)sizeof(double)},
                   sizeof(output), CXPR_VIEW_WRITABLE | CXPR_VIEW_CONTIGUOUS}
    };
    double scalar_start, scalar_end, view_start, view_end;
    volatile double checksum = 0.0;

    if (!model || !program || !scalar || !views) {
        fprintf(stderr, "view benchmark setup failed: %s\n", error.message ? error.message : "unknown");
        return 1;
    }
    for (size_t i = 0u; i < FRAMES; ++i) input[i] = sin((double)i * 0.013);

    scalar_start = now_ns();
    for (size_t trial = 0u; trial < TRIALS; ++trial) {
        for (size_t i = 0u; i < FRAMES; ++i) {
            double result;
            cxpr_context_set(cxpr_model_session_context(scalar), "sample", input[i]);
            if (!cxpr_model_session_tick(program, scalar, NULL, &error)) return 2;
            if (!cxpr_model_session_get_number(scalar, "result", &result)) return 3;
            checksum += result;
        }
    }
    scalar_end = now_ns();

    view_start = now_ns();
    for (size_t trial = 0u; trial < TRIALS; ++trial) {
        if (!cxpr_model_session_tick_views(program, views, NULL, &in, 1u, &out, 1u, &error)) return 4;
        checksum += output[FRAMES - 1u];
    }
    view_end = now_ns();

    printf("scalar_ns_per_frame=%.2f view_ns_per_frame=%.2f frames_per_view=%u checksum=%.6f\n",
           (scalar_end - scalar_start) / (FRAMES * TRIALS),
           (view_end - view_start) / (FRAMES * TRIALS), FRAMES, checksum);
    cxpr_model_session_free(views);
    cxpr_model_session_free(scalar);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
    return 0;
}
