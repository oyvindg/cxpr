#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_analysis_reports_core_flags(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_analysis analysis = {0};
    cxpr_error err = {0};

    assert(reg);
    cxpr_register_defaults(reg);

    assert(cxpr_analyze_expr("close > $threshold ? sqrt(close) : 0", reg, &analysis, &err));
    assert(err.code == CXPR_OK);
    assert(analysis.result_type == CXPR_EXPR_NUMBER);
    assert(analysis.is_constant == false);
    assert(analysis.is_predicate == false);
    assert(analysis.uses_variables == true);
    assert(analysis.uses_parameters == true);
    assert(analysis.uses_functions == true);
    assert(analysis.can_short_circuit == true);
    assert(analysis.node_count > 1);
    assert(analysis.max_depth >= 2);
    assert(analysis.max_lookback_depth == 0);
    assert(analysis.reference_count >= 1);
    assert(analysis.function_count == 1);
    assert(analysis.parameter_count == 1);
    assert(analysis.has_unknown_functions == false);
    assert(analysis.has_unsupported_codegen_nodes == false);
    assert(analysis.first_unsupported_codegen_node == NULL);

    cxpr_registry_free(reg);
}

static void test_analysis_reports_literal_lookback_depth(void) {
    cxpr_analysis analysis = {0};
    cxpr_error err = {0};

    assert(cxpr_analyze_expr("close[2][3] > low[1]", NULL, &analysis, &err));
    assert(err.code == CXPR_OK);
    assert(analysis.max_lookback_depth == 5);
    assert(analysis.has_unsupported_codegen_nodes == false);
    assert(analysis.first_unsupported_codegen_node == NULL);
}

static void test_analysis_reports_codegen_unsupported_nodes_without_flagging_literal_lookback(void) {
    cxpr_analysis analysis = {0};
    cxpr_error err = {0};

    assert(cxpr_analyze_expr("close[$n]", NULL, &analysis, &err));
    assert(err.code == CXPR_OK);
    assert(analysis.has_unsupported_codegen_nodes == true);
    assert(strcmp(analysis.first_unsupported_codegen_node, "lookback_index") == 0);

    analysis = (cxpr_analysis){0};
    err = (cxpr_error){0};
    assert(cxpr_analyze_expr("macd.signal > 0", NULL, &analysis, &err));
    assert(err.code == CXPR_OK);
    assert(analysis.has_unsupported_codegen_nodes == true);
    assert(strcmp(analysis.first_unsupported_codegen_node, "field_access") == 0);
}

int main(void) {
    test_analysis_reports_core_flags();
    test_analysis_reports_literal_lookback_depth();
    test_analysis_reports_codegen_unsupported_nodes_without_flagging_literal_lookback();
    printf("  \xE2\x9C\x93 ast_analyze\n");
    return 0;
}
