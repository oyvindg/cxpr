#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct agent_state {
    uint64_t ticks;
    double total;
} agent_state;

static size_t agent_state_size(void) { return sizeof(agent_state); }

static void agent_tick(void* opaque,
                       const cxpr_value* inputs,
                       const cxpr_value* params,
                       cxpr_value* outputs) {
    agent_state* state = (agent_state*)opaque;
    (void)params;
    state->ticks++;
    state->total += inputs[0].d;
    outputs[0] = (cxpr_value){.type = CXPR_VALUE_NUMBER, .d = state->total};
}

static const cxpr_generated_model_descriptor descriptor = {
    .name = "dynamic_agents",
    .tick = agent_tick,
    .state_size = agent_state_size,
    .input_names = {"delta"},
    .input_types = {CXPR_GENERATED_VALUE_NUMBER},
    .input_count = 1u,
    .output_names = {"total"},
    .output_types = {CXPR_GENERATED_VALUE_NUMBER},
    .output_count = 1u,
    .abi_version = CXPR_GENERATED_MODEL_ABI_VERSION,
};

static void run(cxpr_bulk_state_store* store, const double* deltas) {
    cxpr_value input_values[4] = {{0}};
    cxpr_value output_values[4] = {{0}};
    cxpr_bulk_const_column inputs[] = {{input_values, 1u}};
    cxpr_bulk_column outputs[] = {{output_values, 1u}};
    cxpr_bulk_view view = {
        .inputs = inputs,
        .input_count = 1u,
        .outputs = outputs,
        .output_count = 1u,
    };
    size_t i;
    const size_t count = cxpr_bulk_state_store_count(store);
    assert(count <= 4u);
    for (i = 0u; i < count; ++i)
        input_values[i] = (cxpr_value){.type = CXPR_VALUE_NUMBER, .d = deltas[i]};
    assert(cxpr_bulk_state_store_bind_view(store, &view) ==
           CXPR_BULK_STATE_STORE_OK);
    assert(cxpr_bulk_run(&descriptor, &view) == CXPR_BULK_OK);
}

static void assert_ids(const cxpr_bulk_state_store* store,
                       const uint64_t* expected,
                       size_t count) {
    assert(cxpr_bulk_state_store_count(store) == count);
    assert(memcmp(cxpr_bulk_state_store_ids(store), expected,
                  count * sizeof(*expected)) == 0);
}

int main(void) {
    cxpr_bulk_state_store_status status;
    cxpr_bulk_state_store* dynamic =
        cxpr_bulk_state_store_new(&descriptor, &status);
    cxpr_bulk_state_store* baseline =
        cxpr_bulk_state_store_new(&descriptor, &status);
    cxpr_bulk_state_store* different_history =
        cxpr_bulk_state_store_new(&descriptor, &status);
    const uint64_t sorted_initial[] = {10u, 20u, 30u};
    const uint64_t survivors[] = {10u, 30u};
    const uint64_t final_ids[] = {10u, 25u, 30u};
    const double dynamic_tick[] = {1.0, 99.0, 3.0};
    const double survivor_tick[] = {1.0, 3.0};
    const agent_state* state10;
    const agent_state* state30;

    assert(dynamic && baseline && different_history);
    assert(status == CXPR_BULK_STATE_STORE_OK);

    /* Insertion history never affects iteration order. */
    assert(cxpr_bulk_state_store_add(dynamic, 30u) == CXPR_BULK_STATE_STORE_OK);
    assert(cxpr_bulk_state_store_add(dynamic, 10u) == CXPR_BULK_STATE_STORE_OK);
    assert(cxpr_bulk_state_store_add(dynamic, 20u) == CXPR_BULK_STATE_STORE_OK);
    assert_ids(dynamic, sorted_initial, 3u);
    assert(cxpr_bulk_state_store_add(dynamic, 20u) ==
           CXPR_BULK_STATE_STORE_DUPLICATE_ID);

    assert(cxpr_bulk_state_store_add(baseline, 10u) == CXPR_BULK_STATE_STORE_OK);
    assert(cxpr_bulk_state_store_add(baseline, 30u) == CXPR_BULK_STATE_STORE_OK);
    run(dynamic, dynamic_tick);
    run(baseline, survivor_tick);

    assert(cxpr_bulk_state_store_remove(dynamic, 20u) == CXPR_BULK_STATE_STORE_OK);
    assert_ids(dynamic, survivors, 2u);
    assert(cxpr_bulk_state_store_remove(dynamic, 20u) ==
           CXPR_BULK_STATE_STORE_ID_NOT_FOUND);
    state10 = (const agent_state*)cxpr_bulk_state_store_find_const(dynamic, 10u);
    state30 = (const agent_state*)cxpr_bulk_state_store_find_const(dynamic, 30u);
    assert(memcmp(state10, cxpr_bulk_state_store_find_const(baseline, 10u),
                  sizeof(*state10)) == 0);
    assert(memcmp(state30, cxpr_bulk_state_store_find_const(baseline, 30u),
                  sizeof(*state30)) == 0);

    assert(cxpr_bulk_state_store_add(dynamic, 25u) == CXPR_BULK_STATE_STORE_OK);
    assert_ids(dynamic, final_ids, 3u);
    assert(((const agent_state*)cxpr_bulk_state_store_find_const(dynamic, 25u))->ticks == 0u);

    assert(cxpr_bulk_state_store_add(different_history, 25u) == CXPR_BULK_STATE_STORE_OK);
    assert(cxpr_bulk_state_store_add(different_history, 30u) == CXPR_BULK_STATE_STORE_OK);
    assert(cxpr_bulk_state_store_add(different_history, 10u) == CXPR_BULK_STATE_STORE_OK);
    assert_ids(different_history, final_ids, 3u);

    cxpr_bulk_state_store_free(different_history);
    cxpr_bulk_state_store_free(baseline);
    cxpr_bulk_state_store_free(dynamic);
    puts("dynamic bulk state identity/order tests passed");
    return 0;
}
