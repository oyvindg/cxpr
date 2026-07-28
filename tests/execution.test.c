#include <assert.h>
#include <stdio.h>

#include <cxpr/execution.h>

int main(void) {
    const cxpr_execution_policy generated = {CXPR_EXECUTION_GENERATED_C};
    const cxpr_execution_policy analysis = {CXPR_EXECUTION_AST_ANALYSIS};
    const cxpr_execution_policy reference = {CXPR_EXECUTION_IR_REFERENCE};
    const cxpr_execution_policy invalid = {(cxpr_execution_mode)0};
    cxpr_execution_report report = {3u, 0u, 0u, 0u};

    assert(cxpr_execution_policy_valid(&generated));
    assert(cxpr_execution_policy_valid(&analysis));
    assert(cxpr_execution_policy_valid(&reference));
    assert(!cxpr_execution_policy_valid(&invalid));
    assert(!cxpr_execution_policy_valid(NULL));

    assert(!cxpr_execution_policy_allows_runtime_compile(&generated));
    assert(cxpr_execution_policy_allows_runtime_compile(&analysis));
    assert(cxpr_execution_policy_allows_runtime_compile(&reference));

    assert(cxpr_execution_report_is_generated_only(&report));
    report.runtime_compiles = 1u;
    assert(!cxpr_execution_report_is_generated_only(&report));
    report.runtime_compiles = 0u;
    report.ast_analysis_calls = 1u;
    assert(!cxpr_execution_report_is_generated_only(&report));

    puts("execution policy tests passed");
    return 0;
}
