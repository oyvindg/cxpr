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
