/**
 * @file value_ops.c
 * @brief Typed timestamp/duration value algebra shared by both evaluators.
 */

#include "core.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

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

static const cxpr_value* cxpr_struct_find_field(const cxpr_struct_value* s,
                                                const char* name) {
    if (!s || !name) return NULL;
    for (size_t i = 0u; i < s->field_count; ++i) {
        if (strcmp(s->field_names[i], name) == 0) return &s->field_values[i];
    }
    return NULL;
}

static int cxpr_value_binary_op_impl(cxpr_valop op, cxpr_value a, cxpr_value b,
                                     cxpr_value* out, cxpr_error* err);

static int cxpr_value_numeric_binary_op(cxpr_valop op, double a, double b,
                                        cxpr_value* out, cxpr_error* err) {
    switch (op) {
    case CXPR_VALOP_ADD: *out = cxpr_num(a + b); return 1;
    case CXPR_VALOP_SUB: *out = cxpr_num(a - b); return 1;
    case CXPR_VALOP_MUL: *out = cxpr_num(a * b); return 1;
    case CXPR_VALOP_DIV:
        if (b == 0.0) return cxpr_value_op_div_zero(err);
        *out = cxpr_num(a / b);
        return 1;
    case CXPR_VALOP_LT: *out = cxpr_bool(a < b); return 1;
    case CXPR_VALOP_LTE: *out = cxpr_bool(a <= b); return 1;
    case CXPR_VALOP_GT: *out = cxpr_bool(a > b); return 1;
    case CXPR_VALOP_GTE: *out = cxpr_bool(a >= b); return 1;
    }
    return cxpr_value_op_type_error(err, "Unsupported operator");
}

static bool cxpr_value_is_struct_scalar_operand(cxpr_value value) {
    return value.type == CXPR_VALUE_NUMBER ||
           value.type == CXPR_VALUE_TIMESTAMP ||
           value.type == CXPR_VALUE_DURATION;
}

static int cxpr_value_struct_binary_op(cxpr_valop op, cxpr_value a, cxpr_value b,
                                       cxpr_value* out, cxpr_error* err) {
    const cxpr_struct_value* left = a.type == CXPR_VALUE_STRUCT ? a.s : NULL;
    const cxpr_struct_value* right = b.type == CXPR_VALUE_STRUCT ? b.s : NULL;
    const cxpr_struct_value* shape = left ? left : right;
    cxpr_value* values;
    cxpr_struct_value* result;

    if (!shape) return 0;
    if (op == CXPR_VALOP_LT || op == CXPR_VALOP_LTE ||
        op == CXPR_VALOP_GT || op == CXPR_VALOP_GTE) {
        return cxpr_value_op_type_error(err, "Struct comparison is not supported");
    }
    if (!left && !cxpr_value_is_struct_scalar_operand(a)) {
        return cxpr_value_op_type_error(err,
                                        "Struct arithmetic requires scalar-compatible operands");
    }
    if (!right && !cxpr_value_is_struct_scalar_operand(b)) {
        return cxpr_value_op_type_error(err,
                                        "Struct arithmetic requires scalar-compatible operands");
    }

    values = (cxpr_value*)calloc(shape->field_count ? shape->field_count : 1u,
                                 sizeof(cxpr_value));
    if (!values) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return -1;
    }

    for (size_t i = 0u; i < shape->field_count; ++i) {
        const char* field_name = shape->field_names[i];
        const cxpr_value* av = left ? &left->field_values[i] : &a;
        const cxpr_value* bv = NULL;
        int handled;

        if (right) {
            bv = cxpr_struct_find_field(right, field_name);
            if (!bv) {
                for (size_t j = 0u; j < i; ++j) cxpr_value_free(&values[j]);
                free(values);
                return cxpr_value_op_type_error(
                    err, "Struct arithmetic requires matching named fields");
            }
        } else {
            bv = &b;
        }

        handled = cxpr_value_binary_op_impl(op, *av, *bv, &values[i], err);
        if (handled != 1) {
            for (size_t j = 0u; j < i; ++j) cxpr_value_free(&values[j]);
            free(values);
            if (handled == 0) {
                return cxpr_value_op_type_error(
                    err, "Struct arithmetic requires numeric or compatible fields");
            }
            return -1;
        }
    }

    if (right && right->field_count != shape->field_count) {
        for (size_t i = 0u; i < shape->field_count; ++i) cxpr_value_free(&values[i]);
        free(values);
        return cxpr_value_op_type_error(err,
                                        "Struct arithmetic requires matching named fields");
    }

    result = cxpr_struct_value_new((const char* const*)shape->field_names,
                                   values,
                                   shape->field_count);
    for (size_t i = 0u; i < shape->field_count; ++i) cxpr_value_free(&values[i]);
    free(values);
    if (!result) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return -1;
    }

    *out = cxpr_struct(result);
    return 1;
}

static int cxpr_value_binary_op_impl(cxpr_valop op, cxpr_value a, cxpr_value b,
                                     cxpr_value* out, cxpr_error* err) {
    if (a.type == CXPR_VALUE_STRUCT || b.type == CXPR_VALUE_STRUCT) {
        return cxpr_value_struct_binary_op(op, a, b, out, err);
    }

    if (a.type == CXPR_VALUE_NUMBER && b.type == CXPR_VALUE_NUMBER) {
        return cxpr_value_numeric_binary_op(op, a.d, b.d, out, err);
    }

    if (a.type == CXPR_VALUE_TIMESTAMP || a.type == CXPR_VALUE_DURATION ||
        b.type == CXPR_VALUE_TIMESTAMP || b.type == CXPR_VALUE_DURATION) {
        return cxpr_value_binary_op(op, a, b, out, err);
    }

    return 0;
}

int cxpr_value_binary_op(cxpr_valop op, cxpr_value a, cxpr_value b,
                         cxpr_value* out, cxpr_error* err) {
    const bool a_temporal =
        a.type == CXPR_VALUE_TIMESTAMP || a.type == CXPR_VALUE_DURATION;
    const bool b_temporal =
        b.type == CXPR_VALUE_TIMESTAMP || b.type == CXPR_VALUE_DURATION;
    const bool a_struct = a.type == CXPR_VALUE_STRUCT;
    const bool b_struct = b.type == CXPR_VALUE_STRUCT;

    if (!out) return cxpr_value_op_type_error(err, "Invalid result destination");

    /* Pure numeric operands are left to the caller's own fast numeric path. */
    if (!a_temporal && !b_temporal && !a_struct && !b_struct) return 0;

    if (a_struct || b_struct) {
        return cxpr_value_struct_binary_op(op, a, b, out, err);
    }

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
