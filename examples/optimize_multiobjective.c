#include <cxpr/cxpr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Resolve objective columns by name so the host does not depend on the order
   in which objectives were declared in the model. */
static size_t objective_index(const cxpr_model_compiled* program,
                              const char* name) {
    const size_t count = cxpr_model_optimize_objective_count(program);
    for (size_t i = 0u; i < count; ++i) {
        if (strcmp(cxpr_model_optimize_objective_at(program, i).name, name) == 0)
            return i;
    }
    return count;
}

int main(void) {
    /* Two $params drive the sweep. An assert that references them prunes
       candidates (and guards the declared defaults). Two outputs declare
       objectives in opposite directions: maximise return, minimise risk.

       CXPR records every declared objective for each candidate and ranks the
       sweep by the FIRST declared objective's direction (here: maximise
       return_score). The remaining objectives are reported alongside, so a host
       can apply its own multi-objective policy (e.g. Pareto filtering). */
    const char* source =
        "model multi_objective\n"
        "$lookback = 5 { optimize { values = [5, 10, 20] } }\n"
        "$threshold = 1 { optimize { min = 1, max = 3, step = 1 } }\n"
        "assert $lookback >= $threshold * 4\n"
        "return_score = $lookback * $threshold\n"
        "risk = $threshold * $threshold\n"
        "out return_score { optimize { maximize } }\n"
        "out risk { optimize { minimize } }\n";
    cxpr_model_optimize_inputs inputs = {0};
    cxpr_model_optimize_result result = {0};
    cxpr_error err = {0};
    cxpr_doc* doc = NULL;
    cxpr_model_compiled* program = NULL;
    size_t return_col;
    size_t risk_col;
    size_t lookback_col;
    size_t threshold_col;
    int status = EXIT_FAILURE;

    doc = cxpr_doc_parse_model(source, &err);
    if (!doc) goto fail;
    program = cxpr_model_compile(cxpr_doc_model(doc), NULL, &err);
    if (!program) goto fail;

    /* A params-only model still advances one tick so the bindings evaluate. */
    inputs.tick_count = 1u;
    if (!cxpr_model_optimize(program, &inputs, NULL, &result, &err)) goto fail;

    return_col = objective_index(program, "return_score");
    risk_col = objective_index(program, "risk");
    lookback_col = 0u;
    threshold_col = 1u;
    for (size_t i = 0u; i < cxpr_model_optimize_dimension_count(program); ++i) {
        const char* name = cxpr_model_optimize_dimension_at(program, i).name;
        if (strcmp(name, "lookback") == 0) lookback_col = i;
        else if (strcmp(name, "threshold") == 0) threshold_col = i;
    }
    if (return_col == result.objective_count ||
        risk_col == result.objective_count) {
        fprintf(stderr, "missing objective column\n");
        goto cleanup;
    }

    printf("ranked %zu candidates by return_score (maximize)\n",
           result.candidate_count);
    for (size_t row = 0u; row < result.candidate_count; ++row) {
        printf("  lookback=%.0f threshold=%.0f -> return=%.0f risk=%.0f%s\n",
               result.candidates[row].params[lookback_col],
               result.candidates[row].params[threshold_col],
               result.candidates[row].objectives[return_col],
               result.candidates[row].objectives[risk_col],
               row == result.best_index ? "  <= best" : "");
    }

    /* Best by the leading objective is (lookback=20, threshold=3): return 60,
       risk 9. Keep the example usable as an end-to-end regression test. */
    status = result.candidate_count == 6u &&
                     result.candidates[result.best_index]
                             .params[lookback_col] == 20.0 &&
                     result.candidates[result.best_index]
                             .params[threshold_col] == 3.0 &&
                     result.candidates[result.best_index]
                             .objectives[return_col] == 60.0 &&
                     result.candidates[result.best_index]
                             .objectives[risk_col] == 9.0
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    goto cleanup;

fail:
    fprintf(stderr, "cxpr: %s", err.message ? err.message : "unknown error");
    if (err.line) fprintf(stderr, " at %zu:%zu", err.line, err.column);
    fputc('\n', stderr);
cleanup:
    cxpr_model_optimize_result_free(&result);
    cxpr_model_compiled_free(program);
    cxpr_doc_free(doc);
    return status;
}
