#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "generated_resample_function.h"

int main(void) {
    const double hourly[] = {100.0, 104.0, 103.0};
    const size_t alignment[] = {0u, 0u, 1u, 1u, 2u};
    const double five_minute[] = {1.0, 2.0, 3.0, 4.0, 5.0};
    const size_t five_alignment[] = {0u, 1u, 2u, 3u, 4u};
    const size_t gap_alignment[] = {0u, 0u, 1u, 1u, (size_t)-1};
    cxpr_resample_view views[2] = {
        {hourly, alignment, 3u, 5u},
        {five_minute, five_alignment, 5u, 5u},
    };
    generated_resample_tick_state state;
    double inputs[] = {999.0};
    double outputs[] = {NAN};
    memset(&state, 0, sizeof(state));
    /* First aligned bucket has no target-series [1] history. */
    generated_resample_tick(&state, inputs, NULL, outputs, views, 0u);
    assert(isnan(outputs[0]));
    generated_resample_tick(&state, inputs, NULL, outputs, views, 1u);
    assert(isnan(outputs[0]));
    /* Cursor 2 is the first primary row aligned to the next hourly bucket. */
    generated_resample_tick(&state, inputs, NULL, outputs, views, 2u);
    assert(outputs[0] == 411.0);
    generated_resample_tick(&state, inputs, NULL, outputs, views, 3u);
    assert(outputs[0] == 412.0);
    generated_resample_tick(&state, inputs, NULL, outputs, views, 4u);
    assert(outputs[0] == 419.0);
    assert(cxpr_resample_view_validate(&views[0]) == CXPR_RESAMPLE_VIEW_OK);
    views[0].values = NULL;
    assert(cxpr_resample_view_validate(&views[0]) ==
           CXPR_RESAMPLE_VIEW_VALUES_REQUIRED);
    assert(strstr(cxpr_resample_view_status_message(
        CXPR_RESAMPLE_VIEW_VALUES_REQUIRED), "values buffer") != NULL);
    generated_resample_tick(&state, inputs, NULL, outputs, views, 4u);
    assert(isnan(outputs[0]));
    views[0].values = hourly;
    views[0].alignment = NULL;
    assert(cxpr_resample_view_validate(&views[0]) ==
           CXPR_RESAMPLE_VIEW_ALIGNMENT_REQUIRED);
    views[0].alignment = NULL;
    generated_resample_tick(&state, inputs, NULL, outputs, views, 4u);
    assert(isnan(outputs[0]));
    views[0].alignment = gap_alignment;
    generated_resample_tick(&state, inputs, NULL, outputs, views, 4u);
    assert(isnan(outputs[0]));
    puts("generated resample C current/[1] CSE parity OK");
    return 0;
}
