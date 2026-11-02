#include <cxpr/cxpr.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef CXPR_BENCH_FIXTURE_DIR
#define CXPR_BENCH_FIXTURE_DIR "."
#endif

enum { TRIALS = 9, EVALUATIONS = 1000000, COMPILES = 1000 };
static volatile double sink;

static double now_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static int compare_double(const void* left, const void* right) {
    const double a = *(const double*)left;
    const double b = *(const double*)right;
    return (a > b) - (a < b);
}

static char* read_fixture(const char* name) {
    char path[1024];
    FILE* file;
    long size;
    char* source;
    snprintf(path, sizeof(path), "%s/%s", CXPR_BENCH_FIXTURE_DIR, name);
    file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0) {
        if (file) fclose(file);
        return NULL;
    }
    rewind(file);
    source = (char*)malloc((size_t)size + 1u);
    if (!source || fread(source, 1u, (size_t)size, file) != (size_t)size) {
        free(source);
        fclose(file);
        return NULL;
    }
    source[size] = '\0';
    fclose(file);
    return source;
}

static void run_case(const char* name, const char* fixture) {
    cxpr_model_compile_options options = {CXPR_MODEL_BACKEND_IR, true, false};
    double eval_samples[TRIALS];
    double compile_samples[TRIALS];
    size_t instructions = 0u;
    char* source = read_fixture(fixture);
    if (!source) abort();
    for (size_t trial = 0u; trial < TRIALS; ++trial) {
        cxpr_error err = {0};
        cxpr_model* model = cxpr_model_parse(source, &err);
        cxpr_model_compiled* program;
        cxpr_model_session* session;
        cxpr_context* context;
        double result = 0.0;
        double start;
        if (!model) abort();
        start = now_ns();
        for (size_t i = 0u; i < COMPILES; ++i) {
            program = cxpr_model_compile_with_options(
                model, NULL, &options, &err);
            if (!program || !cxpr_model_compiled_uses_fast_path(program)) abort();
            instructions = cxpr_model_compiled_fast_path_instruction_count(program);
            cxpr_model_compiled_free(program);
        }
        compile_samples[trial] = (now_ns() - start) / (double)COMPILES;
        program = cxpr_model_compile_with_options(model, NULL, &options, &err);
        session = program ? cxpr_model_session_new(program, NULL, &err) : NULL;
        if (!session) abort();
        context = cxpr_model_session_context(session);
        cxpr_context_set(context, "a", 1.5);
        cxpr_context_set(context, "b", 2.5);
        cxpr_context_set(context, "c", 3.5);
        cxpr_context_set(context, "d", 4.5);
        start = now_ns();
        for (size_t i = 0u; i < EVALUATIONS; ++i) {
            if (!cxpr_model_session_tick_fast(program, session, NULL, &err) ||
                !cxpr_model_session_get_number(session, "result", &result)) abort();
            sink += result;
        }
        eval_samples[trial] = (now_ns() - start) / (double)EVALUATIONS;
        if (fabs(result - 8.75) > 1e-12) abort();
        cxpr_model_session_free(session);
        cxpr_model_compiled_free(program);
        cxpr_model_free(model);
    }
    qsort(eval_samples, TRIALS, sizeof(eval_samples[0]), compare_double);
    qsort(compile_samples, TRIALS, sizeof(compile_samples[0]), compare_double);
    printf("%-24s %12.2f %14.0f %12zu\n", name,
           eval_samples[TRIALS / 2], compile_samples[TRIALS / 2], instructions);
    free(source);
}

int main(void) {
    printf("case                       IR ns/tick   compile ns   instructions\n");
    run_case("named_map_pure_manual", "ir_named_map_pure_manual.cxpr");
    run_case("named_map_pure", "ir_named_map_pure.cxpr");
    fprintf(stderr, "sink=%.6f\n", sink);
    return 0;
}
