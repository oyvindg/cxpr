#include <cxpr/cxpr.h>
#include <cxpr/engine.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static cxpr_expr_ast* parse_required(const char* expression) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;

    assert(parser);
    ast = cxpr_expr_ast_parse(parser, expression, &err);
    assert(ast);
    cxpr_expr_parser_free(parser);
    return ast;
}

static double test_rsi(const double* args, size_t argc, void* userdata) {
    (void)args;
    (void)argc;
    (void)userdata;
    return 42.0;
}

static bool test_write_flow_node_host_json(FILE* out,
                                           const cxpr_eval_snapshot_flow* flow,
                                           size_t node_index,
                                           void* userdata) {
    (void)flow;
    (void)node_index;
    (void)userdata;
    return fputs("{ \"role\": \"test-flow-node\" }", out) != EOF;
}

static bool test_write_ast_node_host_json(FILE* out,
                                          const cxpr_eval_snapshot* snapshot,
                                          size_t node_index,
                                          void* userdata) {
    (void)snapshot;
    (void)node_index;
    (void)userdata;
    return fputs("{ \"role\": \"test-ast-node\" }", out) != EOF;
}

static void test_snapshot_marks_short_circuit_branch_skipped(void) {
    cxpr_expr_ast* ast = parse_required("rsi < 30 and ema_fast > ema_slow");
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
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_writes_host_metadata(void) {
    cxpr_expr_ast* ast = parse_required("ema_fast > ema_slow");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    cxpr_snapshot_json_hooks hooks = {
        "test-host",
        "test-host.v1",
        NULL,
        test_write_ast_node_host_json,
        NULL,
    };
    FILE* json;
    char buf[4096];
    size_t nread;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    cxpr_context_set(ctx, "ema_fast", 101.2);
    cxpr_context_set(ctx, "ema_slow", 99.8);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));

    json = tmpfile();
    assert(json);
    assert(cxpr_eval_snapshot_write_json_ex(&snapshot, &hooks, json));
    rewind(json);
    nread = fread(buf, 1, sizeof(buf) - 1u, json);
    buf[nread] = '\0';
    assert(strstr(buf, "\"schema\": \"cxpr.eval_snapshot.v2\"") != NULL);
    assert(strstr(buf, "\"name\": \"test-host\"") != NULL);
    assert(strstr(buf, "\"role\": \"test-ast-node\"") != NULL);
    assert(strstr(buf, "\"has_value\": true") != NULL);
    assert(strstr(buf, "\"value_type\": \"bool\"") != NULL);
    assert(strstr(buf, "\"typed_value\": { \"type\": \"bool\", \"bool\": true }") != NULL);
    fclose(json);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_does_not_prefix_unary_operand_label(void) {
    cxpr_expr_ast* ast = parse_required("not scram_required");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    int saw_spaced_unary_resolved = 0;
    int saw_plain_operand = 0;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    cxpr_context_set_bool(ctx, "scram_required", true);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->role && strcmp(node->role, "root") == 0) {
            assert(node->resolved);
            assert(strcmp(node->resolved, "not true") == 0);
            saw_spaced_unary_resolved = 1;
        }
        if (node->role && strcmp(node->role, "operand") == 0) {
            assert(node->display_label);
            assert(strstr(node->display_label, "operand:") == NULL);
            assert(strstr(node->display_label, "scram_required") != NULL);
            saw_plain_operand = 1;
        }
    }
    assert(saw_spaced_unary_resolved);
    assert(saw_plain_operand);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_labels_lookback_index_role(void) {
    cxpr_expr_ast* ast = parse_required("close[3]");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    int saw_source = 0;
    int saw_index = 0;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    cxpr_context_set(ctx, "close", 42.0);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->role && strcmp(node->role, "source") == 0) {
            assert(node->label);
            assert(node->source);
            assert(node->display_label);
            assert(strcmp(node->label, "close") == 0);
            assert(strcmp(node->source, "close") == 0);
            assert(strstr(node->display_label, "close") != NULL);
            saw_source = 1;
        } else if (node->role && strcmp(node->role, "index") == 0) {
            assert(node->label);
            assert(node->source);
            assert(node->display_label);
            assert(strcmp(node->label, "3") == 0);
            assert(strcmp(node->source, "3") == 0);
            assert(strcmp(node->display_label, "3") == 0);
            saw_index = 1;
        }
    }
    assert(saw_source);
    assert(saw_index);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_labels_rsi_positional_arg0(void) {
    cxpr_expr_ast* ast = parse_required("rsi(14)");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    int saw_arg0 = 0;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->role && strcmp(node->role, "arg0") == 0) {
            assert(node->label);
            assert(node->source);
            assert(node->display_label);
            assert(strcmp(node->label, "arg0") == 0);
            assert(strcmp(node->source, "14") == 0);
            assert(strstr(node->display_label, "arg0") != NULL);
            assert(strstr(node->display_label, "14") != NULL);
            saw_arg0 = 1;
        }
    }
    assert(saw_arg0);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_labels_registered_function_arg_name(void) {
    static const char* const rsi_params[] = { "period" };
    cxpr_expr_ast* ast = parse_required("rsi(14)");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    size_t function_id = 0u;
    int saw_period = 0;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    cxpr_registry_add(reg, "rsi", test_rsi, 1, 1, NULL, NULL);
    assert(cxpr_registry_set_param_names(reg, "rsi", rsi_params, 1));
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->role && strcmp(node->role, "period") == 0) {
            assert(node->label);
            assert(node->source);
            assert(node->display_label);
            assert(strcmp(node->label, "period") == 0);
            assert(strcmp(node->source, "14") == 0);
            assert(strcmp(node->display_label, "period =\n14") == 0);
            assert(node->has_parent);
            assert(node->parent_id == function_id);
            saw_period = 1;
        }
    }
    assert(saw_period);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_display_includes_resolved_and_final_value(void) {
    cxpr_expr_ast* ast = parse_required("liquidity > 0");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    const cxpr_snapshot_node* root;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    cxpr_context_set(ctx, "liquidity", 19617.0);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));
    assert(snapshot.node_count > 0);
    root = &snapshot.nodes[0];
    assert(root->display_label);
    assert(strcmp(root->display_label, ">\n= true") == 0);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_trend_call_folds_function_and_current_sample(void) {
    cxpr_expr_ast* ast = parse_required("falling(close, 2)");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    int saw_value_close = 0;
    int saw_samples = 0;
    int saw_sample0 = 0;
    int saw_sample1_under_call = 0;
    size_t function_id = 0u;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    cxpr_context_set(ctx, "close", 104.1);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->role && strcmp(node->role, "value") == 0) {
            assert(node->source);
            assert(strcmp(node->source, "close") == 0);
            assert(node->has_parent);
            assert(node->parent_id == function_id);
            saw_value_close = 1;
        } else if (node->role && strcmp(node->role, "samples") == 0) {
            assert(node->source);
            assert(strcmp(node->source, "2") == 0);
            assert(node->has_parent);
            assert(node->parent_id == function_id);
            saw_samples = 1;
        } else if (node->role && strcmp(node->role, "sample[0]") == 0) {
            saw_sample0 = 1;
        } else if (node->role && strcmp(node->role, "sample[1]") == 0) {
            assert(node->source);
            assert(strcmp(node->source, "close[1]") == 0);
            assert(strncmp(node->display_label, "sample[1]", 9u) != 0);
            assert(node->has_parent);
            assert(node->parent_id == function_id);
            saw_sample1_under_call = 1;
        }
    }

    assert(saw_value_close);
    assert(saw_samples);
    assert(!saw_sample0);
    assert(saw_sample1_under_call);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_positional_identifier_arg_displays_source(void) {
    cxpr_expr_ast* ast = parse_required("max(close, close[1], close[3])");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    int saw_arg0_close = 0;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    cxpr_context_set(ctx, "close", 104.1);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->role && strcmp(node->role, "arg0") == 0) {
            assert(node->source);
            assert(node->display_label);
            assert(strcmp(node->source, "close") == 0);
            assert(strstr(node->display_label, "arg0 =\nclose\n= 104.1") != NULL);
            saw_arg0_close = 1;
        }
    }
    assert(saw_arg0_close);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_named_function_args_attach_to_call_node(void) {
    cxpr_expr_ast* ast = parse_required("within(close, 100, 110)");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    size_t function_id = 0u;
    int saw_source = 0;
    int saw_min = 0;
    int saw_max = 0;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    cxpr_context_set(ctx, "close", 104.1);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));

    assert(strcmp(snapshot.nodes[0].label, "within") == 0);
    assert(strncmp(snapshot.nodes[0].display_label, "within\n= ", 9u) == 0);

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->role && strcmp(node->role, "source") == 0 &&
                   node->source && strcmp(node->source, "close") == 0) {
            assert(node->has_parent);
            assert(node->parent_id == function_id);
            assert(strcmp(node->display_label, "source =\nclose\n= 104.1") == 0);
            saw_source = 1;
        } else if (node->role && strcmp(node->role, "min") == 0) {
            assert(node->has_parent);
            assert(node->parent_id == function_id);
            assert(strcmp(node->display_label, "min =\n100") == 0);
            saw_min = 1;
        } else if (node->role && strcmp(node->role, "max") == 0) {
            assert(node->has_parent);
            assert(node->parent_id == function_id);
            assert(strcmp(node->display_label, "max =\n110") == 0);
            saw_max = 1;
        }
    }

    assert(saw_source);
    assert(saw_min);
    assert(saw_max);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
}

static void test_context_free_function_snapshot_keeps_call_and_args_separate(void) {
    cxpr_expr_ast* ast = parse_required(
        "bollinger(source=close, period=$bb_params.period, stddev_mult=$bb_params.stddev)");
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};

    assert(cxpr_eval_snapshot_build(ast, NULL, NULL, &snapshot, &err));
    assert(snapshot.node_count == 4u);
    assert(strcmp(snapshot.nodes[0].label, "bollinger") == 0);
    assert(strcmp(snapshot.nodes[0].display_label, "bollinger") == 0);
    assert(strcmp(snapshot.nodes[1].role, "source") == 0);
    assert(strcmp(snapshot.nodes[2].role, "period") == 0);
    assert(strcmp(snapshot.nodes[3].role, "stddev_mult") == 0);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_restores_public_window_nodes(void) {
    cxpr_expr_ast* ast = parse_required("max(window(high, peak_bars))");
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    size_t window_id = (size_t)-1;

    assert(reg);
    cxpr_register_defaults(reg);
    assert(cxpr_eval_snapshot_build(ast, NULL, reg, &snapshot, &err));
    assert(strcmp(snapshot.nodes[0].label, "max") == 0);
    for (size_t i = 0u; i < snapshot.node_count; ++i) {
        assert(strncmp(snapshot.nodes[i].label, "__cxpr_", 7u) != 0);
        if (strcmp(snapshot.nodes[i].label, "window") == 0) window_id = snapshot.nodes[i].id;
    }
    assert(window_id != (size_t)-1);
    assert(snapshot.nodes[window_id].has_parent);
    assert(snapshot.nodes[window_id].parent_id == snapshot.nodes[0].id);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_expr_ast_free(ast);
}

static void test_context_free_nested_max_calls_keep_number_type(void) {
    cxpr_expr_ast* ast = parse_required(
        "max(high - low, max(abs(high - close[1]), abs(low - close[1])))");
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    size_t typed_max_count = 0u;

    assert(reg);
    cxpr_register_defaults(reg);
    assert(cxpr_eval_snapshot_build(ast, NULL, reg, &snapshot, &err));
    for (size_t i = 0u; i < snapshot.node_count; ++i) {
        if (strcmp(snapshot.nodes[i].label, "max") != 0) continue;
        assert(snapshot.nodes[i].has_inferred_type);
        assert(snapshot.nodes[i].inferred_type == CXPR_VALUE_NUMBER);
        typed_max_count++;
    }
    assert(typed_max_count == 2u);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_expr_ast_free(ast);
}

static void test_context_free_lookback_is_symbolic_without_resolver(void) {
    cxpr_expr_ast* ast = parse_required("k.histogram[1]");
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};

    assert(reg);
    cxpr_register_defaults(reg);
    assert(cxpr_eval_snapshot_build(ast, NULL, reg, &snapshot, &err));
    assert(snapshot.node_count > 0u);
    assert(snapshot.nodes[0].state == CXPR_SNAPSHOT_STATE_UNKNOWN);
    assert(snapshot.nodes[0].resolved);
    assert(strcmp(snapshot.nodes[0].resolved, "k.histogram[1]") == 0);
    assert(!strstr(snapshot.nodes[0].display_label, "lookback resolver"));

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_expr_ast_free(ast);
}

static void test_context_free_ternary_keeps_both_branches_symbolic(void) {
    cxpr_expr_ast* ast = parse_required("session.bar_index == 0 ? close[1] : close[2]");
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    int saw_true = 0;
    int saw_false = 0;

    assert(reg);
    cxpr_register_defaults(reg);
    assert(cxpr_eval_snapshot_build(ast, NULL, reg, &snapshot, &err));
    for (size_t i = 0u; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        assert(node->state != CXPR_SNAPSHOT_STATE_ERROR);
        if (node->role && strcmp(node->role, "true") == 0) {
            assert(strncmp(node->display_label, "true", 4u) != 0);
            saw_true = 1;
        }
        if (node->role && strcmp(node->role, "false") == 0) {
            assert(strncmp(node->display_label, "false", 5u) != 0);
            saw_false = 1;
        }
    }
    assert(saw_true);
    assert(saw_false);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_expr_ast_free(ast);
}

static void test_context_free_operator_node_only_displays_operator(void) {
    cxpr_expr_ast* ast = parse_required("1 - gap_pct");
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};

    assert(reg);
    cxpr_register_defaults(reg);
    assert(cxpr_eval_snapshot_build(ast, NULL, reg, &snapshot, &err));
    assert(strcmp(snapshot.nodes[0].kind, "binary_op") == 0);
    assert(strcmp(snapshot.nodes[0].display_label, "-") == 0);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_nested_function_args_attach_to_call_node(void) {
    static const char* const rsi_params[] = { "period" };
    cxpr_expr_ast* ast = parse_required("close < close[1] or rsi(14) > 70");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    size_t rsi_call_id = (size_t)-1;
    int saw_period_under_call = 0;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    cxpr_registry_add(reg, "rsi", test_rsi, 1, 1, NULL, NULL);
    assert(cxpr_registry_set_param_names(reg, "rsi", rsi_params, 1));
    cxpr_context_set(ctx, "close", 104.1);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->role && strcmp(node->role, "left") == 0 &&
            node->source && strcmp(node->source, "rsi(14)") == 0) {
            rsi_call_id = node->id;
        }
    }

    assert(rsi_call_id != (size_t)-1);

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->role && strcmp(node->role, "period") == 0) {
            assert(node->has_parent);
            assert(node->parent_id == rsi_call_id);
            assert(strcmp(node->display_label, "period =\n14") == 0);
            saw_period_under_call = 1;
        }
    }

    assert(saw_period_under_call);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_contains_args_attach_to_call_node(void) {
    cxpr_expr_ast* ast = parse_required("contains(citizenship_code, [100, 101])");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    size_t function_id = 0u;
    int saw_source = 0;
    int saw_values = 0;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    cxpr_context_set(ctx, "citizenship_code", 100.0);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->role && strcmp(node->role, "source") == 0 &&
                   node->source && strcmp(node->source, "citizenship_code") == 0) {
            assert(node->has_parent);
            assert(node->parent_id == function_id);
            assert(strcmp(node->display_label, "source =\ncitizenship_code\n= 100") == 0);
            saw_source = 1;
        } else if (node->role && strcmp(node->role, "values") == 0) {
            assert(node->has_parent);
            assert(node->parent_id == function_id);
            assert(strstr(node->display_label, "values =\n[100, 101]") != NULL);
            saw_values = 1;
        }
    }

    assert(saw_source);
    assert(saw_values);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_producer_args_attach_to_call_node(void) {
    cxpr_expr_ast* ast = parse_required("macd(fast=12, slow=26, signal=9).line");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    size_t function_id = 0u;
    int saw_field = 0;
    int saw_fast = 0;
    int saw_slow = 0;
    int saw_signal = 0;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));
    assert(strcmp(snapshot.nodes[0].kind, "producer_access") == 0);
    assert(strcmp(snapshot.nodes[0].label, "macd") == 0);

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->role && strcmp(node->role, "field") == 0) {
            assert(strcmp(node->kind, "field") == 0);
            assert(node->has_parent);
            assert(node->parent_id == function_id);
            assert(strcmp(node->display_label, "line") == 0);
            saw_field = 1;
        } else if (node->role && strcmp(node->role, "fast") == 0) {
            assert(node->has_parent);
            assert(node->parent_id == function_id);
            assert(strcmp(node->display_label, "fast =\n12") == 0);
            saw_fast = 1;
        } else if (node->role && strcmp(node->role, "slow") == 0) {
            assert(node->has_parent);
            assert(node->parent_id == function_id);
            assert(strcmp(node->display_label, "slow =\n26") == 0);
            saw_slow = 1;
        } else if (node->role && strcmp(node->role, "signal") == 0) {
            assert(node->has_parent);
            assert(node->parent_id == function_id);
            assert(strcmp(node->display_label, "signal =\n9") == 0);
            saw_signal = 1;
        }
    }

    assert(saw_field);
    assert(saw_fast);
    assert(saw_slow);
    assert(saw_signal);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_field_access_has_object_and_field_children(void) {
    cxpr_expr_ast* ast = parse_required("account.balance");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    int saw_object = 0;
    int saw_field = 0;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    cxpr_context_set(ctx, "account.balance", 1250.0);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->role && strcmp(node->role, "object") == 0) {
            assert(node->parent_id == 0u);
            assert(strcmp(node->display_label, "account") == 0);
            saw_object = 1;
        } else if (node->role && strcmp(node->role, "field") == 0) {
            assert(node->parent_id == 0u);
            assert(strcmp(node->display_label, "balance") == 0);
            saw_field = 1;
        }
    }

    assert(saw_object);
    assert(saw_field);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_chain_access_has_segment_children(void) {
    cxpr_expr_ast* ast = parse_required("risk.model.score");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    int saw_segment0 = 0;
    int saw_segment1 = 0;
    int saw_segment2 = 0;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    cxpr_context_set(ctx, "risk.model.score", 0.82);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->role && strcmp(node->role, "segment[0]") == 0) {
            assert(node->parent_id == 0u);
            assert(strcmp(node->display_label, "segment[0] =\nrisk") == 0);
            saw_segment0 = 1;
        } else if (node->role && strcmp(node->role, "segment[1]") == 0) {
            assert(node->parent_id == 0u);
            assert(strcmp(node->display_label, "segment[1] =\nmodel") == 0);
            saw_segment1 = 1;
        } else if (node->role && strcmp(node->role, "segment[2]") == 0) {
            assert(node->parent_id == 0u);
            assert(strcmp(node->display_label, "segment[2] =\nscore") == 0);
            saw_segment2 = 1;
        }
    }

    assert(saw_segment0);
    assert(saw_segment1);
    assert(saw_segment2);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_array_literal_has_item_children(void) {
    cxpr_expr_ast* ast = parse_required("[1, 2, 3]");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    int saw_item0 = 0;
    int saw_item1 = 0;
    int saw_item2 = 0;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->role && strcmp(node->role, "item[0]") == 0) {
            assert(node->parent_id == 0u);
            assert(strcmp(node->display_label, "item[0]: 1") == 0);
            saw_item0 = 1;
        } else if (node->role && strcmp(node->role, "item[1]") == 0) {
            assert(node->parent_id == 0u);
            assert(strcmp(node->display_label, "item[1]: 2") == 0);
            saw_item1 = 1;
        } else if (node->role && strcmp(node->role, "item[2]") == 0) {
            assert(node->parent_id == 0u);
            assert(strcmp(node->display_label, "item[2]: 3") == 0);
            saw_item2 = 1;
        }
    }

    assert(saw_item0);
    assert(saw_item1);
    assert(saw_item2);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
}

static void test_snapshot_complex_lookback_preserves_target_structure(void) {
    cxpr_expr_ast* ast = parse_required("account.balance[1]");
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_eval_snapshot snapshot;
    cxpr_error err = {0};
    size_t source_id = (size_t)-1;
    int saw_source = 0;
    int saw_object = 0;
    int saw_field = 0;

    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->role && strcmp(node->role, "source") == 0 &&
            node->kind && strcmp(node->kind, "field_access") == 0) {
            assert(node->parent_id == 0u);
            source_id = node->id;
            saw_source = 1;
        }
    }
    assert(saw_source);

    for (size_t i = 0; i < snapshot.node_count; ++i) {
        const cxpr_snapshot_node* node = &snapshot.nodes[i];
        if (node->parent_id == source_id &&
            node->role && strcmp(node->role, "object") == 0) {
            assert(strcmp(node->display_label, "account") == 0);
            saw_object = 1;
        } else if (node->parent_id == source_id &&
                   node->role && strcmp(node->role, "field") == 0) {
            assert(strcmp(node->display_label, "balance") == 0);
            saw_field = 1;
        }
    }

    assert(saw_object);
    assert(saw_field);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_ast_free(ast);
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

static void test_flow_snapshot_displays_resolved_and_final_value(void) {
    const cxpr_expression_def defs[] = {
        { "green", "eligible" },
        { "yellow", "not green" },
    };
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator;
    cxpr_context* ctx = cxpr_context_new();
    cxpr_eval_snapshot_flow flow;
    cxpr_error err = {0};
    int saw_yellow = 0;

    assert(reg);
    assert(ctx);
    cxpr_register_defaults(reg);
    evaluator = cxpr_evaluator_new(reg);
    assert(evaluator);
    assert(cxpr_expressions_add(evaluator, defs, 2, &err));
    assert(cxpr_evaluator_compile(evaluator, &err));

    cxpr_context_set_bool(ctx, "eligible", true);
    cxpr_expression_eval_all(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);
    assert(cxpr_eval_snapshot_build_flow(evaluator, ctx, reg, &flow, &err));

    for (size_t i = 0; i < flow.node_count; ++i) {
        if (strcmp(flow.nodes[i].name, "yellow") == 0) {
            assert(flow.nodes[i].display_label);
            assert(strstr(flow.nodes[i].display_label, "= not true\n= false") != NULL);
            saw_yellow = 1;
        }
    }
    assert(saw_yellow);

    cxpr_eval_snapshot_flow_free(&flow);
    cxpr_context_free(ctx);
    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
}

static void test_flow_snapshot_writes_host_metadata(void) {
    const cxpr_expression_def defs[] = {
        { "trend", "ema_fast > ema_slow" },
        { "entry", "rsi < 30 and trend" },
    };
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator;
    cxpr_context* ctx = cxpr_context_new();
    cxpr_eval_snapshot_flow flow;
    cxpr_error err = {0};
    cxpr_snapshot_json_hooks hooks = {
        "test-host",
        "test-host.v1",
        test_write_flow_node_host_json,
        test_write_ast_node_host_json,
        NULL,
    };
    FILE* json;
    char buf[8192];
    size_t nread;

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

    json = tmpfile();
    assert(json);
    assert(cxpr_eval_snapshot_flow_write_json_ex(&flow, &hooks, json));
    rewind(json);
    nread = fread(buf, 1, sizeof(buf) - 1u, json);
    buf[nread] = '\0';
    assert(strstr(buf, "\"schema\": \"cxpr.eval_snapshot_flow.v2\"") != NULL);
    assert(strstr(buf, "\"name\": \"test-host\"") != NULL);
    assert(strstr(buf, "\"role\": \"test-flow-node\"") != NULL);
    assert(strstr(buf, "\"role\": \"test-ast-node\"") != NULL);
    assert(strstr(buf, "\"has_value\": true") != NULL);
    assert(strstr(buf, "\"value_type\": \"number\"") != NULL);
    assert(strstr(buf, "\"typed_value\": { \"type\": \"number\", \"number\": 101.2 }") != NULL);
    assert(strstr(buf, "\"typed_value\": { \"type\": \"bool\", \"bool\": true }") != NULL);
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

static void test_snapshot_preserves_int64_type_and_value(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_eval_snapshot snapshot = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, "9007199254740993i64", &err);
    FILE* json;
    char buf[8192];
    size_t nread;

    assert(ast);
    assert(cxpr_eval_snapshot_build(ast, ctx, reg, &snapshot, &err));
    assert(snapshot.node_count > 0u);
    assert(snapshot.nodes[0].has_value);
    assert(snapshot.nodes[0].value.type == CXPR_VALUE_INT64);
    assert(snapshot.nodes[0].value.i64 == INT64_C(9007199254740993));
    assert(strcmp(snapshot.nodes[0].value_text, "9007199254740993") == 0);

    json = tmpfile();
    assert(json);
    assert(cxpr_eval_snapshot_write_json(&snapshot, json));
    rewind(json);
    nread = fread(buf, 1u, sizeof(buf) - 1u, json);
    buf[nread] = '\0';
    assert(strstr(buf, "\"value_type\": \"int64\"") != NULL);
    assert(strstr(buf, "\"int64\": 9007199254740993") != NULL);

    fclose(json);
    cxpr_eval_snapshot_free(&snapshot);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_expr_parser_free(parser);
}

static void test_snapshot_record_literal_has_named_field_children(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_eval_snapshot snapshot = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(
        parser,
        "{ entry_extension_atr = (close - trend_ema) / safe_atr, "
        "extension_ok = entry_extension_atr < $max_entry_extension_atr }",
        &err);
    int saw_extension_value = 0;
    int saw_extension_gate = 0;

    assert(ast);
    assert(cxpr_eval_snapshot_build(ast, NULL, reg, &snapshot, &err));
    assert(strcmp(snapshot.nodes[0].kind, "record") == 0);
    assert(snapshot.node_count > 2u);
    for (size_t i = 0u; i < snapshot.node_count; ++i) {
        if (strcmp(snapshot.nodes[i].role, "entry_extension_atr") == 0) {
            assert(strcmp(snapshot.nodes[i].display_label, "/") == 0);
            saw_extension_value = 1;
        }
        if (strcmp(snapshot.nodes[i].role, "extension_ok") == 0) {
            assert(strcmp(snapshot.nodes[i].display_label, "<") == 0);
            saw_extension_gate = 1;
        }
    }
    assert(saw_extension_value);
    assert(saw_extension_gate);

    cxpr_eval_snapshot_free(&snapshot);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_expr_parser_free(parser);
}

int main(void) {
    test_snapshot_marks_short_circuit_branch_skipped();
    test_snapshot_writes_host_metadata();
    test_snapshot_does_not_prefix_unary_operand_label();
    test_snapshot_labels_lookback_index_role();
    test_snapshot_labels_rsi_positional_arg0();
    test_snapshot_labels_registered_function_arg_name();
    test_snapshot_display_includes_resolved_and_final_value();
    test_snapshot_trend_call_folds_function_and_current_sample();
    test_snapshot_positional_identifier_arg_displays_source();
    test_snapshot_named_function_args_attach_to_call_node();
    test_context_free_function_snapshot_keeps_call_and_args_separate();
    test_snapshot_restores_public_window_nodes();
    test_context_free_nested_max_calls_keep_number_type();
    test_context_free_lookback_is_symbolic_without_resolver();
    test_context_free_ternary_keeps_both_branches_symbolic();
    test_context_free_operator_node_only_displays_operator();
    test_snapshot_nested_function_args_attach_to_call_node();
    test_snapshot_contains_args_attach_to_call_node();
    test_snapshot_producer_args_attach_to_call_node();
    test_snapshot_field_access_has_object_and_field_children();
    test_snapshot_chain_access_has_segment_children();
    test_snapshot_array_literal_has_item_children();
    test_snapshot_complex_lookback_preserves_target_structure();
    test_flow_snapshot_links_named_expressions();
    test_flow_snapshot_displays_resolved_and_final_value();
    test_flow_snapshot_writes_host_metadata();
    test_engine_flow_snapshot_uses_current_tick_state();
    test_snapshot_preserves_int64_type_and_value();
    test_snapshot_record_literal_has_named_field_children();
    printf("  \xE2\x9C\x93 eval_snapshot\n");
    return 0;
}
