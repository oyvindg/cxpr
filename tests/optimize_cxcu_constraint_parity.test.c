/* Standalone cxpr <-> cxcu (CUDA) parity for optimize-scoped assert pruning.
   The model declares an `assert` that references optimize $params, so it prunes
   candidates. This verifies the CPU and the CUDA host backend agree on the same
   reduced active set, ranking, and objective values. */
#include <assert.h>
#include <cxpr/cxpr.h>
#include <cxpr/backends/cxcu.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#ifndef CXPR_CXCU_TEST_INCLUDE_OPTION
#define CXPR_CXCU_TEST_INCLUDE_OPTION NULL
#endif

int main(void) {
    static const char source[] =
        "model cxcu_constraint_parity\n"
        "in sample\n"
        "$gain = 1 { optimize { values = [1, 2, 4] } }\n"
        "$bias = 0 { optimize { values = [-1, 3] } }\n"
        "assert $gain + $bias <= 4\n"
        "state score = 5\n"
        "score := score + sample * $gain + $bias\n"
        "out score { optimize { maximize } }\n";
    static const char* input_names[] = {"sample"};
    static const double input_values[] = {1.0, 2.0, -0.5, 3.0};
    const cxpr_model_optimize_inputs inputs = {input_names, input_values, 1u, 4u};
    const cxpr_cxcu_backend_options backend_options = {
        CXPR_CXCU_TEST_INCLUDE_OPTION, 0};
    cxpr_model_optimize_options options = {0};
    cxpr_model_optimize_result cpu = {0};
    cxpr_model_optimize_result cuda = {0};
    cxpr_model_optimize_backend backend = {0};
    cxpr_error error = {0};
    cxpr_model* model = cxpr_model_parse(source, &error);
    cxpr_model_compiled* program = model
        ? cxpr_model_compile(model, NULL, &error) : NULL;
    int status = 1;

    if (!program) {
        fprintf(stderr, "cxcu constraint parity fixture failed to compile: %s\n",
                error.message ? error.message : "unknown error");
        goto cleanup;
    }
    cxpr_cxcu_backend_init(&backend, &backend_options);
    if (!backend.available || !backend.available(backend.userdata)) {
        printf("cxpr cxcu constraint parity skipped: cxcu unavailable\n");
        status = 77;
        goto cleanup;
    }

    options.backend = CXPR_OPT_BACKEND_CPU;
    if (!cxpr_model_optimize(program, &inputs, &options, &cpu, &error)) {
        fprintf(stderr, "CPU optimize failed: %s\n", error.message);
        goto cleanup;
    }
    options.backend = CXPR_OPT_BACKEND_HOST;
    if (!cxpr_model_optimize_with_backend(
            program, &inputs, &options, &backend, &cuda, &error)) {
        fprintf(stderr, "cxcu optimize failed: %s\n", error.message);
        goto cleanup;
    }

    /* gain{1,2,4} x bias{-1,3} = 6 combinations; `gain + bias <= 4` prunes
       (2,3) and (4,3), leaving 4 active candidates on both backends. */
    assert(cpu.candidate_count == 4u);
    assert(cuda.candidate_count == cpu.candidate_count);
    assert(cuda.param_count == cpu.param_count);
    assert(cuda.objective_count == cpu.objective_count);
    assert(cuda.best_index == cpu.best_index);
    for (size_t i = 0u; i < cpu.candidate_count; ++i) {
        assert(cpu.candidates[i].params[0] + cpu.candidates[i].params[1] <= 4.0);
        for (size_t p = 0u; p < cpu.param_count; ++p)
            assert(cpu.candidates[i].params[p] == cuda.candidates[i].params[p]);
        assert(fabs(cpu.candidates[i].objectives[0] -
                    cuda.candidates[i].objectives[0]) < 1e-12);
    }
    puts("cxpr CPU / cxcu constraint-pruning parity: OK");
    status = 0;

cleanup:
    cxpr_model_optimize_result_free(&cuda);
    cxpr_model_optimize_result_free(&cpu);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
    return status;
}
