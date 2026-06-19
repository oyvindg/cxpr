/**
 * @file value_ops.c
 * @brief Typed timestamp/duration value algebra shared by both evaluators.
 */

#include "core.h"

#include <math.h>

static int cxpr_value_op_type_error(cxpr_error* err, const char* message) {
    if (err) {
        err->code = CXPR_ERR_TYPE_MISMATCH;
        err->message = message;
    }
    return -1;
}

static int cxpr_value_op_div_zero(cxpr_error* err) {
    if (err) {
        err->code = CXPR_ERR_DIVISION_BY_ZERO;
        err->message = "Division by zero";
    }
    return -1;
}

int cxpr_value_binary_op(cxpr_valop op, cxpr_value a, cxpr_value b,
                         cxpr_value* out, cxpr_error* err) {
    const bool a_temporal =
        a.type == CXPR_VALUE_TIMESTAMP || a.type == CXPR_VALUE_DURATION;
    const bool b_temporal =
        b.type == CXPR_VALUE_TIMESTAMP || b.type == CXPR_VALUE_DURATION;

    if (!out) return cxpr_value_op_type_error(err, "Invalid result destination");

    /* Pure numeric operands are left to the caller's own fast numeric path. */
    if (!a_temporal && !b_temporal) return 0;

    switch (op) {
    case CXPR_VALOP_ADD:
        if (a.type == CXPR_VALUE_TIMESTAMP && b.type == CXPR_VALUE_DURATION) {
            *out = cxpr_timestamp(a.i64 + b.i64);
            return 1;
        }
        if (a.type == CXPR_VALUE_DURATION && b.type == CXPR_VALUE_TIMESTAMP) {
            *out = cxpr_timestamp(a.i64 + b.i64);
            return 1;
        }
        if (a.type == CXPR_VALUE_DURATION && b.type == CXPR_VALUE_DURATION) {
            *out = cxpr_duration(a.i64 + b.i64);
            return 1;
        }
        return cxpr_value_op_type_error(
            err, "'+' on temporal values requires timestamp+duration or duration+duration");

    case CXPR_VALOP_SUB:
        if (a.type == CXPR_VALUE_TIMESTAMP && b.type == CXPR_VALUE_TIMESTAMP) {
            *out = cxpr_duration(a.i64 - b.i64);
            return 1;
        }
        if (a.type == CXPR_VALUE_TIMESTAMP && b.type == CXPR_VALUE_DURATION) {
            *out = cxpr_timestamp(a.i64 - b.i64);
            return 1;
        }
        if (a.type == CXPR_VALUE_DURATION && b.type == CXPR_VALUE_DURATION) {
            *out = cxpr_duration(a.i64 - b.i64);
            return 1;
        }
        return cxpr_value_op_type_error(
            err, "'-' on temporal values requires timestamp-timestamp, "
                 "timestamp-duration, or duration-duration");

    case CXPR_VALOP_MUL:
        if (a.type == CXPR_VALUE_DURATION && b.type == CXPR_VALUE_NUMBER) {
            *out = cxpr_duration((int64_t)llround((double)a.i64 * b.d));
            return 1;
        }
        if (a.type == CXPR_VALUE_NUMBER && b.type == CXPR_VALUE_DURATION) {
            *out = cxpr_duration((int64_t)llround(a.d * (double)b.i64));
            return 1;
        }
        return cxpr_value_op_type_error(
            err, "'*' on temporal values requires duration*number");

    case CXPR_VALOP_DIV:
        if (a.type == CXPR_VALUE_DURATION && b.type == CXPR_VALUE_NUMBER) {
            if (b.d == 0.0) return cxpr_value_op_div_zero(err);
            *out = cxpr_duration((int64_t)llround((double)a.i64 / b.d));
            return 1;
        }
        if (a.type == CXPR_VALUE_DURATION && b.type == CXPR_VALUE_DURATION) {
            if (b.i64 == 0) return cxpr_value_op_div_zero(err);
            *out = cxpr_num((double)a.i64 / (double)b.i64);
            return 1;
        }
        return cxpr_value_op_type_error(
            err, "'/' on temporal values requires duration/number or duration/duration");

    case CXPR_VALOP_LT:
    case CXPR_VALOP_LTE:
    case CXPR_VALOP_GT:
    case CXPR_VALOP_GTE:
        if (a.type != b.type ||
            (a.type != CXPR_VALUE_TIMESTAMP && a.type != CXPR_VALUE_DURATION)) {
            return cxpr_value_op_type_error(
                err, "Comparison requires two timestamps or two durations");
        }
        switch (op) {
        case CXPR_VALOP_LT: *out = cxpr_bool(a.i64 < b.i64); break;
        case CXPR_VALOP_LTE: *out = cxpr_bool(a.i64 <= b.i64); break;
        case CXPR_VALOP_GT: *out = cxpr_bool(a.i64 > b.i64); break;
        default: *out = cxpr_bool(a.i64 >= b.i64); break;
        }
        return 1;
    }

    return cxpr_value_op_type_error(err, "Unsupported temporal operator");
}
