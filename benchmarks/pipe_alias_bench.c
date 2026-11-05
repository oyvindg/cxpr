#include <cxpr/cxpr.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct bench_model {
    cxpr_model* model;
    cxpr_model_compiled* program;
    cxpr_model_session* session;
} bench_model;

static volatile double bench_sink = 0.0;

static long long now_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void fail(const char* label, const cxpr_error* err) {
    fprintf(stderr, "%s: %s\n", label,
            err && err->message ? err->message : "unknown error");
    abort();
}

static bench_model compile_model(const char* source) {
    cxpr_error err = {0};
    bench_model result = {0};
    result.model = cxpr_model_parse(source, &err);
    if (!result.model) fail("parse", &err);
    result.program = cxpr_model_compile(result.model, NULL, &err);
    if (!result.program) fail("compile", &err);
    result.session = cxpr_model_session_new(result.program, NULL, &err);
    if (!result.session) fail("session", &err);
    return result;
}

static bench_model compile_parent(const char* source,
                                  const char* import_name,
                                  const cxpr_model_compiled* child) {
    cxpr_error err = {0};
    cxpr_model_import import = {.name = import_name, .program = child};
    bench_model result = {0};
    result.model = cxpr_model_parse(source, &err);
    if (!result.model) fail("parent parse", &err);
    result.program = cxpr_model_compile_with_imports(
        result.model, NULL, &import, 1u, &err);
    if (!result.program) fail("parent compile", &err);
    result.session = cxpr_model_session_new(result.program, NULL, &err);
    if (!result.session) fail("parent session", &err);
    return result;
}

static void free_model(bench_model* model) {
    if (!model) return;
    cxpr_model_session_free(model->session);
    cxpr_model_compiled_free(model->program);
    cxpr_model_free(model->model);
    *model = (bench_model){0};
}

static double tick_rooted(bench_model* model, double left, double right) {
    cxpr_error err = {0};
    double output = 0.0;
    cxpr_context* context = cxpr_model_session_context(model->session);
    cxpr_context_set(context, "stereo.left", left);
    cxpr_context_set(context, "stereo.right", right);
    if (!cxpr_model_session_tick(model->program, model->session, NULL, &err))
        fail("rooted tick", &err);
    if (!cxpr_model_session_get_number(model->session, "value", &output))
        fail("rooted output", &err);
    return output;
}

static double tick_parent(bench_model* model, double left, double right) {
    cxpr_error err = {0};
    double output = 0.0;
    cxpr_context* context = cxpr_model_session_context(model->session);
    cxpr_context_set(context, "a", left);
    cxpr_context_set(context, "b", right);
    if (!cxpr_model_session_tick(model->program, model->session, NULL, &err))
        fail("pipe tick", &err);
    if (!cxpr_model_session_get_number(model->session, "value", &output))
        fail("pipe output", &err);
    return output;
}

static double time_rooted(bench_model* model, size_t iterations) {
    double total = 0.0;
    const long long start = now_ns();
    for (size_t i = 0u; i < iterations; ++i) {
        const double left = 1.0 + (double)(i & 15u);
        total += tick_rooted(model, left, left * 0.5);
    }
    bench_sink += total;
    return (double)(now_ns() - start) / (double)iterations;
}

static double time_parent(bench_model* model, size_t iterations) {
    double total = 0.0;
    const long long start = now_ns();
    for (size_t i = 0u; i < iterations; ++i) {
        const double left = 1.0 + (double)(i & 15u);
        total += tick_parent(model, left, left * 0.5);
    }
    bench_sink += total;
    return (double)(now_ns() - start) / (double)iterations;
}

static double time_compile(const char* source, size_t iterations) {
    const long long start = now_ns();
    for (size_t i = 0u; i < iterations; ++i) {
        cxpr_error err = {0};
        cxpr_model* model = cxpr_model_parse(source, &err);
        cxpr_model_compiled* program;
        if (!model) fail("timed parse", &err);
        program = cxpr_model_compile(model, NULL, &err);
        if (!program) fail("timed compile", &err);
        cxpr_model_compiled_free(program);
        cxpr_model_free(model);
    }
    return (double)(now_ns() - start) / (double)iterations;
}

static void require_equal(const char* label, double actual, double expected) {
    if (fabs(actual - expected) > 1e-12) {
        fprintf(stderr, "%s mismatch: %.17g != %.17g\n",
                label, actual, expected);
        abort();
    }
}

int main(int argc, char** argv) {
    const size_t iterations = argc > 1 ? (size_t)strtoull(argv[1], NULL, 10)
                                       : 1000000u;
    const size_t compile_iterations = iterations < 1000u ? iterations : 1000u;
    static const char* qualified_source =
        "model qualified\n"
        "in stereo { left, right }\n"
        "out value = stereo.left + stereo.right\n";
    static const char* implicit_source =
        "model implicit\n"
        "in stereo { left, right }\n"
        "out value = left + right\n";
    static const char* inferred_source =
        "model inferred\n"
        "in stereo\n"
        "out value = stereo.left + stereo.right\n";
    static const char* explicit_source =
        "model explicit\n"
        "in stereo { left as channel_left, right as channel_right }\n"
        "out value = channel_left + channel_right\n";
    static const char* pipe_child_source =
        "model stereo_passthrough { pipe = \"stereo\" }\n"
        "in stereo { left, right }\n"
        "out { left = stereo.left, right = stereo.right }\n";
    static const char* parent_source =
        "model parent\n"
        "use stereo_passthrough\n"
        "in { a, b }\n"
        "stereo = { left = a, right = b }\n"
        "processed = stereo |> stereo_passthrough()\n"
        "out value = processed.left + processed.right\n";
    bench_model qualified = compile_model(qualified_source);
    bench_model implicit = compile_model(implicit_source);
    bench_model inferred = compile_model(inferred_source);
    bench_model explicit_alias = compile_model(explicit_source);
    bench_model pipe_child = compile_model(pipe_child_source);
    bench_model pipe_parent = compile_parent(
        parent_source, "stereo_passthrough", pipe_child.program);
    const double expected = 6.0;

    if (iterations == 0u) {
        fprintf(stderr, "iterations must be positive\n");
        return 2;
    }
    require_equal("qualified", tick_rooted(&qualified, 2.0, 4.0), expected);
    require_equal("implicit", tick_rooted(&implicit, 2.0, 4.0), expected);
    require_equal("inferred", tick_rooted(&inferred, 2.0, 4.0), expected);
    require_equal("explicit alias", tick_rooted(&explicit_alias, 2.0, 4.0), expected);
    require_equal("pipe", tick_parent(&pipe_parent, 2.0, 4.0), expected);

    printf("mode,ns_per_tick\n");
    printf("qualified_rooted,%.3f\n", time_rooted(&qualified, iterations));
    printf("implicit_alias,%.3f\n", time_rooted(&implicit, iterations));
    printf("usage_inferred,%.3f\n", time_rooted(&inferred, iterations));
    printf("explicit_as_alias,%.3f\n", time_rooted(&explicit_alias, iterations));
    printf("pipe_metadata,%.3f\n", time_parent(&pipe_parent, iterations));
    printf("compile_mode,ns_per_compile\n");
    printf("explicit_rooted,%.3f\n",
           time_compile(qualified_source, compile_iterations));
    printf("usage_inferred,%.3f\n",
           time_compile(inferred_source, compile_iterations));
    printf("pipe_metadata,%.3f\n",
           time_compile(pipe_child_source, compile_iterations));
    free_model(&pipe_parent);
    free_model(&pipe_child);
    free_model(&explicit_alias);
    free_model(&implicit);
    free_model(&inferred);
    free_model(&qualified);
    return bench_sink == -1.0 ? 1 : 0;
}
