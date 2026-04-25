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
    assert(analysis.reference_count >= 1);
    assert(analysis.function_count == 1);
    assert(analysis.parameter_count == 1);
    assert(analysis.has_unknown_functions == false);

    cxpr_registry_free(reg);
}

int main(void) {
    test_analysis_reports_core_flags();
    printf("  \xE2\x9C\x93 ast_analyze\n");
    return 0;
}
