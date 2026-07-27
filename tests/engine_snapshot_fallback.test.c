#include <cxpr/cxpr.h>
#include <cxpr/engine.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const cxpr_expression_def exprs[] = {
        {"entry", "close > $threshold and enabled"},
    };
    const double close[] = {12.0};
    const cxpr_engine_column_source_def cols[] = {
        {"close", close, sizeof(double), 1u},
    };
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* session;
    cxpr_context* parent = cxpr_context_new();
    cxpr_eval_snapshot_flow flow = {0};
    const cxpr_eval_snapshot* entry_snapshot = NULL;
    size_t i;

    assert(parent);
    cxpr_context_set_param(parent, "threshold", 10.0);
    cxpr_context_set_bool(parent, "enabled", true);
    cfg.expressions = exprs;
    cfg.expression_count = 1u;
    cfg.column_sources = cols;
    cfg.column_source_count = 1u;

    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);
    assert(cxpr_engine_tick_fallback(session, parent, NULL, NULL, &err));
    assert(cxpr_engine_snapshot_flow_fallback(session, parent, &flow, &err));
    for (i = 0u; i < flow.node_count; ++i) {
        if (flow.nodes[i].name && strcmp(flow.nodes[i].name, "entry") == 0) {
            entry_snapshot = &flow.nodes[i].ast;
            break;
        }
    }
    assert(entry_snapshot);
    assert(entry_snapshot->node_count >= 5u);
    assert(entry_snapshot->state == CXPR_SNAPSHOT_STATE_TRUE);

    cxpr_eval_snapshot_flow_free(&flow);
    cxpr_engine_session_free(session);
    cxpr_context_free(parent);
    printf("  \xE2\x9C\x93 engine_snapshot_fallback\n");
    return 0;
}
