#include <cxpr/backends/cxcu.h>

#include <cxpr/cxpr.h>
#include <cxcu/cxcu.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cxpr_cxcu_set_error(cxpr_error* err, cxpr_error_code code,
                                const char* message) {
    if (!err) return;
    *err = (cxpr_error){0};
    err->code = code;
    err->message = message;
}

static void cxpr_cxcu_translate_error(
    cxpr_error* err, cxpr_error_code code, const char* operation,
    const cxcu_error* cxerr) {
    static _Thread_local char message[4096];
    (void)snprintf(message, sizeof(message), "%s: %s", operation,
                   cxerr && cxerr->message[0]
                       ? cxerr->message : "unknown cxcu error");
    cxpr_cxcu_set_error(err, code, message);
}

static bool cxpr_cxcu_available(void* userdata) {
    cxcu_error error = {0};
    (void)userdata;
    return cxcu_available(&error);
}

static char* cxpr_cxcu_compose_source(const cxpr_model_compiled* program,
                                      size_t output_count, cxpr_error* err) {
    static const char kernel_format[] =
        "\n#include <cxpr/cuda_candidate_runtime.cuh>\n"
        "CXPR_CUDA_DEFINE_RESIDENT_CANDIDATE_REPLAY_KERNEL(\n"
        "  cxpr_cxcu_optimize_replay, cxpr_cxcu_candidate_tick_state,\n"
        "  cxpr_cxcu_candidate_tick, %zu)\n";
    const cxpr_cuda_plugin_options options = {
        "cxpr_cxcu_candidate_tick", "static __host__ __device__ inline"};
    char* model_source = cxpr_cuda_plugin_source_from_program(program, &options, err);
    char* source;
    size_t model_size;
    size_t kernel_size;
    if (!model_source) return NULL;
    model_size = strlen(model_source);
    kernel_size = (size_t)snprintf(NULL, 0, kernel_format, output_count);
    source = (char*)malloc(model_size + kernel_size + 1u);
    if (!source) {
        cxpr_cuda_plugin_source_free(model_source);
        cxpr_cxcu_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                            "Out of memory composing CXPR CUDA source");
        return NULL;
    }
    memcpy(source, model_source, model_size);
    (void)snprintf(source + model_size, kernel_size + 1u,
                   kernel_format, output_count);
    cxpr_cuda_plugin_source_free(model_source);
    return source;
}

static bool cxpr_cxcu_run(
    void* userdata, const cxpr_model_compiled* program,
    const cxpr_model_optimize_inputs* inputs,
    const cxpr_model_optimize_options* options,
    cxpr_model_optimize_result* result, cxpr_error* err) {
    const cxpr_cxcu_backend_options* backend_options =
        (const cxpr_cxcu_backend_options*)userdata;
    const char* compile_options[5];
    const size_t output_count = cxpr_model_compiled_output_count(program);
    cxpr_model_optimize_grid grid = {0};
    cxpr_value* typed_inputs = NULL;
    cxpr_value* outputs = NULL;
    unsigned char* states = NULL;
    char* cuda_source = NULL;
    cxcu_module_image image = {0};
    cxcu_module module = {0};
    cxcu_error cxerr = {0};
    char kernel_name[256] = {0};
    size_t state_size = 0u;
    size_t compile_option_count = 0u;
    int ok = 0;

    compile_options[compile_option_count++] = "--std=c++17";
    if (backend_options && backend_options->cxpr_include_option)
        compile_options[compile_option_count++] = backend_options->cxpr_include_option;
    compile_options[compile_option_count++] = "--fmad=false";
    compile_options[compile_option_count++] = "--device-as-default-execution-space";
    compile_options[compile_option_count++] = "--relocatable-device-code=true";

    if (output_count == 0u || !inputs || inputs->input_count !=
        cxpr_model_compiled_input_count(program)) {
        cxpr_cxcu_set_error(err, CXPR_ERR_TYPE_MISMATCH,
                            "CXPR CUDA backend input/output shape mismatch");
        goto cleanup;
    }
    if (!cxpr_model_optimize_prepare_grid(program, inputs, &grid, err)) goto cleanup;
    if (options && options->max_candidates &&
        grid.candidate_count > options->max_candidates) {
        cxpr_cxcu_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                            "Optimize candidate limit exceeded");
        goto cleanup;
    }
    typed_inputs = (cxpr_value*)calloc(inputs->tick_count * inputs->input_count,
                                       sizeof(*typed_inputs));
    outputs = (cxpr_value*)calloc(grid.candidate_count * output_count,
                                  sizeof(*outputs));
    if (!typed_inputs || !outputs) goto oom;
    for (size_t i = 0u; i < inputs->tick_count * inputs->input_count; ++i)
        typed_inputs[i] = cxpr_num(inputs->values[i]);

    cuda_source = cxpr_cxcu_compose_source(program, output_count, err);
    if (!cuda_source) goto cleanup;
    if (!cxcu_compile_named_module_image_for_device(
            cuda_source, "cxpr_cxcu_optimize.generated.cu", compile_options,
            compile_option_count,
            backend_options ? backend_options->device_ordinal : 0,
            "cxpr_cxcu_optimize_replay", kernel_name, sizeof(kernel_name),
            &image, &cxerr) ||
        !cxcu_module_load_data(&module, image.data, image.size, &cxerr)) {
        cxpr_cxcu_translate_error(
            err, CXPR_ERR_UNAVAILABLE,
            "cxcu failed to compile/load CXPR optimizer kernel", &cxerr);
        goto cleanup;
    }
    if (!cxcu_query_kernel_state_size(&module, kernel_name, &state_size, &cxerr) ||
        state_size > SIZE_MAX / grid.candidate_count) {
        cxpr_cxcu_translate_error(
            err, CXPR_ERR_OUT_OF_MEMORY,
            "cxcu failed to query CXPR candidate state size", &cxerr);
        goto cleanup;
    }
    states = (unsigned char*)calloc(grid.candidate_count, state_size);
    if (!states) goto oom;
    {
        size_t candidate_count = grid.candidate_count;
        size_t param_count = grid.param_count;
        size_t input_count = inputs->input_count;
        size_t tick_count = inputs->tick_count;
        cxcu_arg args[] = {
            {CXCU_ARG_OUT, &state_size, sizeof(state_size)},
            {CXCU_ARG_IN, states, state_size * grid.candidate_count},
            {CXCU_ARG_IN, grid.params,
             grid.candidate_count * grid.param_count * sizeof(*grid.params)},
            {CXCU_ARG_IN, typed_inputs,
             inputs->tick_count * inputs->input_count * sizeof(*typed_inputs)},
            {CXCU_ARG_IN, grid.active, grid.candidate_count * sizeof(*grid.active)},
            {CXCU_ARG_OUT, outputs,
             grid.candidate_count * output_count * sizeof(*outputs)},
            {CXCU_ARG_SCALAR, &candidate_count, sizeof(candidate_count)},
            {CXCU_ARG_SCALAR, &param_count, sizeof(param_count)},
            {CXCU_ARG_SCALAR, &input_count, sizeof(input_count)},
            {CXCU_ARG_SCALAR, &tick_count, sizeof(tick_count)}
        };
        cxcu_launch_config launch = {
            (unsigned int)grid.candidate_count, 1u, 1u, 1u, 1u, 1u, 0u, NULL};
        if (!cxcu_run_kernel(&module, kernel_name, &launch, args,
                             sizeof(args) / sizeof(args[0]), &cxerr)) {
            cxpr_cxcu_translate_error(
                err, CXPR_ERR_UNAVAILABLE,
                "cxcu failed to run CXPR optimizer kernel", &cxerr);
            goto cleanup;
        }
    }
    if (!cxpr_model_optimize_finalize_results(
            program, &grid, outputs, output_count, result, err)) goto cleanup;
    ok = 1;
    goto cleanup;

oom:
    cxpr_cxcu_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                        "Out of memory in CXPR cxcu optimizer backend");
cleanup:
    if (!ok) cxpr_model_optimize_result_free(result);
    cxcu_module_unload(&module);
    cxcu_module_image_free(&image);
    free(cuda_source);
    free(states);
    free(outputs);
    free(typed_inputs);
    cxpr_model_optimize_grid_free(&grid);
    return ok != 0;
}

void cxpr_cxcu_backend_init(
    cxpr_model_optimize_backend* out_backend,
    const cxpr_cxcu_backend_options* options) {
    if (!out_backend) return;
    *out_backend = (cxpr_model_optimize_backend){
        CXPR_OPTIMIZE_BACKEND_API_VERSION,
        (void*)options,
        cxpr_cxcu_available,
        cxpr_cxcu_run,
    };
}
