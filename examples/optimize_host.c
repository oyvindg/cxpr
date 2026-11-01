#include <cxpr/cxpr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Resolve dimensions by name so the host does not depend on declaration order. */
static size_t dimension_index(const cxpr_model_compiled* program,
                              const char* name) {
    const size_t count = cxpr_model_optimize_dimension_count(program);
    for (size_t i = 0u; i < count; ++i) {
        if (strcmp(cxpr_model_optimize_dimension_at(program, i).name, name) == 0)
            return i;
    }
    return count;
}

int main(void) {
    /* Nested parameter metadata defines the sweep. Each assert that references
       optimize $params constrains which Cartesian-product rows reach the host;
       the declared defaults (3,5,7,9) must themselves satisfy the asserts. */
    const char* source =
        "model host_agnostic_optimizer\n"
        "$kst = { roc = { "
        "r1 = 3 { optimize = [3, 8] }, "
        "r2 = 5 { optimize = [5, 10] }, "
        "r3 = 7 { optimize = [7, 12] }, "
        "r4 = 9 { optimize = [9, 14] } } }\n"
        "assert $kst.roc.r1 <= $kst.roc.r2\n"
        "assert $kst.roc.r2 <= $kst.roc.r3\n"
        "assert $kst.roc.r3 < $kst.roc.r4\n"
        "score = 100 - ($kst.roc.r1 + $kst.roc.r2 + $kst.roc.r3 + $kst.roc.r4)\n"
        "out score { optimize { maximize } }\n";
    const cxpr_model_optimize_inputs inputs = {0};
    cxpr_model_optimize_grid grid = {0};
    cxpr_model_optimize_result result = {0};
    cxpr_error err = {0};
    cxpr_doc* doc = NULL;
    cxpr_model_compiled* program = NULL;
    cxpr_value* outputs = NULL;
    size_t dimension[4];
    size_t output_count;
    int status = EXIT_FAILURE;

    /* Parse and compile once. A production host can retain this program for
       repeated optimization runs with different input series. */
    doc = cxpr_doc_parse_model(source, &err);
    if (!doc) goto fail;
    program = cxpr_model_compile(cxpr_doc_model(doc), NULL, &err);
    if (!program) goto fail;
    if (!cxpr_model_optimize_prepare_grid(program, &inputs, &grid, &err))
        goto fail;

    /* The grid is candidate-major. Resolve the columns used by this host-side
       scoring function from the public dimension metadata. */
    dimension[0] = dimension_index(program, "kst.roc.r1");
    dimension[1] = dimension_index(program, "kst.roc.r2");
    dimension[2] = dimension_index(program, "kst.roc.r3");
    dimension[3] = dimension_index(program, "kst.roc.r4");
    for (size_t i = 0u; i < 4u; ++i) {
        if (dimension[i] == grid.param_count) {
            fprintf(stderr, "missing optimize dimension\n");
            goto cleanup;
        }
    }

    /* Allocate one complete output row per original candidate. Inactive rows
       remain unused, but retain their position in the generated grid. */
    output_count = cxpr_model_compiled_output_count(program);
    outputs = (cxpr_value*)calloc(grid.candidate_count * output_count,
                                  sizeof(*outputs));
    if (!outputs) {
        fprintf(stderr, "out of memory\n");
        goto cleanup;
    }

    /* Host boundary: run active rows using any CPU, GPU, worker, or service.
       Here the host evaluates the model's declared objective
       (score = 100 - sum of periods) directly from the swept parameter values;
       a production host would substitute a backtest or simulation per candidate
       and return its score here. CXPR owns grid construction, constraint
       pruning, and result ranking. */
    for (size_t candidate = 0u; candidate < grid.candidate_count; ++candidate) {
        const cxpr_value* params = &grid.params[candidate * grid.param_count];
        double total = 0.0;
        if (!grid.active[candidate]) continue;
        for (size_t i = 0u; i < 4u; ++i) total += params[dimension[i]].d;
        outputs[candidate * output_count] = cxpr_num(100.0 - total);
    }

    /* Return host-produced output rows to CXPR. It compacts active candidates,
       extracts declared objectives, and applies their ranking direction. */
    if (!cxpr_model_optimize_finalize_results(
            program, &grid, outputs, output_count, &result, &err))
        goto fail;

    printf("prepared %zu candidates, %zu active\n",
           grid.candidate_count, grid.active_count);
    printf("best: r1=%.0f r2=%.0f r3=%.0f r4=%.0f score=%.0f\n",
           result.candidates[result.best_index].params[dimension[0]],
           result.candidates[result.best_index].params[dimension[1]],
           result.candidates[result.best_index].params[dimension[2]],
           result.candidates[result.best_index].params[dimension[3]],
           result.candidates[result.best_index].objectives[0]);

    /* Keep the example executable useful as an end-to-end regression test. */
    status = grid.candidate_count == 16u && grid.active_count == 5u &&
                     result.candidates[result.best_index]
                             .params[dimension[0]] == 3.0 &&
                     result.candidates[result.best_index]
                             .params[dimension[1]] == 5.0 &&
                     result.candidates[result.best_index]
                             .params[dimension[2]] == 7.0 &&
                     result.candidates[result.best_index]
                             .params[dimension[3]] == 9.0 &&
                     result.candidates[result.best_index].objectives[0] == 76.0
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    goto cleanup;

fail:
    fprintf(stderr, "cxpr: %s", err.message ? err.message : "unknown error");
    if (err.line) fprintf(stderr, " at %zu:%zu", err.line, err.column);
    fputc('\n', stderr);
cleanup:
    free(outputs);
    cxpr_model_optimize_result_free(&result);
    cxpr_model_optimize_grid_free(&grid);
    cxpr_model_compiled_free(program);
    cxpr_doc_free(doc);
    return status;
}
