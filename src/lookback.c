/**
 * @file lookback.c
 * @brief Shared lookback offset validation.
 */

#include "lookback.h"

#include <math.h>

static bool cxpr_lookback_err(cxpr_error* err, const char* message) {
    if (err) {
        err->code = CXPR_ERR_SYNTAX;
        err->message = message;
    }
    return false;
}

bool cxpr_lookback_literal_offset(const cxpr_expr_ast* index_ast,
                                  unsigned* out_offset,
                                  cxpr_error* err,
                                  const char* context) {
    double raw;
    unsigned offset;

    if (out_offset) *out_offset = 0u;
    if (!index_ast || cxpr_expr_ast_kind_of(index_ast) != CXPR_NODE_NUMBER) {
        return cxpr_lookback_err(
            err,
            context ? context : "lookback requires constant index");
    }
    raw = cxpr_expr_ast_number_value(index_ast);
    offset = raw >= 0.0 ? (unsigned)(raw + 0.5) : 0u;
    if (!isfinite(raw) || raw < 0.0 || fabs(raw - (double)offset) > 1e-9) {
        return cxpr_lookback_err(
            err,
            context ? context : "lookback requires non-negative integer index");
    }
    if (out_offset) *out_offset = offset;
    return true;
}

bool cxpr_lookback_add_unsigned(unsigned base,
                                unsigned offset,
                                unsigned* out_sum,
                                cxpr_error* err,
                                const char* context) {
    const unsigned max_unsigned = ~0u;

    if (out_sum) *out_sum = base;
    if (max_unsigned - base < offset) {
        return cxpr_lookback_err(
            err,
            context ? context : "lookback offset overflow");
    }
    if (out_sum) *out_sum = base + offset;
    return true;
}

bool cxpr_lookback_add_int(int base,
                           unsigned offset,
                           int* out_sum,
                           cxpr_error* err,
                           const char* context) {
    const unsigned max_int = ((unsigned)~0u) >> 1u;

    if (out_sum) *out_sum = base;
    if (base < 0 || (unsigned)base > max_int || max_int - (unsigned)base < offset) {
        return cxpr_lookback_err(
            err,
            context ? context : "lookback offset overflow");
    }
    if (out_sum) *out_sum = base + (int)offset;
    return true;
}
