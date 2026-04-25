/**
 * @file helpers.c
 * @brief Shared evaluator helpers, cloning, and cached lookup support.
 */

#include "internal.h"
#include "../core.h"
#include "../context/state.h"

#include "../limits.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

bool cxpr_eval_constant_double(const cxpr_ast* ast, double* out) {
    double left;
    double right;

    if (!ast || !out) return false;

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        *out = ast->data.number.value;
        return true;

    case CXPR_NODE_UNARY_OP:
        if (ast->data.unary_op.op != CXPR_TOK_MINUS ||
            !cxpr_eval_constant_double(ast->data.unary_op.operand, out)) {
            return false;
        }
        *out = -*out;
        return true;

    case CXPR_NODE_BINARY_OP:
        if (!cxpr_eval_constant_double(ast->data.binary_op.left, &left) ||
            !cxpr_eval_constant_double(ast->data.binary_op.right, &right)) {
            return false;
        }
        switch (ast->data.binary_op.op) {
        case CXPR_TOK_PLUS: *out = left + right; return true;
        case CXPR_TOK_MINUS: *out = left - right; return true;
        case CXPR_TOK_STAR: *out = left * right; return true;
        case CXPR_TOK_SLASH:
            if (right == 0.0) return false;
            *out = left / right;
            return true;
        case CXPR_TOK_PERCENT:
            if (right == 0.0) return false;
            *out = fmod(left, right);
            return true;
        case CXPR_TOK_POWER:
            *out = pow(left, right);
            return true;
        default:
            return false;
        }

    case CXPR_NODE_TERNARY:
        if (!cxpr_eval_constant_double(ast->data.ternary.condition, &left)) return false;
        if (left != 0.0) return cxpr_eval_constant_double(ast->data.ternary.true_branch, out);
        return cxpr_eval_constant_double(ast->data.ternary.false_branch, out);

    default:
        return false;
    }
}

static bool cxpr_eval_const_or_param_double(const cxpr_ast* ast,
                                            const cxpr_context* ctx,
                                            double* out) {
    double left;
    double right;
    bool found = false;

    if (!ast || !out) return false;

    switch (ast->type) {
    case CXPR_NODE_VARIABLE:
        if (!ctx) return false;
        *out = cxpr_context_get_param(ctx, ast->data.variable.name, &found);
        return found;

    case CXPR_NODE_NUMBER:
        *out = ast->data.number.value;
        return true;

    case CXPR_NODE_UNARY_OP:
        if (ast->data.unary_op.op != CXPR_TOK_MINUS ||
            !cxpr_eval_const_or_param_double(ast->data.unary_op.operand, ctx, out)) {
            return false;
        }
        *out = -*out;
        return true;

    case CXPR_NODE_BINARY_OP:
        if (!cxpr_eval_const_or_param_double(ast->data.binary_op.left, ctx, &left) ||
            !cxpr_eval_const_or_param_double(ast->data.binary_op.right, ctx, &right)) {
            return false;
        }
        switch (ast->data.binary_op.op) {
        case CXPR_TOK_PLUS: *out = left + right; return true;
        case CXPR_TOK_MINUS: *out = left - right; return true;
        case CXPR_TOK_STAR: *out = left * right; return true;
        case CXPR_TOK_SLASH:
            if (right == 0.0) return false;
            *out = left / right;
            return true;
        case CXPR_TOK_PERCENT:
            if (right == 0.0) return false;
            *out = fmod(left, right);
            return true;
        case CXPR_TOK_POWER:
            *out = pow(left, right);
            return true;
        default:
            return false;
        }

    case CXPR_NODE_TERNARY:
        if (!cxpr_eval_const_or_param_double(ast->data.ternary.condition, ctx, &left)) {
            return false;
        }
        if (left != 0.0) {
            return cxpr_eval_const_or_param_double(ast->data.ternary.true_branch, ctx, out);
        }
        return cxpr_eval_const_or_param_double(ast->data.ternary.false_branch, ctx, out);

    default:
        return false;
    }
}

cxpr_ast* cxpr_eval_clone_ast(const cxpr_ast* ast) {
    if (!ast) return NULL;

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        return cxpr_ast_new_number(ast->data.number.value);
    case CXPR_NODE_BOOL:
        return cxpr_ast_new_bool(ast->data.boolean.value);
    case CXPR_NODE_STRING:
        return cxpr_ast_new_string(ast->data.string.value);
    case CXPR_NODE_IDENTIFIER:
        return cxpr_ast_new_identifier(ast->data.identifier.name);
    case CXPR_NODE_VARIABLE:
        return cxpr_ast_new_variable(ast->data.variable.name);
    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_ast_new_field_access(ast->data.field_access.object, ast->data.field_access.field);
    case CXPR_NODE_CHAIN_ACCESS:
        return cxpr_ast_new_chain_access((const char* const*)ast->data.chain_access.path,
                                         ast->data.chain_access.depth);
    case CXPR_NODE_UNARY_OP: {
        cxpr_ast* operand = cxpr_eval_clone_ast(ast->data.unary_op.operand);
        if (!operand) return NULL;
        return cxpr_ast_new_unary_op(ast->data.unary_op.op, operand);
    }
    case CXPR_NODE_BINARY_OP: {
        cxpr_ast* left = cxpr_eval_clone_ast(ast->data.binary_op.left);
        cxpr_ast* right = cxpr_eval_clone_ast(ast->data.binary_op.right);
        if (!left || !right) {
            cxpr_ast_free(left);
            cxpr_ast_free(right);
            return NULL;
        }
        return cxpr_ast_new_binary_op(ast->data.binary_op.op, left, right);
    }
    case CXPR_NODE_FUNCTION_CALL: {
        cxpr_ast** args = NULL;
        char** arg_names = NULL;
        if (ast->data.function_call.argc > 0) {
            args = (cxpr_ast**)calloc(ast->data.function_call.argc, sizeof(cxpr_ast*));
            arg_names = (char**)calloc(ast->data.function_call.argc, sizeof(char*));
            if (!args || !arg_names) {
                free(args);
                free(arg_names);
                return NULL;
            }
            for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
                args[i] = cxpr_eval_clone_ast(ast->data.function_call.args[i]);
                if (!args[i]) {
                    for (size_t j = 0; j < i; ++j) cxpr_ast_free(args[j]);
                    for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                    free(args);
                    free(arg_names);
                    return NULL;
                }
                if (ast->data.function_call.arg_names &&
                    ast->data.function_call.arg_names[i]) {
                    arg_names[i] = cxpr_strdup(ast->data.function_call.arg_names[i]);
                    if (!arg_names[i]) {
                        for (size_t j = 0; j <= i; ++j) cxpr_ast_free(args[j]);
                        for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                        free(args);
                        free(arg_names);
                        return NULL;
                    }
                }
            }
        }
        return cxpr_ast_new_function_call_named(ast->data.function_call.name, args,
                                                arg_names, ast->data.function_call.argc);
    }
    case CXPR_NODE_PRODUCER_ACCESS: {
        cxpr_ast** args = NULL;
        char** arg_names = NULL;
        if (ast->data.producer_access.argc > 0) {
            args = (cxpr_ast**)calloc(ast->data.producer_access.argc, sizeof(cxpr_ast*));
            arg_names = (char**)calloc(ast->data.producer_access.argc, sizeof(char*));
            if (!args || !arg_names) {
                free(args);
                free(arg_names);
                return NULL;
            }
            for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
                args[i] = cxpr_eval_clone_ast(ast->data.producer_access.args[i]);
                if (!args[i]) {
                    for (size_t j = 0; j < i; ++j) cxpr_ast_free(args[j]);
                    for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                    free(args);
                    free(arg_names);
                    return NULL;
                }
                if (ast->data.producer_access.arg_names &&
                    ast->data.producer_access.arg_names[i]) {
                    arg_names[i] = cxpr_strdup(ast->data.producer_access.arg_names[i]);
                    if (!arg_names[i]) {
                        for (size_t j = 0; j <= i; ++j) cxpr_ast_free(args[j]);
                        for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                        free(args);
                        free(arg_names);
                        return NULL;
                    }
                }
            }
        }
        return cxpr_ast_new_producer_access_named(ast->data.producer_access.name, args,
                                                  arg_names, ast->data.producer_access.argc,
                                                  ast->data.producer_access.field);
    }
    case CXPR_NODE_LOOKBACK: {
        cxpr_ast* target = cxpr_eval_clone_ast(ast->data.lookback.target);
        cxpr_ast* index = cxpr_eval_clone_ast(ast->data.lookback.index);
        if (!target || !index) {
            cxpr_ast_free(target);
            cxpr_ast_free(index);
            return NULL;
        }
        return cxpr_ast_new_lookback(target, index);
    }
    case CXPR_NODE_TERNARY: {
        cxpr_ast* condition = cxpr_eval_clone_ast(ast->data.ternary.condition);
        cxpr_ast* yes = cxpr_eval_clone_ast(ast->data.ternary.true_branch);
        cxpr_ast* no = cxpr_eval_clone_ast(ast->data.ternary.false_branch);
        if (!condition || !yes || !no) {
            cxpr_ast_free(condition);
            cxpr_ast_free(yes);
            cxpr_ast_free(no);
            return NULL;
        }
        return cxpr_ast_new_ternary(condition, yes, no);
    }
    }

    return NULL;
}

const char* cxpr_eval_prepare_const_key_for_producer(const cxpr_ast* ast,
                                                     const cxpr_ast* const* ordered_args,
                                                     size_t argc,
                                                     const cxpr_context* ctx,
                                                     const cxpr_registry* reg,
                                                     char* local_buf,
                                                     size_t local_cap,
                                                     char** heap_buf,
                                                     cxpr_error* err) {
    cxpr_ast* mutable_ast = (cxpr_ast*)ast;
    const char* key;
    double values[CXPR_MAX_CALL_ARGS];

    if (heap_buf) *heap_buf = NULL;
    if (!ast || ast->type != CXPR_NODE_PRODUCER_ACCESS ||
        argc > CXPR_MAX_CALL_ARGS) {
        return NULL;
    }
    if (!cxpr_ast_producer_has_named_args(ast) &&
        ast->data.producer_access.cached_const_key_ready &&
        ast->data.producer_access.cached_const_key) {
        return ast->data.producer_access.cached_const_key;
    }
    if (!cxpr_ast_producer_has_named_args(ast) &&
        !ast->data.producer_access.cached_const_key_ready) {
        mutable_ast->data.producer_access.cached_const_key_ready = true;
        if (argc == 0u) {
            key = cxpr_build_struct_cache_key(ast->data.producer_access.name, values,
                                              0u,
                                              local_buf, local_cap, heap_buf);
            if (!key) return NULL;
            mutable_ast->data.producer_access.cached_const_key =
                heap_buf && *heap_buf ? *heap_buf : cxpr_strdup(key);
            if (heap_buf) *heap_buf = NULL;
            return mutable_ast->data.producer_access.cached_const_key;
        }
        for (size_t i = 0; i < argc; ++i) {
            if (!cxpr_eval_constant_double(ast->data.producer_access.args[i], &values[i])) {
                break;
            }
            if (i + 1u == argc) {
                key = cxpr_build_struct_cache_key(ast->data.producer_access.name, values,
                                                  argc,
                                                  local_buf, local_cap, heap_buf);
                if (!key) return NULL;

                mutable_ast->data.producer_access.cached_const_key =
                    heap_buf && *heap_buf ? *heap_buf : cxpr_strdup(key);
                if (heap_buf) *heap_buf = NULL;
                return mutable_ast->data.producer_access.cached_const_key;
            }
        }
    }

    if (ordered_args && ctx) {
        for (size_t i = 0; i < argc; ++i) {
            if (!cxpr_eval_const_or_param_double(ordered_args[i], ctx, &values[i])) {
                break;
            }
            if (i + 1u == argc) {
                key = cxpr_build_struct_cache_key(ast->data.producer_access.name, values,
                                                  argc,
                                                  local_buf, local_cap, heap_buf);
                if (!key) return NULL;
                return key;
            }
        }
    }

    if (!ordered_args || !ctx || !reg) return NULL;
    for (size_t i = 0; i < argc; ++i) {
        values[i] = cxpr_eval_scalar_arg(ordered_args[i], ctx, reg, err);
        if ((err && err->code != CXPR_OK) || !isfinite(values[i])) return NULL;
    }
    key = cxpr_build_struct_cache_key(ast->data.producer_access.name, values,
                                      argc,
                                      local_buf, local_cap, heap_buf);
    if (!key) return NULL;
    return key;
}

const char* cxpr_build_struct_cache_key(const char* name, const double* args, size_t argc,
                                        char* local_buf, size_t local_cap, char** heap_buf) {
    size_t len;
    size_t offset;
    char* key;
    int written;

    if (heap_buf) *heap_buf = NULL;
    if (!name) return NULL;
    if (argc == 0) return name;

    len = strlen(name) + 4 + (argc * 32);
    if (local_buf && len <= local_cap) {
        key = local_buf;
    } else {
        key = (char*)malloc(len);
        if (!key) return NULL;
        if (heap_buf) *heap_buf = key;
    }

    written = snprintf(key, len, "%s(", name);
    if (written < 0 || (size_t)written >= len) {
        if (heap_buf && *heap_buf) free(*heap_buf);
        if (heap_buf) *heap_buf = NULL;
        return NULL;
    }
    offset = (size_t)written;

    for (size_t i = 0; i < argc; ++i) {
        written = snprintf(key + offset, len - offset, i == 0 ? "%a" : ",%a", args[i]);
        if (written < 0 || (size_t)written >= len - offset) {
            if (heap_buf && *heap_buf) free(*heap_buf);
            if (heap_buf) *heap_buf = NULL;
            return NULL;
        }
        offset += (size_t)written;
    }

    written = snprintf(key + offset, len - offset, ")");
    if (written < 0 || (size_t)written >= len - offset) {
        if (heap_buf && *heap_buf) free(*heap_buf);
        if (heap_buf) *heap_buf = NULL;
        return NULL;
    }

    return key;
}

const cxpr_struct_value* cxpr_eval_struct_result(cxpr_func_entry* entry,
                                                 const char* name,
                                                 const cxpr_ast* const* arg_nodes,
                                                 size_t argc,
                                                 const char* cache_key_hint,
                                                 const cxpr_context* ctx,
                                                 const cxpr_registry* reg,
                                                 cxpr_error* err) {
    cxpr_context* mutable_ctx = (cxpr_context*)ctx;
    const cxpr_struct_value* existing;
    cxpr_struct_value* produced;
    cxpr_value outputs[CXPR_MAX_PRODUCER_FIELDS];
    double args[CXPR_MAX_CALL_ARGS] = {0.0};
    char cache_key_local[256];
    char* cache_key_heap = NULL;
    const char* cache_key;

    if (!entry || !entry->struct_producer || !name) {
        if (err) {
            err->code = CXPR_ERR_UNKNOWN_FUNCTION;
            err->message = "Unknown function";
        }
        return NULL;
    }

    if (argc < entry->min_args || argc > entry->max_args) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Wrong number of arguments";
        }
        return NULL;
    }
    if (argc > CXPR_MAX_CALL_ARGS || entry->fields_per_arg > CXPR_MAX_PRODUCER_FIELDS) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Producer arity too large";
        }
        return NULL;
    }

    for (size_t i = 0; i < argc; ++i) {
        args[i] = cxpr_eval_scalar_arg(arg_nodes[i], ctx, reg, err);
        if (err && err->code != CXPR_OK) return NULL;
    }

    cache_key = cache_key_hint;
    if (!cache_key) {
        cache_key = cxpr_build_struct_cache_key(name, args, argc,
                                                cache_key_local, sizeof(cache_key_local),
                                                &cache_key_heap);
    }
    if (!cache_key) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return NULL;
    }

    existing = cxpr_context_get_cached_struct(ctx, cache_key);
    if (existing) {
        free(cache_key_heap);
        return existing;
    }

    entry->struct_producer(args, argc, outputs, entry->fields_per_arg, entry->userdata);
    produced = cxpr_struct_value_new((const char* const*)entry->struct_fields,
                                     outputs, entry->fields_per_arg);
    if (!produced) {
        free(cache_key_heap);
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return NULL;
    }

    cxpr_context_set_cached_struct(mutable_ctx, cache_key, produced);
    cxpr_struct_value_free(produced);
    existing = cxpr_context_get_cached_struct(ctx, cache_key);
    free(cache_key_heap);
    return existing;
}

cxpr_func_entry* cxpr_eval_cached_function_entry(const cxpr_ast* ast,
                                                 const cxpr_registry* reg) {
    cxpr_ast* mutable_ast = (cxpr_ast*)ast;

    if (!ast || ast->type != CXPR_NODE_FUNCTION_CALL || !reg) return NULL;

    if (ast->data.function_call.cached_lookup_valid &&
        ast->data.function_call.cached_registry == reg &&
        ast->data.function_call.cached_registry_version == reg->version) {
        if (!ast->data.function_call.cached_entry_found ||
            ast->data.function_call.cached_entry_index >= reg->count) {
            return NULL;
        }
        return &((cxpr_registry*)reg)->entries[ast->data.function_call.cached_entry_index];
    }

    mutable_ast->data.function_call.cached_entry_found = false;
    mutable_ast->data.function_call.cached_entry_index = 0;
    for (size_t i = 0; i < reg->count; ++i) {
        if (reg->entries[i].name &&
            strcmp(reg->entries[i].name, ast->data.function_call.name) == 0) {
            mutable_ast->data.function_call.cached_entry_index = i;
            mutable_ast->data.function_call.cached_entry_found = true;
            break;
        }
    }
    mutable_ast->data.function_call.cached_registry = reg;
    mutable_ast->data.function_call.cached_registry_version = reg->version;
    mutable_ast->data.function_call.cached_lookup_valid = true;
    if (!mutable_ast->data.function_call.cached_entry_found) return NULL;
    return &((cxpr_registry*)reg)->entries[mutable_ast->data.function_call.cached_entry_index];
}

cxpr_func_entry* cxpr_eval_cached_producer_entry(const cxpr_ast* ast,
                                                 const cxpr_registry* reg) {
    cxpr_ast* mutable_ast = (cxpr_ast*)ast;

    if (!ast || ast->type != CXPR_NODE_PRODUCER_ACCESS || !reg) return NULL;

    if (ast->data.producer_access.cached_lookup_valid &&
        ast->data.producer_access.cached_registry == reg &&
        ast->data.producer_access.cached_registry_version == reg->version) {
        if (!ast->data.producer_access.cached_entry_found ||
            ast->data.producer_access.cached_entry_index >= reg->count) {
            return NULL;
        }
        return &((cxpr_registry*)reg)->entries[ast->data.producer_access.cached_entry_index];
    }

    mutable_ast->data.producer_access.cached_entry_found = false;
    mutable_ast->data.producer_access.cached_entry_index = 0;
    mutable_ast->data.producer_access.cached_field_index_valid = false;

    for (size_t i = 0; i < reg->count; ++i) {
        if (reg->entries[i].name &&
            strcmp(reg->entries[i].name, ast->data.producer_access.name) == 0) {
            mutable_ast->data.producer_access.cached_entry_index = i;
            mutable_ast->data.producer_access.cached_entry_found = true;
            break;
        }
    }

    mutable_ast->data.producer_access.cached_registry = reg;
    mutable_ast->data.producer_access.cached_registry_version = reg->version;
    mutable_ast->data.producer_access.cached_lookup_valid = true;
    if (!mutable_ast->data.producer_access.cached_entry_found) return NULL;
    return &((cxpr_registry*)reg)->entries[mutable_ast->data.producer_access.cached_entry_index];
}

bool cxpr_eval_ast_contains_string_literal(const cxpr_ast* ast) {
    size_t i;

    if (!ast) return false;

    switch (ast->type) {
    case CXPR_NODE_STRING:
        return true;

    case CXPR_NODE_BINARY_OP:
        return cxpr_eval_ast_contains_string_literal(ast->data.binary_op.left) ||
               cxpr_eval_ast_contains_string_literal(ast->data.binary_op.right);

    case CXPR_NODE_UNARY_OP:
        return cxpr_eval_ast_contains_string_literal(ast->data.unary_op.operand);

    case CXPR_NODE_FUNCTION_CALL:
        for (i = 0; i < ast->data.function_call.argc; ++i) {
            if (cxpr_eval_ast_contains_string_literal(ast->data.function_call.args[i])) {
                return true;
            }
        }
        return false;

    case CXPR_NODE_PRODUCER_ACCESS:
        for (i = 0; i < ast->data.producer_access.argc; ++i) {
            if (cxpr_eval_ast_contains_string_literal(ast->data.producer_access.args[i])) {
                return true;
            }
        }
        return false;

    case CXPR_NODE_LOOKBACK:
        return cxpr_eval_ast_contains_string_literal(ast->data.lookback.target) ||
               cxpr_eval_ast_contains_string_literal(ast->data.lookback.index);

    case CXPR_NODE_TERNARY:
        return cxpr_eval_ast_contains_string_literal(ast->data.ternary.condition) ||
               cxpr_eval_ast_contains_string_literal(ast->data.ternary.true_branch) ||
               cxpr_eval_ast_contains_string_literal(ast->data.ternary.false_branch);

    default:
        return false;
    }
}
