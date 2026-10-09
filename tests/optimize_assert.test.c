#include <assert.h>
#include <cxpr/cxpr.h>
#include <stdio.h>
#include <string.h>

static cxpr_model_compiled* compile_source(const char* source,
                                           cxpr_doc** out_doc,
                                           cxpr_error* err) {
    cxpr_doc* doc = cxpr_doc_parse_model(source, err);
    cxpr_model_compiled* program;
    assert(doc != NULL);
    program = cxpr_model_compile(cxpr_doc_model(doc), NULL, err);
    *out_doc = doc;
    return program;
}

static void test_assert_passes_and_reports_description(void) {
    const char* passing =
        "model passing\n$fast = 5\n$slow = 10\n"
        "assert $fast < $slow { description = \"Fast must be shorter\" }\n"
        "out value = $fast\n";
    const char* failing =
        "model failing\n$fast = 15\n$slow = 10\n"
        "assert $fast < $slow { description = \"Fast must be shorter\" }\n"
        "out value = $fast\n";
    cxpr_error err = {0};
    cxpr_doc* doc;
    cxpr_model_compiled* program = compile_source(passing, &doc, &err);
    cxpr_model_session* session;
    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &err);
    assert(session != NULL);
    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
    cxpr_doc_free(doc);

    program = compile_source(failing, &doc, &err);
    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &err);
    assert(session == NULL);
    assert(err.code == CXPR_ERR_ASSERTION_FAILED);
    assert(strcmp(err.message, "Fast must be shorter") == 0);
    cxpr_model_compiled_free(program);
    cxpr_doc_free(doc);
}

static void test_optimize_constraint_is_inert_and_introspectable(void) {
    const char* source =
        "model optimize_api\n"
        "$fast = 20 { optimize { values = [10, 20] } }\n"
        "$slow = 10 { optimize { min = 10, max = 30, step = 10 } }\n"
        "optimize $fast < $slow { description = \"ordered\" }\n"
        "score = $slow - $fast { optimize { maximize } }\n"
        "out score\n";
    cxpr_error err = {0};
    cxpr_doc* doc;
    cxpr_model_compiled* program = compile_source(source, &doc, &err);
    cxpr_model_session* session;
    cxpr_model_optimize_dimension dim;
    cxpr_model_optimize_objective objective;
    assert(program != NULL);
    assert(cxpr_model_optimize_dimension_count(program) == 2u);
    dim = cxpr_model_optimize_dimension_at(program, 0u);
    assert(strcmp(dim.name, "fast") == 0);
    assert(dim.kind == CXPR_OPT_DIM_VALUES && dim.value_count == 2u);
    assert(cxpr_model_optimize_constraint_count(program) == 1u);
    assert(cxpr_model_optimize_constraint_at(program, 0u) != NULL);
    assert(cxpr_model_optimize_objective_count(program) == 1u);
    objective = cxpr_model_optimize_objective_at(program, 0u);
    assert(strcmp(objective.name, "score") == 0);
    assert(objective.direction == CXPR_OPT_MAXIMIZE);
    session = cxpr_model_session_new(program, NULL, &err);
    assert(session != NULL); /* The violated optimizer constraint is not an invariant. */
    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
    cxpr_doc_free(doc);
}

static void test_assert_rejects_runtime_identifiers(void) {
    const char* source =
        "model invalid_assert\nin close\n$limit = 5\n"
        "assert close > $limit\nout close\n";
    cxpr_error err = {0};
    cxpr_doc* doc;
    cxpr_model_compiled* program = compile_source(source, &doc, &err);
    assert(program == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(strstr(err.message, "only reference $params") != NULL);
    cxpr_doc_free(doc);
}

static void test_serial_candidate_runner_selects_best_allowed_candidate(void) {
    const char* source =
        "model runner\n"
        "$period = 1 { optimize { values = [1, 2, 3] } }\n"
        "optimize $period < 3\n"
        "score = $period\n"
        "out score { optimize { maximize } }\n";
    cxpr_error err = {0};
    cxpr_doc* doc;
    cxpr_model_compiled* program = compile_source(source, &doc, &err);
    cxpr_model_optimize_inputs inputs = {0};
    cxpr_model_optimize_result result = {0};
    inputs.tick_count = 1u;
    assert(program != NULL);
    assert(cxpr_model_optimize(program, &inputs, NULL, &result, &err));
    assert(result.candidate_count == 2u);
    assert(result.candidates[result.best_index].params[0] == 2.0);
    assert(result.candidates[result.best_index].objectives[0] == 2.0);
    cxpr_model_optimize_result_free(&result);
    cxpr_model_compiled_free(program);
    cxpr_doc_free(doc);
}

static void test_prepare_grid_and_finalize_results(void) {
    const char* source =
        "model backend_helpers\n"
        "$period = 1 { optimize { values = [1, 2, 3] } }\n"
        "optimize $period < 3\n"
        "score = $period\n"
        "out score { optimize { maximize } }\n";
    cxpr_model_optimize_inputs inputs = {0};
    cxpr_model_optimize_grid grid = {0};
    cxpr_model_optimize_result result = {0};
    cxpr_value outputs[] = {cxpr_num(10.0), cxpr_num(30.0), cxpr_num(999.0)};
    cxpr_error err = {0};
    cxpr_doc* doc;
    cxpr_model_compiled* program = compile_source(source, &doc, &err);
    inputs.tick_count = 1u;
    assert(program != NULL);
    assert(cxpr_model_optimize_prepare_grid(program, &inputs, &grid, &err));
    assert(grid.candidate_count == 3u && grid.param_count == 1u);
    assert(grid.active_count == 2u);
    assert(grid.params[0].d == 1.0 && grid.params[1].d == 2.0);
    assert(grid.active[0] && grid.active[1] && !grid.active[2]);
    assert(cxpr_model_optimize_finalize_results(
        program, &grid, outputs, 1u, &result, &err));
    assert(result.candidate_count == 2u && result.best_index == 1u);
    assert(result.candidates[1].params[0] == 2.0);
    assert(result.candidates[1].objectives[0] == 30.0);
    cxpr_model_optimize_result_free(&result);
    cxpr_model_optimize_grid_free(&grid);
    cxpr_model_compiled_free(program);
    cxpr_doc_free(doc);
}

static void test_candidate_objective_reads_committed_state_output(void) {
    const char* source =
        "model state_objective\n"
        "in sample\n"
        "$gain = 1 { optimize { values = [1] } }\n"
        "state total = 5\n"
        "total := total + sample * $gain\n"
        "out total { optimize { maximize } }\n";
    const char* names[] = {"sample"};
    const double values[] = {1.0, 2.0, 3.0};
    cxpr_model_optimize_inputs inputs = {names, values, 1u, 3u};
    cxpr_model_optimize_result result = {0};
    cxpr_error err = {0};
    cxpr_doc* doc;
    cxpr_model_compiled* program = compile_source(source, &doc, &err);

    assert(program != NULL);
    assert(cxpr_model_optimize(program, &inputs, NULL, &result, &err));
    assert(result.candidate_count == 1u);
    assert(result.candidates[0].objectives[0] == 11.0);
    cxpr_model_optimize_result_free(&result);
    cxpr_model_compiled_free(program);
    cxpr_doc_free(doc);
}

typedef struct {
    int available;
    int run_count;
} fake_backend_state;

static bool fake_backend_available(void* userdata) {
    return ((fake_backend_state*)userdata)->available != 0;
}

static bool fake_backend_run(
    void* userdata, const cxpr_model_compiled* program,
    const cxpr_model_optimize_inputs* inputs,
    const cxpr_model_optimize_options* options,
    cxpr_model_optimize_result* result, cxpr_error* err) {
    cxpr_model_optimize_options cpu_options = options ? *options
                                                      : (cxpr_model_optimize_options){0};
    ((fake_backend_state*)userdata)->run_count++;
    cpu_options.backend = CXPR_OPT_BACKEND_CPU;
    return cxpr_model_optimize(program, inputs, &cpu_options, result, err);
}

static void test_host_backend_selection_and_fallback(void) {
    const char* source =
        "model backend_selection\n"
        "$value = 1 { optimize { values = [1, 2] } }\n"
        "score = $value { optimize { maximize } }\n"
        "out score\n";
    cxpr_model_optimize_inputs inputs = {0};
    cxpr_model_optimize_options options = {0};
    cxpr_model_optimize_result result = {0};
    cxpr_error err = {0};
    cxpr_doc* doc;
    cxpr_model_compiled* program = compile_source(source, &doc, &err);
    fake_backend_state state = {1, 0};
    const cxpr_model_optimize_backend backend = {
        CXPR_OPTIMIZE_BACKEND_API_VERSION,
        &state,
        fake_backend_available,
        fake_backend_run,
    };

    inputs.tick_count = 1u;
    assert(program != NULL);
    assert(cxpr_model_optimize_with_backend(
        program, &inputs, &options, &backend, &result, &err));
    assert(state.run_count == 1);
    assert(result.candidates[result.best_index].params[0] == 2.0);
    cxpr_model_optimize_result_free(&result);

    options.backend = CXPR_OPT_BACKEND_CPU;
    assert(cxpr_model_optimize_with_backend(
        program, &inputs, &options, &backend, &result, &err));
    assert(state.run_count == 1);
    cxpr_model_optimize_result_free(&result);

    state.available = 0;
    options.backend = CXPR_OPT_BACKEND_AUTO;
    assert(cxpr_model_optimize_with_backend(
        program, &inputs, &options, &backend, &result, &err));
    assert(state.run_count == 1);
    cxpr_model_optimize_result_free(&result);

    options.backend = CXPR_OPT_BACKEND_HOST;
    assert(!cxpr_model_optimize_with_backend(
        program, &inputs, &options, &backend, &result, &err));
    assert(err.code == CXPR_ERR_UNAVAILABLE);

    cxpr_model_compiled_free(program);
    cxpr_doc_free(doc);
}

int main(void) {
    test_assert_passes_and_reports_description();
    test_optimize_constraint_is_inert_and_introspectable();
    test_assert_rejects_runtime_identifiers();
    test_serial_candidate_runner_selects_best_allowed_candidate();
    test_prepare_grid_and_finalize_results();
    test_candidate_objective_reads_committed_state_output();
    test_host_backend_selection_and_fallback();
    printf("All optimize/assert tests passed.\n");
    return 0;
}
