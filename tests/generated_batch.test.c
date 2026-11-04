#include <cxpr/generated_batch.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct state { double total; } state;

static size_t state_size(void) { return sizeof(state); }
static void reset(void* raw) { memset(raw, 0, sizeof(state)); }
static void tick(void* raw, const cxpr_value* inputs,
                 const cxpr_value* params, cxpr_value* outputs) {
    state* session = (state*)raw;
    session->total += inputs[0].d * params[0].d;
    outputs[0] = cxpr_num(session->total);
    outputs[1] = cxpr_bool(inputs[1].b);
    outputs[2] = cxpr_int64(inputs[2].i64 + 1);
}

int main(void) {
    static const cxpr_generated_model_descriptor descriptor = {
        .name = "ordered", .tick = tick, .state_size = state_size, .reset = reset,
        .param_count = 1u, .param_names = {"gain"},
        .param_types = {CXPR_GENERATED_VALUE_NUMBER},
        .input_count = 3u, .input_names = {"value", "enabled", "timestamp"},
        .input_types = {CXPR_GENERATED_VALUE_NUMBER, CXPR_GENERATED_VALUE_BOOL,
                        CXPR_GENERATED_VALUE_INT64},
        .output_count = 3u, .output_names = {"total", "signal", "next"},
        .output_types = {CXPR_GENERATED_VALUE_NUMBER, CXPR_GENERATED_VALUE_BOOL,
                         CXPR_GENERATED_VALUE_INT64},
        .abi_version = CXPR_GENERATED_MODEL_ABI_VERSION
    };
    double values[] = {1.0, 2.0, 3.0}, totals[3] = {0};
    bool enabled[] = {true, false, true}, signals[3] = {false};
    int64_t timestamps[] = {10, 20, 30}, next[3] = {0};
    cxpr_value params[] = {cxpr_num(2.0)};
    state session = {0};
    cxpr_generated_view_binding inputs[] = {
        {"value", {values, CXPR_VALUE_NUMBER, 1u, {3u}, {sizeof(double)}, sizeof(values), CXPR_VIEW_READ_ONLY}},
        {"enabled", {enabled, CXPR_VALUE_BOOL, 1u, {3u}, {sizeof(bool)}, sizeof(enabled), CXPR_VIEW_READ_ONLY}},
        {"timestamp", {timestamps, CXPR_VALUE_INT64, 1u, {3u}, {sizeof(int64_t)}, sizeof(timestamps), CXPR_VIEW_READ_ONLY}}
    };
    cxpr_generated_view_binding outputs[] = {
        {"total", {totals, CXPR_VALUE_NUMBER, 1u, {3u}, {sizeof(double)}, sizeof(totals), CXPR_VIEW_WRITABLE}},
        {"signal", {signals, CXPR_VALUE_BOOL, 1u, {3u}, {sizeof(bool)}, sizeof(signals), CXPR_VIEW_WRITABLE}},
        {"next", {next, CXPR_VALUE_INT64, 1u, {3u}, {sizeof(int64_t)}, sizeof(next), CXPR_VIEW_WRITABLE}}
    };
    assert(cxpr_generated_tick_batch(&descriptor, &session, params, inputs, 3u,
                                     outputs, 3u, 3u) == CXPR_GENERATED_BATCH_OK);
    assert(totals[0] == 2.0 && totals[1] == 6.0 && totals[2] == 12.0);
    assert(signals[0] && !signals[1] && signals[2]);
    assert(next[0] == 11 && next[1] == 21 && next[2] == 31);
    inputs[0].name = "wrong";
    assert(cxpr_generated_tick_batch(&descriptor, &session, params, inputs, 3u,
                                     outputs, 3u, 3u) == CXPR_GENERATED_BATCH_INVALID_VIEW);
    puts("generated ordered batch tests passed");
    return 0;
}
