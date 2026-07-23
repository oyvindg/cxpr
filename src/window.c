#include <cxpr/window.h>

#include <string.h>

static const cxpr_window_ir window_ops[] = {
    {CXPR_WINDOW_OP_ROC, "window_roc", 2u, 1u, 1u, CXPR_WINDOW_REDUCE_NONE},
    {CXPR_WINDOW_OP_SUM, "window_sum", 2u, 1u, 0u, CXPR_WINDOW_REDUCE_SUM},
    {CXPR_WINDOW_OP_MEAN, "window_mean", 2u, 1u, 0u, CXPR_WINDOW_REDUCE_MEAN},
    {CXPR_WINDOW_OP_WMA,
     "window_wma", 2u, 1u, 0u, CXPR_WINDOW_REDUCE_WEIGHTED_MEAN},
    {CXPR_WINDOW_OP_STDDEV,
     "window_stddev", 2u, 1u, 0u, CXPR_WINDOW_REDUCE_STDDEV},
    {CXPR_WINDOW_OP_HIGHEST,
     "window_highest", 2u, 1u, 0u, CXPR_WINDOW_REDUCE_HIGHEST},
    {CXPR_WINDOW_OP_LOWEST,
     "window_lowest", 2u, 1u, 0u, CXPR_WINDOW_REDUCE_LOWEST},
    {CXPR_WINDOW_OP_BARS_SINCE_EXTREME,
     "bars_since_extreme", 3u, 1u, 0u, CXPR_WINDOW_REDUCE_NONE},
    {CXPR_WINDOW_OP_MEAN_ABSDEV,
     "window_mean_absdev", 3u, 1u, 0u, CXPR_WINDOW_REDUCE_NONE},
};

const cxpr_window_ir* cxpr_window_ir_find(const char* name) {
    if (!name) return NULL;
    for (size_t i = 0u; i < cxpr_window_ir_count(); ++i) {
        if (strcmp(window_ops[i].name, name) == 0) return &window_ops[i];
    }
    return NULL;
}

const cxpr_window_ir* cxpr_window_ir_at(size_t index) {
    return index < cxpr_window_ir_count() ? &window_ops[index] : NULL;
}

size_t cxpr_window_ir_count(void) {
    return sizeof(window_ops) / sizeof(window_ops[0]);
}
