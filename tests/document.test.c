/**
 * @file document.test.c
 * @brief Tests for generic .cxpr document manifests.
 */

#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    size_t total;
    size_t host_fields;
    size_t returns;
} document_ast_visit_counts;

static cxpr_visit_control count_document_ast_nodes(const cxpr_doc_ast_node* node,
                                                   void* userdata) {
    document_ast_visit_counts* counts = (document_ast_visit_counts*)userdata;
    counts->total++;
    if (cxpr_doc_ast_node_kind(node) == CXPR_DOC_AST_HOST_FIELD) {
        counts->host_fields++;
    }
    if (cxpr_doc_ast_node_kind(node) == CXPR_DOC_AST_RETURN) {
        counts->returns++;
    }
    return CXPR_VISIT_CONTINUE;
}

static void assert_models_have_same_public_shape(const cxpr_model* left,
                                                 const cxpr_model* right) {
    assert(left != NULL);
    assert(right != NULL);
    assert(strcmp(cxpr_model_name(left), cxpr_model_name(right)) == 0);
    assert(cxpr_model_input_count(left) == cxpr_model_input_count(right));
    for (size_t i = 0u; i < cxpr_model_input_count(left); ++i) {
        assert(strcmp(cxpr_model_input(left, i), cxpr_model_input(right, i)) == 0);
    }
    assert(cxpr_model_constant_count(left) == cxpr_model_constant_count(right));
    for (size_t i = 0u; i < cxpr_model_constant_count(left); ++i) {
        assert(strcmp(cxpr_model_constant_name(left, i),
                      cxpr_model_constant_name(right, i)) == 0);
    }
    assert(cxpr_model_binding_count(left) == cxpr_model_binding_count(right));
    for (size_t i = 0u; i < cxpr_model_binding_count(left); ++i) {
        assert(cxpr_model_binding_kind_at(left, i) ==
               cxpr_model_binding_kind_at(right, i));
        assert(strcmp(cxpr_model_binding_name(left, i),
                      cxpr_model_binding_name(right, i)) == 0);
    }
    assert(cxpr_model_output_count(left) == cxpr_model_output_count(right));
    for (size_t i = 0u; i < cxpr_model_output_count(left); ++i) {
        assert(strcmp(cxpr_model_output(left, i), cxpr_model_output(right, i)) == 0);
    }
}

static void test_manifest_document_accepts_host_blocks_without_model(void) {
    const char* source =
        "project {\n"
        "  name = \"dynasty\"\n"
        "  language = \"c\"\n"
        "}\n"
        "vsix {\n"
        "  recommended = [\"dynasty.cxpr-tools\"]\n"
        "}\n";
    cxpr_error err = {0};
    cxpr_doc* document = cxpr_doc_parse_manifest(source, &err);
    const cxpr_model_host_block* project;
    const cxpr_model_host_block* vsix;

    assert(document != NULL);
    assert(err.code == CXPR_OK);
    assert(cxpr_doc_model(document) == NULL);
    assert(cxpr_doc_host_block_count(document) == 2u);

    project = cxpr_doc_host_block(document, "project");
    vsix = cxpr_doc_host_block(document, "vsix");
    assert(project != NULL);
    assert(vsix != NULL);
    assert(strcmp(cxpr_host_block_field_value_by_key(project, "name"), "\"dynasty\"") == 0);
    assert(strcmp(cxpr_host_block_field_value_by_key(project, "language"), "\"c\"") == 0);
    assert(strcmp(cxpr_host_block_field_value_by_key(vsix, "recommended"),
                  "[\"dynasty.cxpr-tools\"]") == 0);

    cxpr_doc_free(document);
    printf("  ✓ test_manifest_document_accepts_host_blocks_without_model\n");
}

static void test_manifest_document_rejects_model_syntax_without_extension(void) {
    const char* source =
        "model strategy\n"
        "in { close }\n"
        "out close\n";
    cxpr_error err = {0};
    cxpr_doc* document = cxpr_doc_parse_manifest(source, &err);

    assert(document == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(err.message != NULL);
    assert(strstr(err.message, "CXPR_DOC_EXTENSION_MODEL") != NULL);
    printf("  ✓ test_manifest_document_rejects_model_syntax_without_extension\n");
}

static void test_document_model_extension_exposes_model_view(void) {
    const char* source =
        "project { name = \"dynasty\" }\n"
        "model strategy\n"
        "in { close }\n"
        "signal = close > 0\n"
        "out signal\n";
    cxpr_error err = {0};
    cxpr_doc* document = cxpr_doc_parse_model(source, &err);
    const cxpr_model* model;
    cxpr_model_program* program;
    cxpr_model_session* session;
    cxpr_context* ctx;
    cxpr_struct_value* signal;
    cxpr_struct_value* position;
    bool entry_ok = true;
    const char* signal_fields[] = {"fills_next_session", "fills_session_close"};
    cxpr_value signal_values[] = {cxpr_bool(false), cxpr_bool(false)};
    const char* position_fields[] = {"entry_price", "bars_in_position"};
    cxpr_value position_values[] = {cxpr_num(0.0), cxpr_num(0.0)};

    assert(document != NULL);
    assert(err.code == CXPR_OK);
    model = cxpr_doc_model(document);
    assert(model != NULL);
    assert(strcmp(cxpr_model_name(model), "strategy") == 0);
    assert(cxpr_model_input_count(model) == 1u);
    assert(strcmp(cxpr_model_input(model, 0u), "close") == 0);
    assert(cxpr_model_output_count(model) == 1u);
    assert(strcmp(cxpr_model_output(model, 0u), "signal") == 0);
    assert(cxpr_doc_host_block_count(document) == 1u);
    assert(strcmp(cxpr_host_block_field_value_by_key(
                      cxpr_doc_host_block(document, "project"), "name"),
                  "\"dynasty\"") == 0);

    cxpr_doc_free(document);
    printf("  ✓ test_document_model_extension_exposes_model_view\n");
}

static void test_document_input_defaults_lower_to_params(void) {
    const char* source =
        "model configurable_indicator\n"
        "in source, $period = 20, $slope_bars = 5\n"
        "$epsilon = 0.001\n"
        "value = source + $period + $slope_bars + $epsilon\n"
        "out value\n";
    cxpr_error err = {0};
    cxpr_model* model = cxpr_model_parse(source, &err);
    double metadata_number = 0.0;

    assert(model != NULL);
    assert(err.code == CXPR_OK);
    assert(cxpr_model_input_count(model) == 1u);
    assert(strcmp(cxpr_model_input(model, 0u), "source") == 0);
    assert(cxpr_model_constant_count(model) == 3u);
    assert(strcmp(cxpr_model_constant_name(model, 0u), "period") == 0);
    assert(strcmp(cxpr_model_constant_name(model, 1u), "slope_bars") == 0);
    assert(strcmp(cxpr_model_constant_name(model, 2u), "epsilon") == 0);
    assert(cxpr_model_call_param_count(model) == 2u);
    assert(cxpr_model_constant_is_call_param(model, 0u));
    assert(cxpr_model_constant_is_call_param(model, 1u));
    assert(!cxpr_model_constant_is_call_param(model, 2u));
    assert(cxpr_model_validate(model, &err));

    cxpr_model_free(model);

    err = (cxpr_error){0};
    model = cxpr_model_parse(
        "model configurable_with_metadata\n"
        "in {\n"
        "    source\n"
        "    $period = 20 { min = 2, max = 512, description = \"Window period\" }\n"
        "}\n"
        "out source\n",
        &err);
    assert(model != NULL);
    assert(cxpr_model_call_param_count(model) == 1u);
    assert(cxpr_model_metadata_count(model) == 1u);
    assert(cxpr_model_metadata_target_kind_at(model, 0u) ==
           CXPR_MODEL_METADATA_TARGET_PARAM);
    assert(strcmp(cxpr_model_metadata_target_name(model, 0u), "period") == 0);
    assert(cxpr_model_metadata_field_number(model, 0u, "min", &metadata_number));
    assert(metadata_number == 2.0);
    assert(cxpr_model_metadata_field_number(model, 0u, "max", &metadata_number));
    assert(metadata_number == 512.0);
    assert(strcmp(cxpr_model_metadata_field_value(
                      model, 0u, "description"),
                  "\"Window period\"") == 0);
    cxpr_model_free(model);

    err = (cxpr_error){0};
    model = cxpr_model_parse(
        "model ambiguous\n"
        "in source, period = 20\n"
        "out source\n",
        &err);
    assert(model == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    printf("  ✓ test_document_input_defaults_lower_to_params\n");
}

static void test_document_exposes_owned_syntax_tree(void) {
    const char* source =
        "model strategy\n"
        "in { close }\n"
        "state { signal = 0 }\n"
        "out signal := close\n";
    cxpr_error err = {0};
    cxpr_doc* document = cxpr_doc_parse_model(source, &err);
    const cxpr_doc_ast* syntax;
    const cxpr_doc_ast_node* root;
    const cxpr_doc_ast_node* state_block;
    const cxpr_doc_ast_node* output_update;

    assert(document != NULL);
    assert(err.code == CXPR_OK);
    syntax = cxpr_doc_ast_view(document);
    assert(syntax != NULL);
    root = cxpr_doc_ast_root(syntax);
    assert(root != NULL);
    assert(cxpr_doc_ast_node_kind(root) == CXPR_DOC_AST_FILE);
    assert(cxpr_doc_ast_node_child_count(root) == 4u);
    state_block = cxpr_doc_ast_node_child(root, 2u);
    output_update = cxpr_doc_ast_node_child(root, 3u);
    assert(cxpr_doc_ast_node_kind(state_block) == CXPR_DOC_AST_STATE_BLOCK);
    assert(cxpr_doc_ast_node_child_count(state_block) == 1u);
    assert(strcmp(cxpr_doc_ast_node_name(cxpr_doc_ast_node_child(state_block, 0u)),
                  "signal") == 0);
    assert(cxpr_doc_ast_node_kind(output_update) ==
           CXPR_DOC_AST_OUTPUT_STATE_UPDATE);
    assert(strcmp(cxpr_doc_ast_node_name(output_update), "signal") == 0);
    assert(cxpr_doc_ast_node_expr(output_update) != NULL);

    cxpr_doc_free(document);
    printf("  ✓ test_document_exposes_owned_syntax_tree\n");
}

static void test_document_ast_exposes_compact_initial_state_update(void) {
    const char* source =
        "model counter\n"
        "bars := bars + 1 initial 0\n"
        "out bars\n";
    cxpr_error err = {0};
    cxpr_doc_ast* syntax =
        cxpr_doc_ast_parse(source, "counter.cxpr", CXPR_DOC_EXTENSION_MODEL, &err);
    const cxpr_doc_ast_node* root;
    const cxpr_doc_ast_node* update;
    const cxpr_doc_ast_node* declaration;

    assert(syntax != NULL);
    assert(err.code == CXPR_OK);
    root = cxpr_doc_ast_root(syntax);
    assert(cxpr_doc_ast_node_child_count(root) == 3u);
    update = cxpr_doc_ast_node_child(root, 1u);
    assert(cxpr_doc_ast_node_kind(update) == CXPR_DOC_AST_INITIAL_STATE_UPDATE);
    assert(strcmp(cxpr_doc_ast_node_name(update), "bars") == 0);
    assert(cxpr_doc_ast_node_expr(update) != NULL);
    assert(cxpr_doc_ast_node_child_count(update) == 1u);
    declaration = cxpr_doc_ast_node_child(update, 0u);
    assert(cxpr_doc_ast_node_kind(declaration) == CXPR_DOC_AST_STATE_DECL);
    assert(strcmp(cxpr_doc_ast_node_name(declaration), "bars") == 0);
    assert(strcmp(cxpr_doc_ast_node_text(declaration), "0") == 0);
    assert(cxpr_doc_ast_node_expr(declaration) != NULL);

    cxpr_doc_ast_free(syntax);
    printf("  ✓ test_document_ast_exposes_compact_initial_state_update\n");
}

static void test_parse_document_ast_preserves_block_shapes(void) {
    const char* source =
        "project { name = \"dynasty\" }\n"
        "model strategy { source_arg = \"close\" }\n"
        "${ fast = 12, slow = 26 { min = 10, max = 80 } }\n"
        "state {\n"
        "  score = 0\n"
        "}\n"
        "score := score + close\n"
        "out score { role = \"score\" }\n";
    cxpr_error err = {0};
    cxpr_doc_ast* syntax =
        cxpr_doc_ast_parse(source, "shape.cxpr", CXPR_DOC_EXTENSION_MODEL, &err);
    const cxpr_doc_ast_node* root;
    const cxpr_doc_ast_node* host;
    const cxpr_doc_ast_node* model_decl;
    const cxpr_doc_ast_node* params;
    const cxpr_doc_ast_node* slow_param;
    const cxpr_doc_ast_node* metadata;
    const cxpr_doc_ast_node* state;
    const cxpr_doc_ast_node* update;
    const cxpr_doc_ast_node* output;
    cxpr_source_span update_span;

    assert(syntax != NULL);
    assert(err.code == CXPR_OK);
    assert(strcmp(cxpr_doc_ast_source_name(syntax), "shape.cxpr") == 0);
    root = cxpr_doc_ast_root(syntax);
    assert(cxpr_doc_ast_node_child_count(root) == 6u);
    host = cxpr_doc_ast_node_child(root, 0u);
    params = cxpr_doc_ast_node_child(root, 2u);
    state = cxpr_doc_ast_node_child(root, 3u);
    update = cxpr_doc_ast_node_child(root, 4u);
    assert(cxpr_doc_ast_node_kind(host) == CXPR_DOC_AST_HOST_BLOCK);
    assert(strcmp(cxpr_doc_ast_node_name(host), "project") == 0);
    model_decl = cxpr_doc_ast_node_child(root, 1u);
    assert(cxpr_doc_ast_node_kind(model_decl) == CXPR_DOC_AST_MODEL_DECL);
    assert(cxpr_doc_ast_node_child_count(model_decl) == 1u);
    metadata = cxpr_doc_ast_node_child(model_decl, 0u);
    assert(cxpr_doc_ast_node_kind(metadata) == CXPR_DOC_AST_METADATA);
    assert(strstr(cxpr_doc_ast_node_text(metadata), "source_arg") != NULL);
    assert(cxpr_doc_ast_node_kind(params) == CXPR_DOC_AST_PARAM_BLOCK);
    assert(cxpr_doc_ast_node_child_count(params) == 2u);
    slow_param = cxpr_doc_ast_node_child(params, 1u);
    assert(strcmp(cxpr_doc_ast_node_name(slow_param), "slow") == 0);
    assert(cxpr_doc_ast_node_expr(slow_param) != NULL);
    assert(cxpr_doc_ast_node_child_count(slow_param) == 1u);
    metadata = cxpr_doc_ast_node_child(slow_param, 0u);
    assert(cxpr_doc_ast_node_kind(metadata) == CXPR_DOC_AST_METADATA);
    assert(strstr(cxpr_doc_ast_node_text(metadata), "max = 80") != NULL);
    assert(cxpr_doc_ast_node_span(metadata).start.line == 3u);
    assert(cxpr_doc_ast_node_kind(state) == CXPR_DOC_AST_STATE_BLOCK);
    assert(cxpr_doc_ast_node_child_count(state) == 1u);
    assert(cxpr_doc_ast_node_kind(update) == CXPR_DOC_AST_STATE_UPDATE);
    assert(strcmp(cxpr_doc_ast_node_name(update), "score") == 0);
    update_span = cxpr_doc_ast_node_span(update);
    assert(update_span.start.line == 7u);
    assert(update_span.end.offset > update_span.start.offset);
    output = cxpr_doc_ast_node_child(root, 5u);
    assert(cxpr_doc_ast_node_kind(output) == CXPR_DOC_AST_OUTPUT_DECL);
    assert(strcmp(cxpr_doc_ast_node_name(output), "score") == 0);
    assert(cxpr_doc_ast_node_child_count(output) == 1u);
    metadata = cxpr_doc_ast_node_child(output, 0u);
    assert(cxpr_doc_ast_node_kind(metadata) == CXPR_DOC_AST_METADATA);
    assert(strstr(cxpr_doc_ast_node_text(metadata), "role") != NULL);

    cxpr_doc_ast_free(syntax);
    printf("  ✓ test_parse_document_ast_preserves_block_shapes\n");
}

static void test_document_ast_lowers_to_independent_document(void) {
    const char* source =
        "model strategy\n"
        "in { close }\n"
        "signal = close > 0\n"
        "out signal\n";
    cxpr_error err = {0};
    cxpr_doc_ast* syntax =
        cxpr_doc_ast_parse(source, "lower.cxpr", CXPR_DOC_EXTENSION_MODEL, &err);
    cxpr_doc* document;
    const cxpr_model* model;

    assert(syntax != NULL);
    document = cxpr_doc_ast_lower(syntax, &err);
    cxpr_doc_ast_free(syntax);

    assert(document != NULL);
    assert(err.code == CXPR_OK);
    assert(cxpr_doc_ast_view(document) != NULL);
    model = cxpr_doc_model(document);
    assert(model != NULL);
    assert(strcmp(cxpr_model_name(model), "strategy") == 0);
    assert(cxpr_model_output_count(model) == 1u);
    assert(strcmp(cxpr_model_output(model, 0u), "signal") == 0);

    cxpr_doc_free(document);
    printf("  ✓ test_document_ast_lowers_to_independent_document\n");
}

static void test_document_ast_lowering_equivalent_block_and_shorthand_forms(void) {
    const char* shorthand =
        "model equivalent\n"
        "in close\n"
        "$fast = 12\n"
        "$slow = 26\n"
        "state score = 0\n"
        "score := score + close + $fast - $slow\n"
        "out score\n";
    const char* block =
        "model equivalent\n"
        "in { close }\n"
        "${ fast = 12, slow = 26 }\n"
        "state { score = 0 }\n"
        "score := score + close + $fast - $slow\n"
        "out { score }\n";
    cxpr_error err = {0};
    cxpr_doc* shorthand_document = cxpr_doc_parse_model(shorthand, &err);
    cxpr_doc* block_document;

    assert(shorthand_document != NULL);
    assert(err.code == CXPR_OK);
    block_document = cxpr_doc_parse_model(block, &err);
    assert(block_document != NULL);
    assert(err.code == CXPR_OK);

    assert_models_have_same_public_shape(cxpr_doc_model(shorthand_document),
                                         cxpr_doc_model(block_document));

    cxpr_doc_free(block_document);
    cxpr_doc_free(shorthand_document);
    printf("  ✓ test_document_ast_lowering_equivalent_block_and_shorthand_forms\n");
}

static void test_document_ast_lowering_rejects_state_output_assignment(void) {
    const char* source =
        "model invalid\n"
        "state signal = 0\n"
        "out signal = close\n";
    cxpr_error err = {0};
    cxpr_doc* document = cxpr_doc_parse_model(source, &err);

    assert(document == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(strcmp(err.message, "State updates must use ':=' assignments") == 0);

    printf("  ✓ test_document_ast_lowering_rejects_state_output_assignment\n");
}

static void test_document_ast_lowering_handles_use_aliases_and_groups(void) {
    const char* source =
        "model uses\n"
        "use { atr, ema } from indicators\n"
        "use robotics as r\n"
        "in high, low, close\n"
        "out high, close\n";
    cxpr_error err = {0};
    cxpr_doc* document = cxpr_doc_parse_model(source, &err);
    const cxpr_model* model;
    cxpr_source_span span;

    assert(document != NULL);
    assert(err.code == CXPR_OK);
    model = cxpr_doc_model(document);
    assert(model != NULL);
    assert(cxpr_model_use_count(model) == 3u);
    assert(strcmp(cxpr_model_use(model, 0u), "indicators/atr") == 0);
    assert(strcmp(cxpr_model_use(model, 1u), "indicators/ema") == 0);
    assert(strcmp(cxpr_model_use(model, 2u), "robotics") == 0);
    assert(cxpr_model_use_alias(model, 0u) == NULL);
    assert(cxpr_model_use_alias(model, 1u) == NULL);
    assert(strcmp(cxpr_model_use_alias(model, 2u), "r") == 0);
    assert(cxpr_model_input_count(model) == 3u);
    assert(strcmp(cxpr_model_input(model, 0u), "high") == 0);
    assert(strcmp(cxpr_model_input(model, 2u), "close") == 0);
    assert(cxpr_model_output_count(model) == 2u);
    assert(strcmp(cxpr_model_output(model, 0u), "high") == 0);
    assert(strcmp(cxpr_model_output(model, 1u), "close") == 0);
    assert(cxpr_model_use_source_span(model, 0u, &span));
    assert(span.start.line == 2u);

    cxpr_doc_free(document);
    printf("  ✓ test_document_ast_lowering_handles_use_aliases_and_groups\n");
}

static void test_document_ast_lowering_handles_struct_input_blocks(void) {
    const char* source =
        "model struct_inputs\n"
        "in signal { fills_next_session, fills_session_close }\n"
        "in position {\n"
        "  entry_price\n"
        "  bars_in_position\n"
        "}\n"
        "entry_ok = not signal.fills_next_session and position.bars_in_position == 0\n"
        "out entry_ok\n";
    cxpr_error err = {0};
    cxpr_doc* document = cxpr_doc_parse_model(source, &err);
    const cxpr_model* model;
    cxpr_model_program* program;
    cxpr_model_session* session;
    cxpr_context* ctx;
    cxpr_struct_value* signal;
    cxpr_struct_value* position;
    bool entry_ok = true;
    const char* signal_fields[] = {"fills_next_session", "fills_session_close"};
    cxpr_value signal_values[] = {cxpr_bool(false), cxpr_bool(false)};
    const char* position_fields[] = {"entry_price", "bars_in_position"};
    cxpr_value position_values[] = {cxpr_num(0.0), cxpr_num(0.0)};

    assert(document != NULL);
    assert(err.code == CXPR_OK);
    model = cxpr_doc_model(document);
    assert(model != NULL);
    assert(cxpr_model_input_count(model) == 4u);
    assert(strcmp(cxpr_model_input(model, 0u), "signal.fills_next_session") == 0);
    assert(strcmp(cxpr_model_input(model, 1u), "signal.fills_session_close") == 0);
    assert(strcmp(cxpr_model_input(model, 2u), "position.entry_price") == 0);
    assert(strcmp(cxpr_model_input(model, 3u), "position.bars_in_position") == 0);

    program = cxpr_compile_model(model, NULL, &err);
    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &err);
    assert(session != NULL);
    ctx = cxpr_model_session_context(session);
    signal = cxpr_struct_value_new(signal_fields, signal_values, 2u);
    position = cxpr_struct_value_new(position_fields, position_values, 2u);
    assert(signal != NULL);
    assert(position != NULL);
    cxpr_context_set_struct(ctx, "signal", signal);
    cxpr_context_set_struct(ctx, "position", position);
    cxpr_struct_value_free(signal);
    cxpr_struct_value_free(position);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "entry_ok", &entry_ok));
    assert(entry_ok);

    cxpr_model_session_free(session);
    cxpr_model_program_free(program);
    cxpr_doc_free(document);
    printf("  ✓ test_document_ast_lowering_handles_struct_input_blocks\n");
}

static void test_document_ast_lowering_handles_scalar_function_declaration(void) {
    const char* source =
        "model function_doc\n"
        "fn above(src, threshold) = src > threshold\n"
        "in close\n"
        "$threshold = 10\n"
        "signal = above(close, $threshold)\n"
        "out signal\n";
    cxpr_error err = {0};
    cxpr_doc* document = cxpr_doc_parse_model(source, &err);
    cxpr_model_program* program;

    assert(document != NULL);
    assert(err.code == CXPR_OK);
    assert(cxpr_doc_model(document) != NULL);
    program = cxpr_compile_model(cxpr_doc_model(document), NULL, &err);
    assert(program != NULL);
    assert(cxpr_model_program_function_count(program) == 1u);

    cxpr_model_program_free(program);
    cxpr_doc_free(document);
    printf("  ✓ test_document_ast_lowering_handles_scalar_function_declaration\n");
}

static void test_document_ast_lowering_handles_record_function_shorthand(void) {
    const char* source =
        "model record_function_doc\n"
        "in close\n"
        "fn bands(src) = upper = src + 1, lower = src - 1\n"
        "top = bands(close).upper\n"
        "bottom = bands(close).lower\n"
        "out { top, bottom }\n";
    cxpr_error err = {0};
    cxpr_doc* document = cxpr_doc_parse_model(source, &err);
    cxpr_model_program* program;
    cxpr_context* ctx;
    bool found = false;

    assert(document != NULL);
    assert(err.code == CXPR_OK);
    program = cxpr_compile_model(cxpr_doc_model(document), NULL, &err);
    assert(program != NULL);
    ctx = cxpr_context_new();
    assert(ctx != NULL);
    cxpr_context_set(ctx, "close", 10.0);
    assert(cxpr_eval_model_program(program, ctx, NULL, &err));
    assert(cxpr_context_get(ctx, "top", &found) == 11.0 && found);
    assert(cxpr_context_get(ctx, "bottom", &found) == 9.0 && found);

    cxpr_context_free(ctx);
    cxpr_model_program_free(program);
    cxpr_doc_free(document);
    printf("  ✓ test_document_ast_lowering_handles_record_function_shorthand\n");
}

static void test_document_ast_lowering_handles_scalar_function_block(void) {
    const char* source =
        "model function_block_doc\n"
        "fn impulse(src, base, threshold) {\n"
        "  delta = src - base\n"
        "  magnitude = abs(delta)\n"
        "  return magnitude > threshold\n"
        "}\n"
        "in close\n"
        "signal = impulse(close, 10, 2)\n"
        "out signal\n";
    cxpr_error err = {0};
    cxpr_doc* document = cxpr_doc_parse_model(source, &err);
    cxpr_model_program* program;
    cxpr_context* ctx;
    bool found = false;

    assert(document != NULL);
    assert(err.code == CXPR_OK);
    program = cxpr_compile_model(cxpr_doc_model(document), NULL, &err);
    assert(program != NULL);
    ctx = cxpr_context_new();
    assert(ctx != NULL);
    cxpr_context_set(ctx, "close", 13.0);
    assert(cxpr_eval_model_program(program, ctx, NULL, &err));
    assert(cxpr_context_get_bool(ctx, "signal", &found) && found);

    cxpr_context_free(ctx);
    cxpr_model_program_free(program);
    cxpr_doc_free(document);
    printf("  ✓ test_document_ast_lowering_handles_scalar_function_block\n");
}

static void test_document_ast_lowering_handles_record_function_block(void) {
    const char* source =
        "model record_function_block_doc\n"
        "in close\n"
        "fn bands(src) {\n"
        "  upper = src + 1\n"
        "  lower = src - 1\n"
        "  return { upper, lower }\n"
        "}\n"
        "top = bands(close).upper\n"
        "bottom = bands(close).lower\n"
        "out { top, bottom }\n";
    cxpr_error err = {0};
    cxpr_doc* document = cxpr_doc_parse_model(source, &err);
    cxpr_model_program* program;
    cxpr_context* ctx;
    bool found = false;

    assert(document != NULL);
    assert(err.code == CXPR_OK);
    program = cxpr_compile_model(cxpr_doc_model(document), NULL, &err);
    assert(program != NULL);
    ctx = cxpr_context_new();
    assert(ctx != NULL);
    cxpr_context_set(ctx, "close", 10.0);
    assert(cxpr_eval_model_program(program, ctx, NULL, &err));
    assert(cxpr_context_get(ctx, "top", &found) == 11.0 && found);
    assert(cxpr_context_get(ctx, "bottom", &found) == 9.0 && found);

    cxpr_context_free(ctx);
    cxpr_model_program_free(program);
    cxpr_doc_free(document);
    printf("  ✓ test_document_ast_lowering_handles_record_function_block\n");
}

static void test_document_model_exposes_semantic_source_spans(void) {
    const char* source =
        "project { name = \"dynasty\" }\n"
        "model strategy\n"
        "use indicators/rsi as rsi_lib\n"
        "in { close }\n"
        "$fast = 12 { optimize = [8, 12] }\n"
        "state { signal = 0 }\n"
        "signal := close > $fast\n"
        "out { signal }\n";
    cxpr_error err = {0};
    cxpr_model* direct_model = cxpr_model_parse(source, &err);
    cxpr_doc* document;
    const cxpr_model* model;
    cxpr_source_span span;

    assert(direct_model != NULL);
    assert(cxpr_model_name_source_span(direct_model, &span));
    assert(span.start.line == 2u);
    cxpr_model_free(direct_model);

    document = cxpr_doc_parse_model(source, &err);
    assert(document != NULL);
    assert(err.code == CXPR_OK);
    model = cxpr_doc_model(document);
    assert(model != NULL);

    assert(cxpr_model_host_block_source_span(model, 0u, &span));
    assert(span.start.line == 1u);
    assert(cxpr_model_name_source_span(model, &span));
    assert(span.start.line == 2u);
    assert(cxpr_model_use_source_span(model, 0u, &span));
    assert(span.start.line == 3u);
    assert(cxpr_model_input_source_span(model, 0u, &span));
    assert(span.start.line == 4u);
    assert(cxpr_model_constant_source_span(model, 0u, &span));
    assert(span.start.line == 5u);
    assert(cxpr_model_binding_source_span(model, 0u, &span));
    assert(span.start.line == 6u);
    assert(cxpr_model_binding_source_span(model, 1u, &span));
    assert(span.start.line == 7u);
    assert(cxpr_model_output_source_span(model, 0u, &span));
    assert(span.start.line == 8u);
    assert(cxpr_model_metadata_source_span(model, 0u, &span));
    assert(span.start.line == 5u);

    cxpr_doc_free(document);
    printf("  ✓ test_document_model_exposes_semantic_source_spans\n");
}

static void test_document_ast_function_body_host_fields_and_visitor(void) {
    const char* source =
        "project { name = \"dynasty\", language = \"c\" }\n"
        "model strategy\n"
        "fn impulse(src, base, threshold) {\n"
        "  delta = src - base\n"
        "  magnitude = abs(delta)\n"
        "  return magnitude > threshold\n"
        "}\n"
        "out impulse(close, 10, 2)\n";
    cxpr_error err = {0};
    cxpr_doc_ast* syntax =
        cxpr_doc_ast_parse(source, "fn.cxpr", CXPR_DOC_EXTENSION_MODEL, &err);
    const cxpr_doc_ast_node* root;
    const cxpr_doc_ast_node* host;
    const cxpr_doc_ast_node* function;
    const cxpr_doc_ast_node* body;
    const cxpr_doc_ast_node* ret;
    document_ast_visit_counts counts = {0};

    assert(syntax != NULL);
    assert(err.code == CXPR_OK);
    root = cxpr_doc_ast_root(syntax);
    assert(cxpr_doc_ast_node_child_count(root) == 4u);
    host = cxpr_doc_ast_node_child(root, 0u);
    function = cxpr_doc_ast_node_child(root, 2u);
    assert(cxpr_doc_ast_node_kind(host) == CXPR_DOC_AST_HOST_BLOCK);
    assert(cxpr_doc_ast_node_child_count(host) == 2u);
    assert(strcmp(cxpr_doc_ast_node_name(cxpr_doc_ast_node_child(host, 0u)),
                  "name") == 0);
    assert(strcmp(cxpr_doc_ast_node_text(cxpr_doc_ast_node_child(host, 1u)),
                  "\"c\"") == 0);
    assert(cxpr_doc_ast_node_kind(function) == CXPR_DOC_AST_FUNCTION_DECL);
    assert(cxpr_doc_ast_node_child_count(function) == 1u);
    body = cxpr_doc_ast_node_child(function, 0u);
    assert(cxpr_doc_ast_node_kind(body) == CXPR_DOC_AST_FUNCTION_BODY);
    assert(cxpr_doc_ast_node_child_count(body) == 3u);
    assert(cxpr_doc_ast_node_kind(cxpr_doc_ast_node_child(body, 0u)) ==
           CXPR_DOC_AST_LOCAL_BINDING);
    ret = cxpr_doc_ast_node_child(body, 2u);
    assert(cxpr_doc_ast_node_kind(ret) == CXPR_DOC_AST_RETURN);
    assert(cxpr_doc_ast_node_expr(ret) != NULL);

    assert(cxpr_doc_ast_visit(syntax, count_document_ast_nodes, &counts) ==
           CXPR_VISIT_CONTINUE);
    assert(counts.total >= 9u);
    assert(counts.host_fields == 2u);
    assert(counts.returns == 1u);

    cxpr_doc_ast_free(syntax);
    printf("  ✓ test_document_ast_function_body_host_fields_and_visitor\n");
}

static void test_document_ast_represents_advanced_existing_syntax(void) {
    const char* source =
        "model parent { source_arg = \"source\" }\n"
        "use { atr, ema } from indicators\n"
        "use robotics as r\n"
        "in { high, low, close }\n"
        "fn step(prev, x, period) =\n"
        "  prev + x / max(1, period)\n"
        "out bb(close, 1)\n";
    cxpr_error err = {0};
    cxpr_doc_ast* syntax =
        cxpr_doc_ast_parse(source, "advanced.cxpr", CXPR_DOC_EXTENSION_MODEL, &err);
    const cxpr_doc_ast_node* root;
    const cxpr_doc_ast_node* model_decl;
    const cxpr_doc_ast_node* grouped_use;
    const cxpr_doc_ast_node* aliased_use;
    const cxpr_doc_ast_node* inputs;
    const cxpr_doc_ast_node* function;
    const cxpr_doc_ast_node* output;

    assert(syntax != NULL);
    assert(err.code == CXPR_OK);
    root = cxpr_doc_ast_root(syntax);
    assert(cxpr_doc_ast_node_child_count(root) == 6u);
    model_decl = cxpr_doc_ast_node_child(root, 0u);
    grouped_use = cxpr_doc_ast_node_child(root, 1u);
    aliased_use = cxpr_doc_ast_node_child(root, 2u);
    inputs = cxpr_doc_ast_node_child(root, 3u);
    function = cxpr_doc_ast_node_child(root, 4u);
    output = cxpr_doc_ast_node_child(root, 5u);

    assert(cxpr_doc_ast_node_kind(model_decl) == CXPR_DOC_AST_MODEL_DECL);
    assert(cxpr_doc_ast_node_child_count(model_decl) == 1u);
    assert(cxpr_doc_ast_node_kind(grouped_use) == CXPR_DOC_AST_USE);
    assert(strstr(cxpr_doc_ast_node_text(grouped_use), "from indicators") != NULL);
    assert(cxpr_doc_ast_node_kind(aliased_use) == CXPR_DOC_AST_USE);
    assert(strstr(cxpr_doc_ast_node_text(aliased_use), " as r") != NULL);
    assert(cxpr_doc_ast_node_kind(inputs) == CXPR_DOC_AST_INPUT_BLOCK);
    assert(cxpr_doc_ast_node_child_count(inputs) == 3u);
    assert(cxpr_doc_ast_node_kind(function) == CXPR_DOC_AST_FUNCTION_DECL);
    assert(cxpr_doc_ast_node_expr(function) != NULL);
    assert(cxpr_doc_ast_node_kind(output) == CXPR_DOC_AST_ANONYMOUS_OUTPUT);
    assert(cxpr_doc_ast_node_expr(output) != NULL);

    cxpr_doc_ast_free(syntax);
    printf("  ✓ test_document_ast_represents_advanced_existing_syntax\n");
}

static void test_document_ast_represents_struct_input_blocks(void) {
    const char* source =
        "model struct_input_shape\n"
        "in signal { fills_next_session, fills_session_close }\n"
        "out signal.fills_next_session\n";
    cxpr_error err = {0};
    cxpr_doc_ast* syntax =
        cxpr_doc_ast_parse(source, "struct-inputs.cxpr", CXPR_DOC_EXTENSION_MODEL, &err);
    const cxpr_doc_ast_node* root;
    const cxpr_doc_ast_node* inputs;

    assert(syntax != NULL);
    assert(err.code == CXPR_OK);
    root = cxpr_doc_ast_root(syntax);
    assert(cxpr_doc_ast_node_child_count(root) == 3u);
    inputs = cxpr_doc_ast_node_child(root, 1u);
    assert(cxpr_doc_ast_node_kind(inputs) == CXPR_DOC_AST_INPUT_BLOCK);
    assert(strcmp(cxpr_doc_ast_node_name(inputs), "signal") == 0);
    assert(cxpr_doc_ast_node_child_count(inputs) == 2u);
    assert(strcmp(cxpr_doc_ast_node_name(cxpr_doc_ast_node_child(inputs, 0u)),
                  "fills_next_session") == 0);
    assert(strcmp(cxpr_doc_ast_node_name(cxpr_doc_ast_node_child(inputs, 1u)),
                  "fills_session_close") == 0);

    cxpr_doc_ast_free(syntax);
    printf("  ✓ test_document_ast_represents_struct_input_blocks\n");
}

static void test_load_document_file_is_primary_entrypoint(void) {
    const char* path = "/tmp/cxpr_document_manifest_test.cxpr";
    FILE* file = fopen(path, "wb");
    cxpr_error err = {0};
    cxpr_doc* document;
    const cxpr_model_host_block* tooling;

    assert(file != NULL);
    fputs("tooling { cli = \"dyn_cli\" }\n", file);
    fclose(file);

    document = cxpr_doc_load_manifest(path, &err);
    assert(document != NULL);
    assert(err.code == CXPR_OK);
    tooling = cxpr_doc_host_block(document, "tooling");
    assert(tooling != NULL);
    assert(strcmp(cxpr_host_block_field_value_by_key(tooling, "cli"), "\"dyn_cli\"") == 0);

    cxpr_doc_free(document);
    unlink(path);
    printf("  ✓ test_load_document_file_is_primary_entrypoint\n");
}

static void test_manifest_document_accepts_comment_forms(void) {
    const char* source =
        "/** profile docs */\n"
        "host_profile trading { // host block\n"
        "  type = \"host_profile\" # line metadata\n"
        "  source close { /** nested docs */\n"
        "    type = \"series\" // inline\n"
        "    scoped = true\n"
        "  }\n"
        "  input note {\n"
        "    type = \"string with # and // preserved\"\n"
        "  }\n"
        "}\n";
    cxpr_error err = {0};
    cxpr_doc* document = cxpr_doc_parse_manifest(source, &err);
    const cxpr_model_host_block* profile;
    const cxpr_model_host_block* close_source;
    const cxpr_model_host_block* note_input;

    assert(document != NULL);
    assert(err.code == CXPR_OK);
    assert(cxpr_doc_host_block_count(document) == 1u);
    profile = cxpr_doc_host_block(document, "host_profile");
    assert(profile != NULL);
    assert(strcmp(cxpr_host_block_name(profile), "trading") == 0);
    assert(strcmp(cxpr_host_block_field_value_by_key(profile, "type"),
                  "\"host_profile\"") == 0);
    close_source = cxpr_host_block_child_by_kind(profile, "source");
    assert(close_source != NULL);
    assert(strcmp(cxpr_host_block_name(close_source), "close") == 0);
    assert(strcmp(cxpr_host_block_field_value_by_key(close_source, "type"),
                  "\"series\"") == 0);
    note_input = cxpr_host_block_child_by_kind(profile, "input");
    assert(note_input != NULL);
    assert(strcmp(cxpr_host_block_name(note_input), "note") == 0);
    assert(strcmp(cxpr_host_block_field_value_by_key(note_input, "type"),
                  "\"string with # and // preserved\"") == 0);

    cxpr_doc_free(document);
    printf("  ✓ test_manifest_document_accepts_comment_forms\n");
}

int main(void) {
    printf("Running cxpr document tests...\n");
    test_manifest_document_accepts_host_blocks_without_model();
    test_manifest_document_rejects_model_syntax_without_extension();
    test_document_model_extension_exposes_model_view();
    test_document_input_defaults_lower_to_params();
    test_document_exposes_owned_syntax_tree();
    test_document_ast_exposes_compact_initial_state_update();
    test_parse_document_ast_preserves_block_shapes();
    test_document_ast_lowers_to_independent_document();
    test_document_ast_lowering_equivalent_block_and_shorthand_forms();
    test_document_ast_lowering_rejects_state_output_assignment();
    test_document_ast_lowering_handles_use_aliases_and_groups();
    test_document_ast_lowering_handles_struct_input_blocks();
    test_document_ast_lowering_handles_scalar_function_declaration();
    test_document_ast_lowering_handles_record_function_shorthand();
    test_document_ast_lowering_handles_scalar_function_block();
    test_document_ast_lowering_handles_record_function_block();
    test_document_model_exposes_semantic_source_spans();
    test_document_ast_function_body_host_fields_and_visitor();
    test_document_ast_represents_advanced_existing_syntax();
    test_document_ast_represents_struct_input_blocks();
    test_load_document_file_is_primary_entrypoint();
    test_manifest_document_accepts_comment_forms();
    printf("All document tests passed.\n");
    return 0;
}
