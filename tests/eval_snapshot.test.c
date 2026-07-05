#include <cxpr/cxpr.h>
#include <cxpr/engine.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static cxpr_ast* parse_required(const char* expression) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;

    assert(parser);
    ast = cxpr_parse(parser, expression, &err);
    assert(ast);
    cxpr_parser_free(parser);
    return ast;
}

static void test_snapshot_marks_short_circuit_branch_skipped(void) {
    cxpr_ast* ast = parse_required("rsi < 30 and ema_fast > ema_slow");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    int saw_skipped_right = 0;
    int saw_false_root = 0;
    FILE* json;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    cxpr_context_set(ctx, "rsi", 42.0);
    cxpr_context_set(ctx, "ema_fast", 101.2);
    cxpr_context_set(ctx, "ema_slow", 99.8);

    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));
    assert(snapshot.node_count >= 7);
    assert(snapshot.state == CXPR_SNAPSHOT_STATE_FALSE);
    assert(snapshot.resolved != NULL);
    assert(strstr(snapshot.resolved, "right not evaluated") != NULL);

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->id == 0 && node->state == CXPR_SNAPSHOT_STATE_FALSE) {
            saw_false_root = 1;
        }
        if (node->role && strcmp(node->role, "right") == 0 &&
            node->state == CXPR_SNAPSHOT_STATE_SKIPPED) {
            saw_skipped_right = 1;
        }
    }
    assert(saw_false_root);
    assert(saw_skipped_right);

    json = tmpfile();
    assert(json);
    assert(cxpr_eval_snapshot_write_json(&snapshot, json));
    fclose(json);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_ast_free(ast);
}

static void test_flow_snapshot_links_named_expressions(void) {
    const cxpr_expression_def defs[] = {
        { "trend", "ema_fast > ema_slow" },
        { "entry", "rsi < 30 and trend" },
    };
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator;
    cxpr_context* ctx = cxpr_context_new();
    cxpr_eval_snapshot_flow flow;
    cxpr_error err = {0};
    int saw_trend_to_entry = 0;
    int saw_ema_fast_source = 0;
    FILE* json;

    assert(reg);
    assert(ctx);
    cxpr_register_defaults(reg);
    evaluator = cxpr_evaluator_new(reg);
    assert(evaluator);
    assert(cxpr_expressions_add(evaluator, defs, 2, &err));
    assert(cxpr_evaluator_compile(evaluator, &err));

    cxpr_context_set(ctx, "rsi", 42.0);
    cxpr_context_set(ctx, "ema_fast", 101.2);
    cxpr_context_set(ctx, "ema_slow", 99.8);
    cxpr_expression_eval_all(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);

    assert(cxpr_eval_snapshot_build_flow(evaluator, ctx, reg, &flow, &err));
    assert(flow.node_count >= 5);
    assert(flow.edge_count >= 1);
    for (size_t i = 0; i < flow.node_count; ++i) {
        if (strcmp(flow.nodes[i].name, "ema_fast") == 0 &&
            strcmp(flow.nodes[i].kind, "source") == 0 &&
            strcmp(flow.nodes[i].value_text, "101.2") == 0) {
            saw_ema_fast_source = 1;
        }
    }
    for (size_t i = 0; i < flow.edge_count; ++i) {
        if (strcmp(flow.edges[i].source_name, "trend") == 0 &&
            strcmp(flow.edges[i].target_name, "entry") == 0) {
            saw_trend_to_entry = 1;
        }
    }
    assert(saw_trend_to_entry);
    assert(saw_ema_fast_source);

    json = tmpfile();
    assert(json);
    assert(cxpr_eval_snapshot_flow_write_json(&flow, json));
    fclose(json);

    cxpr_eval_snapshot_flow_free(&flow);
    cxpr_context_free(ctx);
    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
}

static void test_engine_flow_snapshot_uses_current_tick_state(void) {
    const cxpr_expression_def defs[] = {
        { "trend", "ema_fast > ema_slow" },
        { "entry", "rsi < 30 and trend" },
    };
    cxpr_engine_config cfg = {0};
    cxpr_engine_session* session;
    cxpr_context* ctx;
    cxpr_eval_snapshot_flow flow;
    cxpr_error err = {0};
    int saw_trend_to_entry = 0;

    cfg.expressions = defs;
    cfg.expression_count = 2;
    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);

    ctx = cxpr_engine_session_context(session);
    assert(ctx);
    cxpr_context_set(ctx, "rsi", 42.0);
    cxpr_context_set(ctx, "ema_fast", 101.2);
    cxpr_context_set(ctx, "ema_slow", 99.8);

    assert(cxpr_engine_tick(session, NULL, NULL, &err));
    assert(cxpr_engine_snapshot_flow(session, &flow, &err));
    assert(flow.node_count >= 5);
    assert(flow.edge_count >= 1);
    for (size_t i = 0; i < flow.edge_count; ++i) {
        if (strcmp(flow.edges[i].source_name, "trend") == 0 &&
            strcmp(flow.edges[i].target_name, "entry") == 0) {
            saw_trend_to_entry = 1;
        }
    }
    assert(saw_trend_to_entry);

    cxpr_eval_snapshot_flow_free(&flow);
    cxpr_engine_session_free(session);
}

int main(void) {
    test_snapshot_marks_short_circuit_branch_skipped();
    test_flow_snapshot_links_named_expressions();
    test_engine_flow_snapshot_uses_current_tick_state();
    printf("  \xE2\x9C\x93 eval_snapshot\n");
    return 0;
}
