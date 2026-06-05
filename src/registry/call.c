/**
 * @file call.c
 * @brief Registry invocation helpers.
 */

#include "internal.h"
#include "limits.h"

#include <math.h>
#include <stdio.h>

static const char* cxpr_registry_unknown_function_message(const char* name) {
    static char message[256];
    if (!name || name[0] == '\0') return "Unknown function";
    snprintf(message, sizeof(message), "Unknown function '%s'", name);
    return message;
}

cxpr_value cxpr_registry_call_typed(const cxpr_registry* reg, const char* name,
                                    const cxpr_value* args, size_t argc, cxpr_error* err) {
    cxpr_func_entry* entry = cxpr_registry_find(reg, name);
    double scalar_args[CXPR_MAX_CALL_ARGS];

    if (!entry || (!entry->sync_func && !entry->value_func && !entry->typed_func)) {
        if (err) {
            err->code = CXPR_ERR_UNKNOWN_FUNCTION;
            err->message = cxpr_registry_unknown_function_message(name);
            err->position = 0;
            err->line = 0;
            err->column = 0;
        }
        return cxpr_num(NAN);
    }

    if (argc < entry->min_args || argc > entry->max_args) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Wrong number of arguments";
            err->position = 0;
            err->line = 0;
            err->column = 0;
        }
        return cxpr_num(NAN);
    }

    if (entry->arg_types) {
        for (size_t i = 0; i < argc && i < entry->arg_type_count; ++i) {
            if (args[i].type != entry->arg_types[i]) {
                if (err) {
                    err->code = CXPR_ERR_TYPE_MISMATCH;
                    err->message = "Function argument type mismatch";
                    err->position = 0;
                    err->line = 0;
                    err->column = 0;
                }
                return cxpr_num(NAN);
            }
        }
    }

    if (entry->typed_func) {
        if (err) *err = (cxpr_error){0};
        return entry->typed_func(args, argc, entry->userdata);
    }

    if (entry->value_func) {
        if (err) *err = (cxpr_error){0};
        return entry->value_func(args, argc, entry->userdata);
    }

    if (argc > CXPR_MAX_CALL_ARGS) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Too many function arguments";
            err->position = 0;
            err->line = 0;
            err->column = 0;
        }
        return cxpr_num(NAN);
    }

    for (size_t i = 0; i < argc; ++i) {
        if (args[i].type != CXPR_VALUE_NUMBER) {
            if (err) {
                err->code = CXPR_ERR_TYPE_MISMATCH;
                err->message = "Function arguments must be numbers";
                err->position = 0;
                err->line = 0;
                err->column = 0;
            }
            return cxpr_num(NAN);
        }
        scalar_args[i] = args[i].d;
    }

    if (err) *err = (cxpr_error){0};
    return cxpr_num(entry->sync_func(scalar_args, argc, entry->userdata));
}

cxpr_value cxpr_registry_call_value(const cxpr_registry* reg, const char* name,
                                    const double* args, size_t argc, cxpr_error* err) {
    cxpr_value typed_args[CXPR_MAX_CALL_ARGS];
    if (argc > CXPR_MAX_CALL_ARGS) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Too many function arguments";
            err->position = 0;
            err->line = 0;
            err->column = 0;
        }
        return cxpr_num(NAN);
    }
    for (size_t i = 0; i < argc; ++i) {
        typed_args[i] = cxpr_num(args[i]);
    }
    return cxpr_registry_call_typed(reg, name, typed_args, argc, err);
}

double cxpr_registry_call(const cxpr_registry* reg, const char* name,
                          const double* args, size_t argc, cxpr_error* err) {
    cxpr_value value = cxpr_registry_call_value(reg, name, args, argc, err);
    if (err && err->code != CXPR_OK) return NAN;
    if (value.type == CXPR_VALUE_NUMBER) return value.d;
    if (value.type == CXPR_VALUE_BOOL) return value.b ? 1.0 : 0.0;
    if (err) {
        err->code = CXPR_ERR_TYPE_MISMATCH;
        err->message = "Function did not evaluate to scalar";
        err->position = 0;
        err->line = 0;
        err->column = 0;
    }
    return NAN;
}
