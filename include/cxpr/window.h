#ifndef CXPR_WINDOW_H
#define CXPR_WINDOW_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CXPR_WINDOW_OP_NONE = 0,
    CXPR_WINDOW_OP_ROC,
    CXPR_WINDOW_OP_SUM,
    CXPR_WINDOW_OP_MEAN,
    CXPR_WINDOW_OP_WMA,
    CXPR_WINDOW_OP_STDDEV,
    CXPR_WINDOW_OP_HIGHEST,
    CXPR_WINDOW_OP_LOWEST,
    CXPR_WINDOW_OP_BARS_SINCE_EXTREME,
    CXPR_WINDOW_OP_MEAN_ABSDEV
} cxpr_window_op;

typedef enum {
    CXPR_WINDOW_REDUCE_NONE = -1,
    CXPR_WINDOW_REDUCE_SUM = 0,
    CXPR_WINDOW_REDUCE_MEAN = 1,
    CXPR_WINDOW_REDUCE_HIGHEST = 2,
    CXPR_WINDOW_REDUCE_LOWEST = 3,
    CXPR_WINDOW_REDUCE_STDDEV = 4,
    CXPR_WINDOW_REDUCE_WEIGHTED_MEAN = 5
} cxpr_window_reduce;

typedef struct {
    cxpr_window_op op;
    const char* name;
    size_t arity;
    size_t period_argument;
    size_t history_tail;
    cxpr_window_reduce reduction;
} cxpr_window_ir;

const cxpr_window_ir* cxpr_window_ir_find(const char* name);
const cxpr_window_ir* cxpr_window_ir_at(size_t index);
size_t cxpr_window_ir_count(void);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_WINDOW_H */
