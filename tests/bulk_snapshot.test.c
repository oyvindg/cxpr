#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct counter_state {
    uint64_t ticks;
    double total;
} counter_state;

static size_t counter_state_size(void) { return sizeof(counter_state); }

static void counter_tick(void* opaque,
                         const cxpr_value* inputs,
                         const cxpr_value* params,
                         cxpr_value* outputs) {
    counter_state* state = (counter_state*)opaque;
    (void)params;
    state->ticks++;
    state->total += inputs[0].d;
    outputs[0] = (cxpr_value){.type = CXPR_VALUE_NUMBER, .d = state->total};
}

static const cxpr_generated_model_descriptor descriptor = {
    .name = "composed_counter",
    .tick = counter_tick,
    .state_size = counter_state_size,
    .input_names = {"amount"},
    .input_types = {CXPR_GENERATED_VALUE_NUMBER},
    .input_count = 1u,
    .output_names = {"total"},
    .output_types = {CXPR_GENERATED_VALUE_NUMBER},
    .output_count = 1u,
    .abi_version = CXPR_GENERATED_MODEL_ABI_VERSION,
};

static void run_tick(const cxpr_generated_model_descriptor* model,
                     counter_state* states,
                     size_t count,
                     const double* amounts,
                     double* totals) {
    size_t i;
    for (i = 0u; i < count; ++i) {
        const cxpr_value input = {.type = CXPR_VALUE_NUMBER, .d = amounts[i]};
        cxpr_value output = {0};
        model->tick(&states[i], &input, NULL, &output);
        assert(output.type == CXPR_VALUE_NUMBER);
        totals[i] = output.d;
    }
}

int main(void) {
    const uint64_t layout_id = UINT64_C(0x7e9bcbda947e2d31);
    const double first[] = {1.25, -2.0, 8.5};
    const double continuation[] = {4.0, 0.5, -3.0};
    counter_state states[3] = {{0}};
    counter_state restored[3] = {{0}};
    double ignored[3];
    double expected[3];
    double actual[3];
    cxpr_bulk_view source_view = {0};
    cxpr_bulk_view restored_view = {0};
    cxpr_bulk_snapshot snapshot = {0};
    cxpr_bulk_snapshot corrupted;
    cxpr_generated_model_descriptor incompatible = descriptor;

    source_view.states = states;
    source_view.state_stride = sizeof(states[0]);
    source_view.element_count = 3u;
    restored_view.states = restored;
    restored_view.state_stride = sizeof(restored[0]);
    restored_view.element_count = 3u;

    run_tick(&descriptor, states, 3u, first, ignored);
    assert(cxpr_bulk_snapshot_create(&descriptor, layout_id, &source_view, &snapshot) ==
           CXPR_BULK_SNAPSHOT_OK);
    assert(snapshot.data != NULL && snapshot.size > sizeof(states));

    run_tick(&descriptor, states, 3u, continuation, expected);
    assert(cxpr_bulk_snapshot_restore(
               &descriptor, layout_id, &snapshot, &restored_view) ==
           CXPR_BULK_SNAPSHOT_OK);
    run_tick(&descriptor, restored, 3u, continuation, actual);
    assert(memcmp(expected, actual, sizeof(expected)) == 0);
    assert(memcmp(states, restored, sizeof(states)) == 0);

    incompatible.name = "changed_layout";
    assert(cxpr_bulk_snapshot_restore(
               &incompatible, layout_id, &snapshot, &restored_view) ==
           CXPR_BULK_SNAPSHOT_LAYOUT_MISMATCH);
    assert(cxpr_bulk_snapshot_restore(
               &descriptor, layout_id + 1u, &snapshot, &restored_view) ==
           CXPR_BULK_SNAPSHOT_LAYOUT_MISMATCH);
    restored_view.element_count = 2u;
    assert(cxpr_bulk_snapshot_restore(
               &descriptor, layout_id, &snapshot, &restored_view) ==
           CXPR_BULK_SNAPSHOT_ELEMENT_COUNT_MISMATCH);
    restored_view.element_count = 3u;

    corrupted.size = snapshot.size;
    corrupted.data = (unsigned char*)malloc(snapshot.size);
    assert(corrupted.data != NULL);
    memcpy(corrupted.data, snapshot.data, snapshot.size);
    corrupted.data[corrupted.size - 1u] ^= 1u;
    assert(cxpr_bulk_snapshot_restore(
               &descriptor, layout_id, &corrupted, &restored_view) ==
           CXPR_BULK_SNAPSHOT_CHECKSUM_MISMATCH);

    cxpr_bulk_snapshot_free(&corrupted);
    cxpr_bulk_snapshot_free(&snapshot);
    puts("bulk snapshot/restore continuation parity passed");
    return 0;
}
