/**
 * @file args.c
 * @brief Internal helpers for validating and ordering call arguments.
 */

#include "args.h"
#include "../ast/internal.h"
#include "../limits.h"
#include <string.h>

static void cxpr_call_bind_set_error(cxpr_error_code* out_code,
                                     const char** out_message,
                                     cxpr_error_code code,
                                     const char* message) {
    if (out_code) *out_code = code;
    if (out_message) *out_message = message;
}

static bool cxpr_call_bind_named_window(const char* const* arg_names,
                                        const cxpr_ast* const* args,
                                        size_t argc,
                                        const char* const* param_names,
                                        size_t param_count,
                                        const cxpr_ast** out_args) {
    for (size_t window_start = 0; window_start + argc <= param_count; ++window_start) {
        bool candidate_seen_named = false;
        bool candidate_used[CXPR_MAX_PARAM_NAMES] = {0};
        size_t candidate_positional = 0;
        bool candidate_ok = true;
        const cxpr_ast* candidate_args[CXPR_MAX_CALL_ARGS] = {0};

        for (size_t i = 0; i < argc; ++i) {
            const char* arg_name = arg_names ? arg_names[i] : NULL;

            if (!arg_name) {
                if (candidate_seen_named || candidate_positional >= argc) {
                    candidate_ok = false;
                    break;
                }
                if (out_args) candidate_args[candidate_positional] = args[i];
                candidate_used[candidate_positional++] = true;
                continue;
            }

            candidate_seen_named = true;
            {
                size_t match = argc;
                for (size_t j = 0; j < argc; ++j) {
                    if (strcmp(param_names[window_start + j], arg_name) == 0) {
                        match = j;
                        break;
                    }
                }
                if (match == argc || candidate_used[match]) {
                    candidate_ok = false;
                    break;
                }
                if (out_args) candidate_args[match] = args[i];
                candidate_used[match] = true;
            }
        }

        if (!candidate_ok) continue;
        for (size_t i = 0; i < argc; ++i) {
            if (!candidate_used[i]) {
                candidate_ok = false;
                break;
            }
        }
        if (!candidate_ok) continue;

        if (out_args) {
            for (size_t i = 0; i < argc; ++i) out_args[i] = candidate_args[i];
        }
        return true;
    }

    return false;
}

bool cxpr_call_bind_args(const cxpr_ast* ast, const cxpr_func_entry* entry,
                         const cxpr_ast** out_args,
                         cxpr_error_code* out_code,
                         const char** out_message) {
    const char* const* param_names;
    const cxpr_ast* const* args;
    char* const* arg_names;
    bool used[CXPR_MAX_PARAM_NAMES] = {0};
    bool seen_named = false;
    size_t param_count = 0;
    size_t positional_index = 0;
    size_t argc = 0;

    if (!ast || !entry) return false;

    if (ast->type == CXPR_NODE_FUNCTION_CALL) {
        argc = ast->data.function_call.argc;
        args = (const cxpr_ast* const*)ast->data.function_call.args;
        arg_names = ast->data.function_call.arg_names;
    } else if (ast->type == CXPR_NODE_PRODUCER_ACCESS) {
        argc = ast->data.producer_access.argc;
        args = (const cxpr_ast* const*)ast->data.producer_access.args;
        arg_names = ast->data.producer_access.arg_names;
    } else {
        return false;
    }

    if (argc > CXPR_MAX_CALL_ARGS) {
        cxpr_call_bind_set_error(out_code, out_message, CXPR_ERR_WRONG_ARITY,
                                 "Too many function arguments");
        return false;
    }

    if (!cxpr_ast_call_uses_named_args(ast)) {
        if (out_args) {
            for (size_t i = 0; i < argc; ++i) out_args[i] = args[i];
        }
        return true;
    }

    param_names = cxpr_registry_entry_param_names(entry, &param_count);
    if (!param_names || param_count == 0 || param_count > CXPR_MAX_PARAM_NAMES) {
        cxpr_call_bind_set_error(out_code, out_message, CXPR_ERR_SYNTAX,
                                 "Named arguments are not supported for this function");
        return false;
    }

    if (ast->type == CXPR_NODE_PRODUCER_ACCESS &&
        cxpr_call_bind_named_window((const char* const*)arg_names, args, argc,
                                    param_names, param_count, out_args)) {
        return true;
    }

    for (size_t i = 0; i < argc; ++i) {
        const char* arg_name = arg_names ? arg_names[i] : NULL;

        if (!arg_name) {
            if (seen_named) {
                cxpr_call_bind_set_error(out_code, out_message, CXPR_ERR_SYNTAX,
                                         "Positional arguments cannot follow named arguments");
                return false;
            }
            if (positional_index >= param_count) {
                cxpr_call_bind_set_error(out_code, out_message, CXPR_ERR_WRONG_ARITY,
                                         "Wrong number of arguments");
                return false;
            }
            if (out_args) out_args[positional_index] = args[i];
            used[positional_index++] = true;
            continue;
        }

        seen_named = true;
        {
            size_t match = param_count;
            for (size_t j = 0; j < param_count; ++j) {
                if (strcmp(param_names[j], arg_name) == 0) {
                    match = j;
                    break;
                }
            }
            if (match == param_count) {
                cxpr_call_bind_set_error(out_code, out_message, CXPR_ERR_SYNTAX,
                                         "Unknown named argument");
                return false;
            }
            if (used[match]) {
                cxpr_call_bind_set_error(out_code, out_message, CXPR_ERR_SYNTAX,
                                         "Duplicate argument");
                return false;
            }
            if (out_args) out_args[match] = args[i];
            used[match] = true;
        }
    }

    for (size_t i = 0; i < argc; ++i) {
        if (!used[i]) {
            cxpr_call_bind_set_error(out_code, out_message, CXPR_ERR_SYNTAX,
                                     "Named arguments must fill a positional prefix");
            return false;
        }
    }

    return true;
}
