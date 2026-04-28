/**
 * @file base.c
 * @brief Core evaluator helper primitives.
 */

#include "internal.h" // IWYU pragma: keep

cxpr_value cxpr_eval_error(cxpr_error* err, cxpr_error_code code, const char* message) {
    if (err) {
        err->code = code;
        err->message = message;
    }
    return cxpr_fv_double(NAN);
}

bool cxpr_require_type(cxpr_value value, cxpr_value_type type,
                       cxpr_error* err, const char* message) {
    if (value.type != type) {
        cxpr_eval_error(err, CXPR_ERR_TYPE_MISMATCH, message);
        return false;
    }
    return true;
}

cxpr_value cxpr_struct_get_field(const cxpr_struct_value* value, const char* field, bool* found) {
    if (found) *found = false;
    if (!value || !field) return cxpr_fv_double(NAN);

    for (size_t i = 0; i < value->field_count; ++i) {
        if (strcmp(value->field_names[i], field) == 0) {
            if (found) *found = true;
            return value->field_values[i];
        }
    }

    return cxpr_fv_double(NAN);
}

cxpr_value cxpr_struct_get_field_by_index(const cxpr_struct_value* value, size_t index,
                                          bool* found) {
    if (found) *found = false;
    if (!value || index >= value->field_count) return cxpr_fv_double(NAN);
    if (found) *found = true;
    return value->field_values[index];
}
