#include <cuda_runtime.h>
#include <cxpr/cuda_candidate_runtime.cuh>
#include <cxpr/cxpr.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "cxpr_optimize_candidate_generated.cuh"

static void check(cudaError_t status, const char* operation) {
    if (status == cudaSuccess) return;
    std::fprintf(stderr, "%s: %s\n", operation, cudaGetErrorString(status));
    std::exit(2);
}

/* Migration oracle: preserve the original hand-written replay path. */
__global__ static void replay_candidates_legacy(
    cxpr_optimize_candidate_tick_state* states, const cxpr_value* params,
    const cxpr_value* inputs, const unsigned char* active,
    size_t candidate_count, size_t tick_count, cxpr_value* outputs) {
    const size_t candidate = blockIdx.x * blockDim.x + threadIdx.x;
    if (candidate >= candidate_count || active[candidate] == 0u) return;
    for (size_t tick = 0u; tick < tick_count; ++tick)
        cxpr_optimize_candidate_tick(
            &states[candidate], &inputs[tick], &params[candidate * 2u],
            &outputs[candidate * 2u]);
}

CXPR_CUDA_DEFINE_CANDIDATE_REPLAY_KERNEL(
    replay_candidates_generated, cxpr_optimize_candidate_tick_state,
    cxpr_optimize_candidate_tick)

static char* read_fixture(void) {
    FILE* file = std::fopen(CXPR_CUDA_OPTIMIZE_FIXTURE, "rb");
    long size;
    char* text;
    if (!file || std::fseek(file, 0, SEEK_END) != 0 || (size = std::ftell(file)) < 0)
        return nullptr;
    std::rewind(file);
    text = static_cast<char*>(std::malloc(static_cast<size_t>(size) + 1u));
    if (!text || std::fread(text, 1u, static_cast<size_t>(size), file) !=
                     static_cast<size_t>(size))
        std::exit(2);
    text[size] = '\0';
    std::fclose(file);
    return text;
}

static void require_equal(double actual, double expected, const char* label,
                          size_t candidate, size_t output) {
    if (std::isnan(actual) && std::isnan(expected)) return;
    if (std::abs(actual - expected) <= 1e-12) return;
    std::fprintf(stderr,
                 "%s mismatch candidate=%zu output=%zu got=%.17g expected=%.17g\n",
                 label, candidate, output, actual, expected);
    std::exit(1);
}

int main(void) {
    constexpr size_t candidate_count = 6u;
    constexpr size_t param_count = 2u;
    constexpr size_t output_count = 2u;
    constexpr size_t tick_count = 5u;
    const double gains[3] = {1.0, 2.0, 4.0};
    const double biases[2] = {0.0, 2.0};
    const double samples[tick_count] = {1.0, 2.0, -0.5, 3.0, 4.5};
    const char* input_names[] = {"sample"};
    std::vector<cxpr_value> params(candidate_count * param_count);
    std::vector<cxpr_value> inputs(tick_count);
    std::vector<unsigned char> active(candidate_count);
    std::vector<cxpr_value> legacy(candidate_count * output_count, cxpr_num(NAN));
    std::vector<cxpr_value> generated(candidate_count * output_count, cxpr_num(NAN));
    std::vector<cxpr_value> chunked(candidate_count * output_count, cxpr_num(NAN));
    cxpr_optimize_candidate_tick_state *legacy_states = nullptr,
                                       *generated_states = nullptr,
                                       *chunked_states = nullptr;
    cxpr_value *device_params = nullptr, *device_inputs = nullptr,
               *legacy_outputs = nullptr, *generated_outputs = nullptr,
               *chunked_outputs = nullptr;
    unsigned char* device_active = nullptr;

    for (size_t gain = 0u, candidate = 0u; gain < 3u; ++gain) {
        for (size_t bias = 0u; bias < 2u; ++bias, ++candidate) {
            params[candidate * param_count] = cxpr_num(gains[gain]);
            params[candidate * param_count + 1u] = cxpr_num(biases[bias]);
            active[candidate] = gains[gain] + biases[bias] <= 4.0 ? 1u : 0u;
        }
    }
    for (size_t tick = 0u; tick < tick_count; ++tick) inputs[tick] = cxpr_num(samples[tick]);

#define ALLOC(ptr, bytes, label) check(cudaMalloc(&(ptr), (bytes)), (label))
    ALLOC(legacy_states, candidate_count * sizeof(*legacy_states), "allocate legacy states");
    ALLOC(generated_states, candidate_count * sizeof(*generated_states), "allocate generated states");
    ALLOC(chunked_states, candidate_count * sizeof(*chunked_states), "allocate chunked states");
    ALLOC(device_params, params.size() * sizeof(*device_params), "allocate params");
    ALLOC(device_inputs, inputs.size() * sizeof(*device_inputs), "allocate inputs");
    ALLOC(device_active, active.size(), "allocate active mask");
    ALLOC(legacy_outputs, legacy.size() * sizeof(*legacy_outputs), "allocate legacy outputs");
    ALLOC(generated_outputs, generated.size() * sizeof(*generated_outputs), "allocate generated outputs");
    ALLOC(chunked_outputs, chunked.size() * sizeof(*chunked_outputs), "allocate chunked outputs");
#undef ALLOC
    check(cudaMemset(legacy_states, 0, candidate_count * sizeof(*legacy_states)), "reset legacy states");
    check(cudaMemset(generated_states, 0, candidate_count * sizeof(*generated_states)), "reset generated states");
    check(cudaMemset(chunked_states, 0, candidate_count * sizeof(*chunked_states)), "reset chunked states");
    check(cudaMemcpy(device_params, params.data(), params.size() * sizeof(*device_params),
                     cudaMemcpyHostToDevice), "copy params");
    check(cudaMemcpy(device_inputs, inputs.data(), inputs.size() * sizeof(*device_inputs),
                     cudaMemcpyHostToDevice), "copy inputs");
    check(cudaMemcpy(device_active, active.data(), active.size(), cudaMemcpyHostToDevice),
          "copy active mask");
    check(cudaMemcpy(legacy_outputs, legacy.data(), legacy.size() * sizeof(*legacy_outputs),
                     cudaMemcpyHostToDevice), "initialize legacy outputs");
    check(cudaMemcpy(generated_outputs, generated.data(), generated.size() * sizeof(*generated_outputs),
                     cudaMemcpyHostToDevice), "initialize generated outputs");
    check(cudaMemcpy(chunked_outputs, chunked.data(), chunked.size() * sizeof(*chunked_outputs),
                     cudaMemcpyHostToDevice), "initialize chunked outputs");

    replay_candidates_legacy<<<1u, 32u>>>(
        legacy_states, device_params, device_inputs, device_active,
        candidate_count, tick_count, legacy_outputs);
    replay_candidates_generated<<<1u, 32u>>>(
        generated_states, device_params, param_count, device_inputs, 1u, tick_count,
        device_active, generated_outputs, output_count, 0u, candidate_count);
    replay_candidates_generated<<<1u, 32u>>>(
        chunked_states, device_params, param_count, device_inputs, 1u, tick_count,
        device_active, chunked_outputs, output_count, 0u, 2u);
    replay_candidates_generated<<<1u, 32u>>>(
        chunked_states, device_params, param_count, device_inputs, 1u, tick_count,
        device_active, chunked_outputs, output_count, 2u, candidate_count - 2u);
    check(cudaGetLastError(), "launch candidate parity kernels");
    check(cudaDeviceSynchronize(), "synchronize candidate parity kernels");
    check(cudaMemcpy(legacy.data(), legacy_outputs, legacy.size() * sizeof(*legacy_outputs),
                     cudaMemcpyDeviceToHost), "copy legacy outputs");
    check(cudaMemcpy(generated.data(), generated_outputs, generated.size() * sizeof(*generated_outputs),
                     cudaMemcpyDeviceToHost), "copy generated outputs");
    check(cudaMemcpy(chunked.data(), chunked_outputs, chunked.size() * sizeof(*chunked_outputs),
                     cudaMemcpyDeviceToHost), "copy chunked outputs");

    for (size_t candidate = 0u; candidate < candidate_count; ++candidate) {
        for (size_t output = 0u; output < output_count; ++output) {
            const size_t index = candidate * output_count + output;
            require_equal(generated[index].d, legacy[index].d,
                          "generated/legacy", candidate, output);
            require_equal(chunked[index].d, generated[index].d,
                          "chunked/generated", candidate, output);
        }
    }

    char* source = read_fixture();
    cxpr_error error = {};
    cxpr_model* model = source ? cxpr_model_parse(source, &error) : nullptr;
    cxpr_model_compiled* program = model ? cxpr_model_compile(model, nullptr, &error) : nullptr;
    cxpr_model_optimize_inputs optimize_inputs = {input_names, samples, 1u, tick_count};
    cxpr_model_optimize_result cpu = {};
    if (!program || !cxpr_model_optimize(program, &optimize_inputs, nullptr, &cpu, &error)) {
        std::fprintf(stderr, "CPU optimizer failed: %s\n",
                     error.message ? error.message : "unknown error");
        return 2;
    }
    if (cpu.candidate_count != 5u || cpu.param_count != param_count ||
        cpu.objective_count != output_count) return 1;
    for (size_t row = 0u; row < cpu.candidate_count; ++row) {
        size_t candidate = 0u;
        while (candidate < candidate_count &&
               (params[candidate * param_count].d != cpu.candidates[row].params[0] ||
                params[candidate * param_count + 1u].d != cpu.candidates[row].params[1]))
            ++candidate;
        if (candidate == candidate_count || !active[candidate]) return 1;
        for (size_t objective = 0u; objective < cpu.objective_count; ++objective) {
            cxpr_model_optimize_objective metadata =
                cxpr_model_optimize_objective_at(program, objective);
            const size_t output = std::strcmp(metadata.name, "total") == 0 ? 0u : 1u;
            require_equal(generated[candidate * output_count + output].d,
                          cpu.candidates[row].objectives[objective],
                          "GPU/CPU optimizer", candidate, output);
        }
    }

    cxpr_model_optimize_result_free(&cpu);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
    std::free(source);
    cudaFree(chunked_outputs); cudaFree(generated_outputs); cudaFree(legacy_outputs);
    cudaFree(device_active); cudaFree(device_inputs); cudaFree(device_params);
    cudaFree(chunked_states); cudaFree(generated_states); cudaFree(legacy_states);
    std::puts("CUDA generated/legacy/CPU optimizer parity and chunking OK");
    return 0;
}
