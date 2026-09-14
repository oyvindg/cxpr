#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dynamic_host.gen.c"

enum { AGENT_COUNT = 3 };

typedef struct fictional_host {
    cxpr_bulk_state_store* agents;
    cxpr_value inputs[AGENT_COUNT];
    cxpr_value outputs[3][AGENT_COUNT];
    cxpr_bulk_const_column input_columns[1];
    cxpr_bulk_column output_columns[3];
    cxpr_bulk_view view;
} fictional_host;

static void host_init(fictional_host* host,
                      const cxpr_generated_model_descriptor* descriptor) {
    cxpr_bulk_state_store_status status;
    size_t i;
    memset(host, 0, sizeof(*host));
    host->agents = cxpr_bulk_state_store_new(descriptor, &status);
    assert(host->agents && status == CXPR_BULK_STATE_STORE_OK);
    host->input_columns[0] = (cxpr_bulk_const_column){host->inputs, 1u};
    for (i = 0u; i < 3u; ++i)
        host->output_columns[i] = (cxpr_bulk_column){host->outputs[i], 1u};
    host->view.inputs = host->input_columns;
    host->view.input_count = 1u;
    host->view.outputs = host->output_columns;
    host->view.output_count = 3u;
}

static void host_dispose(fictional_host* host) {
    cxpr_bulk_state_store_free(host->agents);
}

static void host_tick(fictional_host* host,
                      const cxpr_generated_model_descriptor* descriptor,
                      const double* deltas) {
    size_t i;
    const size_t count = cxpr_bulk_state_store_count(host->agents);
    for (i = 0u; i < count; ++i)
        host->inputs[i] = cxpr_num(deltas[i]);
    assert(cxpr_bulk_state_store_bind_view(host->agents, &host->view) ==
           CXPR_BULK_STATE_STORE_OK);
    assert(cxpr_bulk_run(descriptor, &host->view) == CXPR_BULK_OK);
}

static size_t output_index(const cxpr_generated_model_descriptor* descriptor,
                           const char* name) {
    size_t i;
    for (i = 0u; i < descriptor->output_count; ++i)
        if (strcmp(descriptor->output_names[i], name) == 0) return i;
    assert(!"missing generated output");
    return 0u;
}

int main(void) {
    const cxpr_generated_model_descriptor* descriptor =
        &dynamic_host_tick_descriptor;
    const uint64_t layout_id = UINT64_C(0x4fb529eb13b8c764);
    const uint64_t survivor_ids[] = {10u, 30u};
    const uint64_t final_ids[] = {10u, 25u, 30u};
    const double first_tick[] = {1.0, 50.0, 3.0};
    const double survivor_tick[] = {2.0, 4.0};
    const double final_tick[] = {1.0, 7.0, 1.0};
    fictional_host original;
    fictional_host restarted;
    cxpr_bulk_snapshot snapshot = {0};
    cxpr_value expected[3][2];
    size_t total;
    size_t visits;
    size_t visit_echo;
    size_t output;
    size_t agent;

    assert(cxpr_generated_model_descriptor_abi_valid(descriptor));
    assert(descriptor->input_count == 1u && descriptor->output_count == 3u);
    total = output_index(descriptor, "total");
    visits = output_index(descriptor, "visits");
    visit_echo = output_index(descriptor, "visit_echo");
    assert(descriptor->output_types[total] == CXPR_GENERATED_VALUE_NUMBER);
    assert(descriptor->output_types[visits] == CXPR_GENERATED_VALUE_INT64);

    host_init(&original, descriptor);
    assert(cxpr_bulk_state_store_add(original.agents, 30u) == CXPR_BULK_STATE_STORE_OK);
    assert(cxpr_bulk_state_store_add(original.agents, 10u) == CXPR_BULK_STATE_STORE_OK);
    assert(cxpr_bulk_state_store_add(original.agents, 20u) == CXPR_BULK_STATE_STORE_OK);
    host_tick(&original, descriptor, first_tick);
    assert(original.outputs[visits][0].i64 == INT64_C(9007199254740994));
    assert(original.outputs[visits][0].i64 == original.outputs[visit_echo][0].i64);

    assert(cxpr_bulk_state_store_remove(original.agents, 20u) == CXPR_BULK_STATE_STORE_OK);
    assert(memcmp(cxpr_bulk_state_store_ids(original.agents), survivor_ids,
                  sizeof(survivor_ids)) == 0);
    assert(cxpr_bulk_state_store_bind_view(original.agents, &original.view) ==
           CXPR_BULK_STATE_STORE_OK);
    assert(cxpr_bulk_snapshot_create(descriptor, layout_id, &original.view, &snapshot) ==
           CXPR_BULK_SNAPSHOT_OK);

    host_tick(&original, descriptor, survivor_tick);
    for (output = 0u; output < 3u; ++output)
        for (agent = 0u; agent < 2u; ++agent)
            expected[output][agent] = original.outputs[output][agent];

    host_init(&restarted, descriptor);
    assert(cxpr_bulk_state_store_add(restarted.agents, 30u) == CXPR_BULK_STATE_STORE_OK);
    assert(cxpr_bulk_state_store_add(restarted.agents, 10u) == CXPR_BULK_STATE_STORE_OK);
    assert(cxpr_bulk_state_store_bind_view(restarted.agents, &restarted.view) ==
           CXPR_BULK_STATE_STORE_OK);
    assert(cxpr_bulk_snapshot_restore(descriptor, layout_id, &snapshot, &restarted.view) ==
           CXPR_BULK_SNAPSHOT_OK);
    host_tick(&restarted, descriptor, survivor_tick);
    for (output = 0u; output < 3u; ++output)
        for (agent = 0u; agent < 2u; ++agent)
            assert(memcmp(&expected[output][agent], &restarted.outputs[output][agent],
                          sizeof(cxpr_value)) == 0);

    assert(cxpr_bulk_state_store_add(restarted.agents, 25u) == CXPR_BULK_STATE_STORE_OK);
    assert(memcmp(cxpr_bulk_state_store_ids(restarted.agents), final_ids,
                  sizeof(final_ids)) == 0);
    host_tick(&restarted, descriptor, final_tick);
    assert(restarted.outputs[visits][0].i64 == INT64_C(9007199254740996));
    assert(restarted.outputs[visits][1].i64 == INT64_C(9007199254740994));
    assert(restarted.outputs[visits][2].i64 == INT64_C(9007199254740996));

    cxpr_bulk_snapshot_free(&snapshot);
    host_dispose(&restarted);
    host_dispose(&original);
    puts("fictional host dynamic-state integration passed");
    return 0;
}
