#include <cuda_runtime.h>
#include <cxpr/cuda_candidate_runtime.cuh>
#include <cxpr/cxpr.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "cxpr_optimize_candidate_generated.cuh"
#include "cuda_optimize_legacy_oracle.cuh"
#include "cuda_test_helpers.cuh"

using cxpr_cuda_test::device_buffer;

CXPR_CUDA_DEFINE_CANDIDATE_REPLAY_KERNEL(
    replay_candidates, cxpr_optimize_candidate_tick_state,
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
    device_buffer<cxpr_optimize_candidate_tick_state>
        legacy_states(candidate_count, "allocate legacy states"),
        generated_states(candidate_count, "allocate generated states"),
        chunked_states(candidate_count, "allocate chunked states");
    device_buffer<cxpr_value> device_params(params.size(), "allocate params");
    device_buffer<cxpr_value> device_inputs(inputs.size(), "allocate inputs");
    device_buffer<unsigned char> device_active(active.size(), "allocate active mask");
    device_buffer<cxpr_value> legacy_outputs(legacy.size(), "allocate legacy outputs");
    device_buffer<cxpr_value> generated_outputs(generated.size(), "allocate generated outputs");
    device_buffer<cxpr_value> chunked_outputs(chunked.size(), "allocate chunked outputs");

    for (size_t gain = 0u, candidate = 0u; gain < 3u; ++gain) {
        for (size_t bias = 0u; bias < 2u; ++bias, ++candidate) {
            params[candidate * param_count] = cxpr_num(gains[gain]);
            params[candidate * param_count + 1u] = cxpr_num(biases[bias]);
            active[candidate] = gains[gain] + biases[bias] <= 4.0 ? 1u : 0u;
        }
    }
    for (size_t tick = 0u; tick < tick_count; ++tick) inputs[tick] = cxpr_num(samples[tick]);

    legacy_states.clear("reset legacy states");
    generated_states.clear("reset generated states");
    chunked_states.clear("reset chunked states");
    device_params.upload(params.data(), params.size(), "copy params");
    device_inputs.upload(inputs.data(), inputs.size(), "copy inputs");
    device_active.upload(active.data(), active.size(), "copy active mask");
    legacy_outputs.upload(legacy.data(), legacy.size(), "initialize legacy outputs");
    generated_outputs.upload(generated.data(), generated.size(), "initialize generated outputs");
    chunked_outputs.upload(chunked.data(), chunked.size(), "initialize chunked outputs");

    cxpr_cuda_replay_candidates_legacy<<<1u, 32u>>>(
        legacy_states.data(), device_params.data(), device_inputs.data(),
        device_active.data(), candidate_count, tick_count, legacy_outputs.data());
    replay_candidates<<<1u, 32u>>>(
        generated_states.data(), device_params.data(), param_count,
        device_inputs.data(), 1u, tick_count, device_active.data(),
        generated_outputs.data(), output_count, 0u, candidate_count);
    replay_candidates<<<1u, 32u>>>(
        chunked_states.data(), device_params.data(), param_count,
        device_inputs.data(), 1u, tick_count, device_active.data(),
        chunked_outputs.data(), output_count, 0u, 2u);
    replay_candidates<<<1u, 32u>>>(
        chunked_states.data(), device_params.data(), param_count,
        device_inputs.data(), 1u, tick_count, device_active.data(),
        chunked_outputs.data(), output_count, 2u, candidate_count - 2u);
    cxpr_cuda_test::synchronize("run candidate parity kernels");
    legacy_outputs.download(legacy.data(), legacy.size(), "copy legacy outputs");
    generated_outputs.download(generated.data(), generated.size(), "copy generated outputs");
    chunked_outputs.download(chunked.data(), chunked.size(), "copy chunked outputs");

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
    std::puts("CUDA generated/legacy/CPU optimizer parity and chunking OK");
    return 0;
}
