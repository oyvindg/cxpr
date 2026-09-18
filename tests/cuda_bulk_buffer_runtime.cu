#include <cuda_runtime.h>
#include <cxpr/cxpr.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "cxpr_rolling_buffer_generated.cuh"

static char* read_fixture(void) {
    FILE* file = std::fopen(CXPR_ROLLING_BUFFER_FIXTURE, "rb");
    long size;
    char* text;
    if (!file || std::fseek(file, 0, SEEK_END) != 0 || (size = std::ftell(file)) < 0)
        return nullptr;
    std::rewind(file);
    text = static_cast<char*>(std::malloc(static_cast<size_t>(size) + 1u));
    if (!text || std::fread(text, 1u, static_cast<size_t>(size), file) != static_cast<size_t>(size))
        std::exit(2);
    text[size] = '\0';
    std::fclose(file);
    return text;
}

static void check(cudaError_t status, const char* operation) {
    if (status == cudaSuccess) return;
    std::fprintf(stderr, "%s: %s\n", operation, cudaGetErrorString(status));
    std::exit(2);
}

__global__ static void rolling_buffer_bulk(
    cxpr_rolling_buffer_tick_state* states, const double* inputs,
    double* outputs, size_t count) {
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    cxpr_value element_inputs[1];
    cxpr_value element_outputs[3];
    if (i >= count) return;
    element_inputs[0] = cxpr_num(inputs[i]);
    cxpr_rolling_buffer_tick(&states[i], element_inputs, nullptr, element_outputs);
    for (size_t output = 0u; output < 3u; ++output)
        outputs[output * count + i] = element_outputs[output].d;
}

int main(void) {
    constexpr size_t count = 257u;
    constexpr size_t ticks = 7u;
    std::vector<double> inputs(count), actual(3u * count), expected(3u * count);
    std::vector<cxpr_model_session*> sessions(count);
    cxpr_rolling_buffer_tick_state* device_states = nullptr;
    double* device_inputs = nullptr;
    double* device_outputs = nullptr;
    cxpr_error error = {};
    char* source = read_fixture();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_model* model;
    cxpr_model_compiled* program;

    if (!source || !registry) return 2;
    cxpr_register_defaults(registry);
    model = cxpr_model_parse(source, &error);
    program = model ? cxpr_model_compile(model, registry, &error) : nullptr;
    if (!program) return 2;
    for (size_t i = 0u; i < count; ++i) {
        sessions[i] = cxpr_model_session_new(program, registry, &error);
        if (!sessions[i]) return 2;
    }

    check(cudaMalloc(&device_states, count * sizeof(*device_states)), "allocate states");
    check(cudaMalloc(&device_inputs, count * sizeof(*device_inputs)), "allocate inputs");
    check(cudaMalloc(&device_outputs, actual.size() * sizeof(double)), "allocate outputs");
    check(cudaMemset(device_states, 0, count * sizeof(*device_states)), "reset states");

    for (size_t tick = 0u; tick < ticks; ++tick) {
        for (size_t i = 0u; i < count; ++i) inputs[i] = (double)(1000u * i + tick + 1u);
        check(cudaMemcpy(device_inputs, inputs.data(), count * sizeof(double),
                         cudaMemcpyHostToDevice), "copy inputs");
        rolling_buffer_bulk<<<(unsigned)((count + 255u) / 256u), 256>>>(
            device_states, device_inputs, device_outputs, count);
        check(cudaGetLastError(), "launch rolling-buffer kernel");
        check(cudaDeviceSynchronize(), "synchronize rolling-buffer kernel");
        check(cudaMemcpy(actual.data(), device_outputs, actual.size() * sizeof(double),
                         cudaMemcpyDeviceToHost), "copy outputs");
        for (size_t i = 0u; i < count; ++i) {
            double host_outputs[3];
            cxpr_context_set(cxpr_model_session_context(sessions[i]), "sample", inputs[i]);
            if (!cxpr_model_session_tick(program, sessions[i], registry, &error) ||
                !cxpr_model_session_get_number(sessions[i], "newest", &host_outputs[0]) ||
                !cxpr_model_session_get_number(sessions[i], "oldest", &host_outputs[1]) ||
                !cxpr_model_session_get_number(sessions[i], "window_sum", &host_outputs[2]))
                return 2;
            for (size_t output = 0u; output < 3u; ++output) {
                expected[output * count + i] = host_outputs[output];
                const double a = actual[output * count + i];
                const double e = expected[output * count + i];
                if (!(a == e || (std::isnan(a) && std::isnan(e)))) {
                    std::fprintf(stderr, "CUDA buffer mismatch tick=%zu element=%zu output=%zu\n",
                                 tick, i, output);
                    return 1;
                }
            }
        }
    }
    cudaFree(device_outputs);
    cudaFree(device_inputs);
    cudaFree(device_states);
    for (size_t i = 0u; i < count; ++i) cxpr_model_session_free(sessions[i]);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
    cxpr_registry_free(registry);
    std::free(source);
    std::printf("CUDA rolling-buffer parity OK (%zu independent states)\n", count);
    return 0;
}
