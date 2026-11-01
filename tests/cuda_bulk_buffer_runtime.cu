#include <cuda_runtime.h>
#include <cxpr/cxpr.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "cuda_rolling_buffer_fixture.cuh"
#include "cuda_test_helpers.cuh"

using cxpr_cuda_test::device_buffer;

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

int main(void) {
    constexpr size_t count = 257u;
    constexpr size_t ticks = 7u;
    std::vector<double> inputs(count), actual(3u * count), expected(3u * count);
    std::vector<cxpr_model_session*> sessions(count);
    device_buffer<cxpr_rolling_buffer_tick_state> device_states(count, "allocate states");
    device_buffer<double> device_inputs(count, "allocate inputs");
    device_buffer<double> device_outputs(actual.size(), "allocate outputs");
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

    device_states.clear("reset states");

    for (size_t tick = 0u; tick < ticks; ++tick) {
        for (size_t i = 0u; i < count; ++i) inputs[i] = (double)(1000u * i + tick + 1u);
        device_inputs.upload(inputs.data(), count, "copy inputs");
        cxpr_cuda_rolling_buffer_step<<<cxpr_cuda_test::blocks(count), 256>>>(
            device_states.data(), device_inputs.data(), device_outputs.data(), count);
        cxpr_cuda_test::synchronize("run rolling-buffer kernel");
        device_outputs.download(actual.data(), actual.size(), "copy outputs");
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
    for (size_t i = 0u; i < count; ++i) cxpr_model_session_free(sessions[i]);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
    cxpr_registry_free(registry);
    std::free(source);
    std::printf("CUDA rolling-buffer parity OK (%zu independent states)\n", count);
    return 0;
}
