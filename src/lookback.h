/**
 * @file lookback.h
 * @brief Internal helpers for lookback offset validation.
 */

#ifndef CXPR_LOOKBACK_H
#define CXPR_LOOKBACK_H

#include <cxpr/expr/ast.h>

bool cxpr_lookback_literal_offset(const cxpr_expr_ast* index_ast,
                                  unsigned* out_offset,
                                  cxpr_error* err,
                                  const char* context);
bool cxpr_lookback_add_unsigned(unsigned base,
                                unsigned offset,
                                unsigned* out_sum,
                                cxpr_error* err,
                                const char* context);
bool cxpr_lookback_add_int(int base,
                           unsigned offset,
                           int* out_sum,
                           cxpr_error* err,
                           const char* context);

#endif /* CXPR_LOOKBACK_H */
