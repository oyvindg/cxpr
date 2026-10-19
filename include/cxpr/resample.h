/** @file resample.h Build-time temporal series selection helpers. */
#ifndef CXPR_RESAMPLE_H
#define CXPR_RESAMPLE_H
#include <cxpr/expr/ast.h>
#include <cxpr/types.h>
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CXPR_RESAMPLE_CANONICAL_CAPACITY 32u
typedef struct cxpr_resample_interval {
    int64_t duration_ns;
    char canonical[CXPR_RESAMPLE_CANONICAL_CAPACITY];
} cxpr_resample_interval;
typedef struct cxpr_resample_call {
    const cxpr_expr_ast* source;
    cxpr_resample_interval every;
} cxpr_resample_call;
/** Standard bucket aggregate typing contract for hosts materializing views. */
typedef enum cxpr_resample_aggregate {
    CXPR_RESAMPLE_AGG_FIRST = 0,
    CXPR_RESAMPLE_AGG_LAST,
    CXPR_RESAMPLE_AGG_MIN,
    CXPR_RESAMPLE_AGG_MAX,
    CXPR_RESAMPLE_AGG_SUM,
    CXPR_RESAMPLE_AGG_COUNT,
    CXPR_RESAMPLE_AGG_MEAN
} cxpr_resample_aggregate;
/** FIRST/LAST/MIN/MAX/SUM preserve scalar type; COUNT is int64; MEAN is number. */
cxpr_value_type cxpr_resample_aggregate_result_type(
    cxpr_value_type input_type, cxpr_resample_aggregate aggregate);
bool cxpr_parse_fixed_duration(const char* text, cxpr_resample_interval* out,
                               cxpr_error* err);
bool cxpr_resample_call_parse(const cxpr_expr_ast* ast, cxpr_resample_call* out,
                              cxpr_error* err);
/** Validate every resample call contained in an expression tree. */
bool cxpr_resample_validate_ast(const cxpr_expr_ast* ast, cxpr_error* err);
#ifdef __cplusplus
}
#endif
#endif
