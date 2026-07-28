/**
 * @file window.h
 * @brief Window-operation metadata used by cxpr analysis and code generation.
 */

#ifndef CXPR_WINDOW_H
#define CXPR_WINDOW_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Supported built-in operations over a trailing value window. */
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

/** @brief Reduction implemented by a window operation, when applicable. */
typedef enum {
    CXPR_WINDOW_REDUCE_NONE = -1,
    CXPR_WINDOW_REDUCE_SUM = 0,
    CXPR_WINDOW_REDUCE_MEAN = 1,
    CXPR_WINDOW_REDUCE_HIGHEST = 2,
    CXPR_WINDOW_REDUCE_LOWEST = 3,
    CXPR_WINDOW_REDUCE_STDDEV = 4,
    CXPR_WINDOW_REDUCE_WEIGHTED_MEAN = 5
} cxpr_window_reduce;

/** @brief Static intermediate-representation metadata for a window operation. */
typedef struct {
    cxpr_window_op op;             /**< Operation identifier. */
    const char* name;              /**< Expression function name. */
    size_t arity;                  /**< Required argument count. */
    size_t period_argument;        /**< Index of the period argument. */
    size_t history_tail;           /**< Additional history samples required. */
    cxpr_window_reduce reduction;  /**< Associated reduction, or none. */
} cxpr_window_ir;

/**
 * @brief Find window metadata by expression function name.
 * @param name Function name to look up.
 * @return Static metadata, or NULL when @p name is not a window operation.
 */
const cxpr_window_ir* cxpr_window_ir_find(const char* name);

/**
 * @brief Return window metadata by registry index.
 * @param index Zero-based registry index.
 * @return Static metadata, or NULL when @p index is out of range.
 */
const cxpr_window_ir* cxpr_window_ir_at(size_t index);

/** @brief Return the number of registered window operations. */
size_t cxpr_window_ir_count(void);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_WINDOW_H */
