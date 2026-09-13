/**
 * @file source_location.h
 * @brief Source positions shared by expression and document AST APIs.
 */

#pragma once

#include <stddef.h>

/** Offsets and columns are zero-based; lines are one-based. */
typedef struct {
    size_t offset;
    size_t line;
    size_t column;
} cxpr_source_pos;

/** Half-open source span; end points one byte past the represented range. */
typedef struct {
    cxpr_source_pos start;
    cxpr_source_pos end;
} cxpr_source_span;
