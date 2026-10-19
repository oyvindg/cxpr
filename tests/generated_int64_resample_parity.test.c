#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "generated_int64_resample_function.h"

int main(void) {
    static const int64_t ids[] = {
        INT64_C(9007199254740993),
        INT64_C(9007199254740995),
        INT64_C(9007199254740997),
    };
    static const size_t alignment[] = {0u, 1u, 2u};
    const cxpr_typed_resample_view views[] = {{
        .int64_values = ids,
        .alignment = alignment,
        .value_count = 3u,
        .primary_count = 3u,
        .value_type = CXPR_VALUE_INT64,
    }};
    generated_int64_resample_tick_state state;
    cxpr_value inputs[] = {cxpr_int64(INT64_C(0))};
    cxpr_value outputs[] = {cxpr_null()};
    memset(&state, 0, sizeof(state));
    assert(cxpr_typed_resample_view_valid(&views[0]));
    generated_int64_resample_tick(&state, inputs, NULL, outputs, views, 2u);
    assert(outputs[0].type == CXPR_VALUE_INT64);
    assert(outputs[0].i64 == INT64_C(18014398509481992));
    puts("generated int64 resample exact parity OK");
    return 0;
}
