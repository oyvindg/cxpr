#include <assert.h>
#include <cxpr/cxpr.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int calls;
    int generated_source;
} host_backend_fixture;

static bool host_available(void* userdata) {
    (void)userdata;
    return true;
}

static bool host_run(
    void* userdata, const cxpr_model_compiled* program,
    const cxpr_model_optimize_inputs* inputs,
    const cxpr_model_optimize_options* options,
    cxpr_model_optimize_result* result, cxpr_error* err) {
    host_backend_fixture* fixture = (host_backend_fixture*)userdata;
    const cxpr_cuda_plugin_options cuda_options = {
        "integration_candidate_tick", "static __device__ __forceinline__"};
    cxpr_model_optimize_options cpu_options = options ? *options
                                                      : (cxpr_model_optimize_options){0};
    char* source = cxpr_cuda_plugin_source_from_program(
        program, &cuda_options, err);
    fixture->calls++;
    if (!source) return false;
    fixture->generated_source =
        strstr(source, "integration_candidate_tick") != NULL;
    cxpr_cuda_plugin_source_free(source);
    if (!fixture->generated_source) return false;

    /* A cxcu host would compile/launch above source. This fixture uses the CPU
       executor to validate the public backend/result ownership contract. */
    cpu_options.backend = CXPR_OPT_BACKEND_CPU;
    return cxpr_model_optimize(
        program, inputs, &cpu_options, result, err);
}

int main(void) {
    const char* source =
        "model host_backend_integration\n"
        "in sample\n"
        "$gain = 1 { optimize { values = [1, 2, 4] } }\n"
        "optimize $gain <= 2\n"
        "state total = 5\n"
        "total := total + sample * $gain\n"
        "out total { optimize { maximize } }\n";
    const char* names[] = {"sample"};
    const double values[] = {1.0, 2.0, 3.0};
    const cxpr_model_optimize_inputs inputs = {names, values, 1u, 3u};
    cxpr_model_optimize_options options = {0};
    cxpr_model_optimize_result accelerated = {0};
    cxpr_model_optimize_result reference = {0};
    cxpr_error err = {0};
    cxpr_model* model = cxpr_model_parse(source, &err);
    cxpr_model_compiled* program = model
        ? cxpr_model_compile(model, NULL, &err) : NULL;
    host_backend_fixture fixture = {0};
    const cxpr_model_optimize_backend backend = {
        CXPR_OPTIMIZE_BACKEND_API_VERSION,
        &fixture,
        host_available,
        host_run,
    };

    assert(program != NULL);
    options.backend = CXPR_OPT_BACKEND_HOST;
    assert(cxpr_model_optimize_with_backend(
        program, &inputs, &options, &backend, &accelerated, &err));
    options.backend = CXPR_OPT_BACKEND_CPU;
    assert(cxpr_model_optimize(
        program, &inputs, &options, &reference, &err));

    assert(fixture.calls == 1 && fixture.generated_source);
    assert(accelerated.candidate_count == reference.candidate_count);
    assert(accelerated.best_index == reference.best_index);
    for (size_t i = 0u; i < reference.candidate_count; ++i) {
        assert(accelerated.candidates[i].params[0] ==
               reference.candidates[i].params[0]);
        assert(accelerated.candidates[i].objectives[0] ==
               reference.candidates[i].objectives[0]);
    }

    cxpr_model_optimize_result_free(&reference);
    cxpr_model_optimize_result_free(&accelerated);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
    return 0;
}
