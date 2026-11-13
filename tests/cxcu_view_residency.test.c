#include <cxpr/backends/cxcu.h>

#include <assert.h>
#include <stdio.h>

int main(void) {
    const double values[6] = {1, 2, 3, 4, 5, 6};
    const cxpr_view_resolved resolved = {
        .data = values,
        .element_type = CXPR_VALUE_NUMBER,
        .rank = 2u,
        .shape = {2u, 3u},
        .element_strides = {3u, 1u},
    };
    cxpr_view_table table = {.views = &resolved, .count = 1u, .generation = 11u};
    cxpr_cxcu_view_plan plan;
    cxpr_error error = {0};
    cxcu_error availability = {0};
    cxpr_cxcu_view_plan_init(&plan, 0);
    if (!cxcu_available(&availability)) {
        assert(!cxpr_cxcu_view_plan_bind(&plan, &table, &error));
        assert(error.code == CXPR_ERR_UNAVAILABLE);
        printf("cxpr cxcu device view residency skipped: %s\n", availability.message);
        return 77;
    }
    assert(cxpr_cxcu_view_plan_bind(&plan, &table, &error));
    assert(plan.count == 1u && plan.generation == 11u);
    assert(plan.descriptors[0].rank == 2u);
    assert(plan.residency.upload_count == 1u);
    assert(cxpr_cxcu_view_plan_bind(&plan, &table, &error));
    assert(plan.residency.reuse_count == 1u);
    table.generation = 12u;
    assert(cxpr_cxcu_view_plan_bind(&plan, &table, &error));
    assert(plan.residency.upload_count == 2u);
    cxpr_cxcu_view_plan_free(&plan);
    cxcu_shutdown();
    puts("cxpr cxcu device view residency: OK");
    return 0;
}
