/**
 * @file eval.c
 * @brief Typed tree-walk evaluator for cxpr.
 */

#include "internal.h"
#include <math.h>
#include <stdio.h>

static cxpr_field_value cxpr_eval_node(const cxpr_ast* ast, const cxpr_context* ctx,
                                       const cxpr_registry* reg, cxpr_error* err);
static double cxpr_eval_scalar_arg(const cxpr_ast* ast, const cxpr_context* ctx,
                                   const cxpr_registry* reg, cxpr_error* err);
static bool cxpr_eval_constant_double(const cxpr_ast* ast, double* out);
static const cxpr_struct_value* cxpr_eval_struct_result(cxpr_func_entry* entry,
                                                        const char* name,
                                                        const cxpr_ast* const* arg_nodes,
                                                        size_t argc,
                                                        const cxpr_context* ctx,
                                                        const cxpr_registry* reg,
                                                        cxpr_error* err);

static cxpr_field_value cxpr_eval_error(cxpr_error* err, cxpr_error_code code,
                                        const char* message) {
    if (err) {
        err->code = code;
        err->message = message;
    }
    return cxpr_fv_double(NAN);
}

static bool cxpr_require_type(cxpr_field_value value, cxpr_field_type type,
                              cxpr_error* err, const char* message) {
    if (value.type != type) {
        cxpr_eval_error(err, CXPR_ERR_TYPE_MISMATCH, message);
        return false;
    }
    return true;
}

static bool cxpr_eval_constant_double(const cxpr_ast* ast, double* out) {
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
        default:
            return false;
        }
    default:
        return false;
    }
}

static cxpr_field_value cxpr_struct_get_field(const cxpr_struct_value* value,
                                              const char* field, bool* found) {
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

static const char* cxpr_build_struct_cache_key(const char* name, const double* args, size_t argc,
                                               char* local_buf, size_t local_cap,
                                               char** heap_buf) {
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

static cxpr_func_entry* cxpr_eval_cached_producer_entry(const cxpr_ast* ast,
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

static const char* cxpr_eval_prepare_const_key_for_call(const cxpr_ast* ast, const char* name,
                                                        const cxpr_ast* const* args, size_t argc) {
    cxpr_ast* mutable_ast = (cxpr_ast*)ast;
    char local_buf[256];
    char* heap_buf = NULL;
    const char* key;
    double values[32];

    if (!ast || ast->type != CXPR_NODE_FUNCTION_CALL || argc > 32) return NULL;
    if (ast->data.function_call.cached_const_key_ready) {
        return ast->data.function_call.cached_const_key;
    }
    mutable_ast->data.function_call.cached_const_key_ready = true;

    for (size_t i = 0; i < argc; ++i) {
        if (!cxpr_eval_constant_double(args[i], &values[i])) return NULL;
    }

    key = cxpr_build_struct_cache_key(name, values, argc, local_buf, sizeof(local_buf), &heap_buf);
    if (!key) return NULL;
    mutable_ast->data.function_call.cached_const_key =
        heap_buf ? heap_buf : cxpr_strdup(key);
    return mutable_ast->data.function_call.cached_const_key;
}

static const char* cxpr_eval_prepare_const_key_for_producer(const cxpr_ast* ast) {
    cxpr_ast* mutable_ast = (cxpr_ast*)ast;
    char local_buf[256];
    char* heap_buf = NULL;
    const char* key;
    double values[32];

    if (!ast || ast->type != CXPR_NODE_PRODUCER_ACCESS ||
        ast->data.producer_access.argc > 32) {
        return NULL;
    }
    if (ast->data.producer_access.cached_const_key_ready) {
        return ast->data.producer_access.cached_const_key;
    }
    mutable_ast->data.producer_access.cached_const_key_ready = true;

    for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
        if (!cxpr_eval_constant_double(ast->data.producer_access.args[i], &values[i])) return NULL;
    }

    key = cxpr_build_struct_cache_key(ast->data.producer_access.name, values,
                                      ast->data.producer_access.argc,
                                      local_buf, sizeof(local_buf), &heap_buf);
    if (!key) return NULL;
    mutable_ast->data.producer_access.cached_const_key =
        heap_buf ? heap_buf : cxpr_strdup(key);
    return mutable_ast->data.producer_access.cached_const_key;
}

static const cxpr_struct_value* cxpr_eval_struct_result(cxpr_func_entry* entry,
                                                        const char* name,
                                                        const cxpr_ast* const* arg_nodes,
                                                        size_t argc,
                                                        const cxpr_context* ctx,
                                                        const cxpr_registry* reg,
                                                        cxpr_error* err) {
    cxpr_context* mutable_ctx = (cxpr_context*)ctx;
    const cxpr_struct_value* existing;
    cxpr_struct_value* produced;
    cxpr_field_value outputs[64];
    double args[32];
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
    if (argc > 32 || entry->fields_per_arg > 64) {
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

    cache_key = cxpr_build_struct_cache_key(name, args, argc,
                                            cache_key_local, sizeof(cache_key_local),
                                            &cache_key_heap);
    if (!cache_key) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return NULL;
    }

    existing = cxpr_context_get_struct(ctx, cache_key);
    if (!existing && argc == 0) {
        existing = cxpr_context_get_struct(ctx, name);
    }
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

    cxpr_context_set_struct(mutable_ctx, cache_key, produced);
    cxpr_struct_value_free(produced);
    existing = cxpr_context_get_struct(ctx, cache_key);
    free(cache_key_heap);
    return existing;
}

/**
 * @brief Resolve and cache the registry entry for a FUNCTION_CALL AST node.
 *
 * @param ast Function-call AST node whose cache may be refreshed.
 * @param reg Registry used for resolution.
 * @return Matching registry entry, or NULL when the function is unknown.
 */
static cxpr_func_entry* cxpr_eval_cached_function_entry(const cxpr_ast* ast,
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

static cxpr_field_value cxpr_eval_struct_producer(cxpr_func_entry* entry, const char* name,
                                                  const char* field,
                                                  const cxpr_ast* const* arg_nodes,
                                                  size_t argc,
                                                  const cxpr_context* ctx,
                                                  const cxpr_registry* reg,
                                                  cxpr_error* err) {
    const cxpr_struct_value* produced;
    cxpr_field_value result;
    bool found = false;

    if (!entry || !entry->struct_producer) {
        return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown field access");
    }

    produced = cxpr_eval_struct_result(entry, name, arg_nodes, argc, ctx, reg, err);
    if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);

    result = cxpr_struct_get_field(produced, field, &found);
    if (!found) {
        return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown field access");
    }
    return result;
}

static double cxpr_eval_scalar_arg(const cxpr_ast* ast, const cxpr_context* ctx,
                                   const cxpr_registry* reg, cxpr_error* err) {
    cxpr_field_value value = cxpr_eval_node(ast, ctx, reg, err);
    if (err && err->code != CXPR_OK) return NAN;
    if (!cxpr_require_type(value, CXPR_FIELD_DOUBLE, err, "Expected double argument")) {
        return NAN;
    }
    return value.d;
}

static cxpr_field_value cxpr_eval_defined_function(cxpr_func_entry* entry,
                                                   const cxpr_ast* call_ast,
                                                   const cxpr_context* ctx,
                                                   const cxpr_registry* reg,
                                                   cxpr_error* err) {
    const size_t argc = call_ast->data.function_call.argc;
    cxpr_context* tmp = NULL;
    double scalar_locals[32];
    bool scalar_only = (argc <= 32);

    if (argc != entry->defined_param_count) {
        return cxpr_eval_error(err, CXPR_ERR_WRONG_ARITY, "Wrong number of arguments");
    }

    for (size_t i = 0; i < entry->defined_param_count; i++) {
        if (entry->defined_param_fields[i] && entry->defined_param_field_counts[i] > 0) {
            scalar_only = false;
            break;
        }
    }

    if (scalar_only) {
        for (size_t i = 0; i < entry->defined_param_count; i++) {
            cxpr_field_value v = cxpr_eval_node(call_ast->data.function_call.args[i], ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_require_type(v, CXPR_FIELD_DOUBLE, err,
                                   "Defined function locals must be doubles")) {
                return cxpr_fv_double(NAN);
            }
            scalar_locals[i] = v.d;
        }
    } else {
        tmp = cxpr_context_overlay_new(ctx);
        if (!tmp) return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");

        for (size_t i = 0; i < entry->defined_param_count; i++) {
        const char* pname = entry->defined_param_names[i];
        const cxpr_ast* arg = call_ast->data.function_call.args[i];

        if (entry->defined_param_fields[i] &&
            entry->defined_param_field_counts[i] > 0) {
            if (arg->type != CXPR_NODE_IDENTIFIER) {
                cxpr_context_free(tmp);
                return cxpr_eval_error(err, CXPR_ERR_SYNTAX,
                                       "Struct argument must be an identifier");
            }

            for (size_t f = 0; f < entry->defined_param_field_counts[i]; f++) {
                bool found = false;
                cxpr_field_value value =
                    cxpr_context_get_field(ctx, arg->data.identifier.name,
                                           entry->defined_param_fields[i][f], &found);
                char dst_key[256];
                char src_key[256];

                if (!found) {
                    double fallback;
                    snprintf(src_key, sizeof(src_key), "%s.%s", arg->data.identifier.name,
                             entry->defined_param_fields[i][f]);
                    fallback = cxpr_context_get(ctx, src_key, &found);
                    if (!found) {
                        cxpr_context_free(tmp);
                        return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                               "Unknown struct field");
                    }
                    value = cxpr_fv_double(fallback);
                }
                if (!cxpr_require_type(value, CXPR_FIELD_DOUBLE, err,
                                       "Struct function arguments must be scalar doubles")) {
                    cxpr_context_free(tmp);
                    return cxpr_fv_double(NAN);
                }

                snprintf(dst_key, sizeof(dst_key), "%s.%s", pname,
                         entry->defined_param_fields[i][f]);
                cxpr_context_set(tmp, dst_key, value.d);
            }
        } else {
            cxpr_field_value value = cxpr_eval_node(arg, ctx, reg, err);
            if (err && err->code != CXPR_OK) {
                cxpr_context_free(tmp);
                return cxpr_fv_double(NAN);
            }
            if (!cxpr_require_type(value, CXPR_FIELD_DOUBLE, err,
                                   "Defined function locals must be doubles")) {
                cxpr_context_free(tmp);
                return cxpr_fv_double(NAN);
            }
            cxpr_context_set(tmp, pname, value.d);
        }
        }
    }

    if (scalar_only) {
        if (cxpr_ir_prepare_defined_program(entry, reg, err) && entry->defined_program) {
            return cxpr_fv_double(cxpr_ir_exec_with_locals(&entry->defined_program->ir, ctx, reg,
                                                           scalar_locals,
                                                           entry->defined_param_count, err));
        }

        tmp = cxpr_context_overlay_new(ctx);
        if (!tmp) return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        for (size_t i = 0; i < entry->defined_param_count; i++) {
            cxpr_context_set(tmp, entry->defined_param_names[i], scalar_locals[i]);
        }
    }

    {
        cxpr_field_value result = cxpr_eval_node(entry->defined_body, tmp ? tmp : ctx, reg, err);
        if (tmp) cxpr_context_free(tmp);
        return result;
    }
}

static cxpr_field_value cxpr_eval_field_access(const cxpr_ast* ast, const cxpr_context* ctx,
                                               const cxpr_registry* reg, cxpr_error* err) {
    bool found = false;
    cxpr_field_value value =
        cxpr_context_get_field(ctx, ast->data.field_access.object, ast->data.field_access.field,
                               &found);

    if (!found) {
        double fallback =
            cxpr_context_get(ctx, ast->data.field_access.full_key, &found);
        if (found) {
            /* deprecated: flat-key fallback, removed in Phase 4 */
            return cxpr_fv_double(fallback);
        }
        {
            cxpr_func_entry* producer = cxpr_registry_find(reg, ast->data.field_access.object);
            if (producer && producer->struct_producer) {
                value = cxpr_eval_struct_producer(producer, ast->data.field_access.object,
                                                  ast->data.field_access.field, NULL, 0,
                                                  ctx, reg, err);
                if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
                found = true;
            } else {
                return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown field access");
            }
        }
    }

    return value;
}

static cxpr_field_value cxpr_eval_chain_access(const cxpr_ast* ast, const cxpr_context* ctx,
                                               cxpr_error* err) {
    const cxpr_struct_value* current = cxpr_context_get_struct(ctx, ast->data.chain_access.path[0]);
    if (!current) {
        return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown identifier");
    }

    for (size_t i = 1; i < ast->data.chain_access.depth; i++) {
        bool found = false;
        cxpr_field_value value = cxpr_fv_double(0.0);

        for (size_t j = 0; j < current->field_count; j++) {
            if (strcmp(current->field_names[j], ast->data.chain_access.path[i]) == 0) {
                found = true;
                value = current->field_values[j];
                break;
            }
        }

        if (!found) {
            return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown field access");
        }

        if (i + 1 == ast->data.chain_access.depth) return value;

        if (value.type != CXPR_FIELD_STRUCT) {
            return cxpr_eval_error(err, CXPR_ERR_TYPE_MISMATCH,
                                   "Chain access requires struct intermediates");
        }
        current = value.s;
    }

    return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown field access");
}

static cxpr_field_value cxpr_eval_node(const cxpr_ast* ast, const cxpr_context* ctx,
                                       const cxpr_registry* reg, cxpr_error* err) {
    if (!ast) return cxpr_eval_error(err, CXPR_ERR_SYNTAX, "NULL AST node");

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        return cxpr_fv_double(ast->data.number.value);

    case CXPR_NODE_BOOL:
        return cxpr_fv_bool(ast->data.boolean.value);

    case CXPR_NODE_IDENTIFIER: {
        bool found = false;
        double value = cxpr_context_get(ctx, ast->data.identifier.name, &found);
        if (!found) {
            return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown identifier");
        }
        return cxpr_fv_double(value);
    }

    case CXPR_NODE_VARIABLE: {
        bool found = false;
        double value = cxpr_context_get_param(ctx, ast->data.variable.name, &found);
        if (!found) {
            return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                   "Unknown parameter variable");
        }
        return cxpr_fv_double(value);
    }

    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_eval_field_access(ast, ctx, reg, err);

    case CXPR_NODE_CHAIN_ACCESS:
        return cxpr_eval_chain_access(ast, ctx, err);

    case CXPR_NODE_BINARY_OP: {
        int op = ast->data.binary_op.op;

        if (op == CXPR_TOK_AND || op == CXPR_TOK_OR) {
            cxpr_field_value left = cxpr_eval_node(ast->data.binary_op.left, ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_require_type(left, CXPR_FIELD_BOOL, err, "Logical operators require bool")) {
                return cxpr_fv_double(NAN);
            }

            if (op == CXPR_TOK_AND && !left.b) return cxpr_fv_bool(false);
            if (op == CXPR_TOK_OR && left.b) return cxpr_fv_bool(true);

            cxpr_field_value right = cxpr_eval_node(ast->data.binary_op.right, ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_require_type(right, CXPR_FIELD_BOOL, err,
                                   "Logical operators require bool")) {
                return cxpr_fv_double(NAN);
            }
            return cxpr_fv_bool(op == CXPR_TOK_AND ? right.b : right.b);
        }

        {
            cxpr_field_value left = cxpr_eval_node(ast->data.binary_op.left, ctx, reg, err);
            cxpr_field_value right;
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            right = cxpr_eval_node(ast->data.binary_op.right, ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);

            switch (op) {
            case CXPR_TOK_PLUS:
            case CXPR_TOK_MINUS:
            case CXPR_TOK_STAR:
            case CXPR_TOK_SLASH:
            case CXPR_TOK_PERCENT:
            case CXPR_TOK_POWER:
                if (!cxpr_require_type(left, CXPR_FIELD_DOUBLE, err,
                                       "Arithmetic requires double operands") ||
                    !cxpr_require_type(right, CXPR_FIELD_DOUBLE, err,
                                       "Arithmetic requires double operands")) {
                    return cxpr_fv_double(NAN);
                }
                if ((op == CXPR_TOK_SLASH || op == CXPR_TOK_PERCENT) && right.d == 0.0) {
                    return cxpr_eval_error(err, CXPR_ERR_DIVISION_BY_ZERO,
                                           op == CXPR_TOK_SLASH ? "Division by zero"
                                                                : "Modulo by zero");
                }
                if (op == CXPR_TOK_PLUS) return cxpr_fv_double(left.d + right.d);
                if (op == CXPR_TOK_MINUS) return cxpr_fv_double(left.d - right.d);
                if (op == CXPR_TOK_STAR) return cxpr_fv_double(left.d * right.d);
                if (op == CXPR_TOK_SLASH) return cxpr_fv_double(left.d / right.d);
                if (op == CXPR_TOK_PERCENT) return cxpr_fv_double(fmod(left.d, right.d));
                return cxpr_fv_double(pow(left.d, right.d));

            case CXPR_TOK_LT:
            case CXPR_TOK_LTE:
            case CXPR_TOK_GT:
            case CXPR_TOK_GTE:
                if (!cxpr_require_type(left, CXPR_FIELD_DOUBLE, err,
                                       "Comparison requires double operands") ||
                    !cxpr_require_type(right, CXPR_FIELD_DOUBLE, err,
                                       "Comparison requires double operands")) {
                    return cxpr_fv_double(NAN);
                }
                if (op == CXPR_TOK_LT) return cxpr_fv_bool(left.d < right.d);
                if (op == CXPR_TOK_LTE) return cxpr_fv_bool(left.d <= right.d);
                if (op == CXPR_TOK_GT) return cxpr_fv_bool(left.d > right.d);
                return cxpr_fv_bool(left.d >= right.d);

            case CXPR_TOK_EQ:
            case CXPR_TOK_NEQ:
                if (left.type != right.type ||
                    (left.type != CXPR_FIELD_DOUBLE && left.type != CXPR_FIELD_BOOL)) {
                    return cxpr_eval_error(err, CXPR_ERR_TYPE_MISMATCH,
                                           "Equality requires matching scalar types");
                }
                if (left.type == CXPR_FIELD_DOUBLE) {
                    return cxpr_fv_bool(op == CXPR_TOK_EQ ? (left.d == right.d)
                                                          : (left.d != right.d));
                }
                return cxpr_fv_bool(op == CXPR_TOK_EQ ? (left.b == right.b)
                                                      : (left.b != right.b));

            default:
                return cxpr_eval_error(err, CXPR_ERR_SYNTAX, "Unknown binary operator");
            }
        }
    }

    case CXPR_NODE_UNARY_OP: {
        cxpr_field_value operand = cxpr_eval_node(ast->data.unary_op.operand, ctx, reg, err);
        if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);

        switch (ast->data.unary_op.op) {
        case CXPR_TOK_MINUS:
            if (!cxpr_require_type(operand, CXPR_FIELD_DOUBLE, err,
                                   "Unary minus requires double")) {
                return cxpr_fv_double(NAN);
            }
            return cxpr_fv_double(-operand.d);
        case CXPR_TOK_NOT:
            if (!cxpr_require_type(operand, CXPR_FIELD_BOOL, err,
                                   "Logical not requires bool")) {
                return cxpr_fv_double(NAN);
            }
            return cxpr_fv_bool(!operand.b);
        default:
            return cxpr_eval_error(err, CXPR_ERR_SYNTAX, "Unknown unary operator");
        }
    }

    case CXPR_NODE_FUNCTION_CALL: {
        const char* name = ast->data.function_call.name;
        size_t argc = ast->data.function_call.argc;
        cxpr_func_entry* entry = cxpr_eval_cached_function_entry(ast, reg);

        if (!entry) return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_FUNCTION, "Unknown function");
        if (entry->defined_body) return cxpr_eval_defined_function(entry, ast, ctx, reg, err);
        if (entry->struct_producer) {
            const cxpr_struct_value* produced =
                cxpr_eval_struct_result(entry, name,
                                        (const cxpr_ast* const*)ast->data.function_call.args,
                                        argc, ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            return cxpr_fv_struct((cxpr_struct_value*)produced);
        }

        if (entry->struct_fields) {
            double args[32];
            size_t out = 0;

            if (argc != entry->struct_argc) {
                return cxpr_eval_error(err, CXPR_ERR_WRONG_ARITY,
                                       "Wrong number of struct arguments");
            }

            for (size_t i = 0; i < entry->struct_argc && out < 32; i++) {
                const cxpr_ast* arg = ast->data.function_call.args[i];
                if (arg->type != CXPR_NODE_IDENTIFIER) {
                    return cxpr_eval_error(err, CXPR_ERR_SYNTAX,
                                           "Struct argument must be an identifier");
                }
                for (size_t f = 0; f < entry->fields_per_arg && out < 32; f++) {
                    bool found = false;
                    cxpr_field_value value =
                        cxpr_context_get_field(ctx, arg->data.identifier.name,
                                               entry->struct_fields[f], &found);
                    if (!found) {
                        return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                               "Unknown struct field");
                    }
                    if (!cxpr_require_type(value, CXPR_FIELD_DOUBLE, err,
                                           "Struct function arguments must be scalar doubles")) {
                        return cxpr_fv_double(NAN);
                    }
                    args[out++] = value.d;
                }
            }

            return cxpr_fv_double(entry->sync_func(args, out, entry->userdata));
        }

        if (strcmp(name, "if") == 0 && argc == 3) {
            cxpr_field_value cond = cxpr_eval_node(ast->data.function_call.args[0], ctx, reg, err);
            cxpr_field_value a;
            cxpr_field_value b;
            bool take_true;
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);

            if (cond.type == CXPR_FIELD_BOOL) {
                take_true = cond.b;
            } else if (cond.type == CXPR_FIELD_DOUBLE) {
                take_true = (cond.d != 0.0);
            } else {
                return cxpr_eval_error(err, CXPR_ERR_TYPE_MISMATCH,
                                       "if() condition must be bool or double");
            }

            a = cxpr_eval_node(ast->data.function_call.args[1], ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            b = cxpr_eval_node(ast->data.function_call.args[2], ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_require_type(a, CXPR_FIELD_DOUBLE, err, "if() branches must be double") ||
                !cxpr_require_type(b, CXPR_FIELD_DOUBLE, err, "if() branches must be double")) {
                return cxpr_fv_double(NAN);
            }
            return take_true ? a : b;
        }

        if (argc < entry->min_args || argc > entry->max_args) {
            return cxpr_eval_error(err, CXPR_ERR_WRONG_ARITY, "Wrong number of arguments");
        }

        if (entry->native_kind == CXPR_NATIVE_KIND_NULLARY && argc == 0) {
            return cxpr_fv_double(entry->native_scalar.nullary());
        }
        if (entry->native_kind == CXPR_NATIVE_KIND_UNARY && argc == 1) {
            double a = cxpr_eval_scalar_arg(ast->data.function_call.args[0], ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            return cxpr_fv_double(entry->native_scalar.unary(a));
        }
        if (entry->native_kind == CXPR_NATIVE_KIND_BINARY && argc == 2) {
            double a = cxpr_eval_scalar_arg(ast->data.function_call.args[0], ctx, reg, err);
            double b = cxpr_eval_scalar_arg(ast->data.function_call.args[1], ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            return cxpr_fv_double(entry->native_scalar.binary(a, b));
        }
        if (entry->native_kind == CXPR_NATIVE_KIND_TERNARY && argc == 3) {
            double a = cxpr_eval_scalar_arg(ast->data.function_call.args[0], ctx, reg, err);
            double b = cxpr_eval_scalar_arg(ast->data.function_call.args[1], ctx, reg, err);
            double c = cxpr_eval_scalar_arg(ast->data.function_call.args[2], ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            return cxpr_fv_double(entry->native_scalar.ternary(a, b, c));
        }

        {
            double args[32];
            if (argc > 32) argc = 32;
            for (size_t i = 0; i < argc; i++) {
                args[i] = cxpr_eval_scalar_arg(ast->data.function_call.args[i], ctx, reg, err);
                if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            }
            return cxpr_fv_double(entry->sync_func(args, argc, entry->userdata));
        }
    }

    case CXPR_NODE_PRODUCER_ACCESS: {
        cxpr_func_entry* entry = cxpr_registry_find(reg, ast->data.producer_access.name);
        cxpr_field_value value;
        if (!entry || !entry->struct_producer) {
            return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_FUNCTION, "Unknown function");
        }
        value = cxpr_eval_struct_producer(entry, ast->data.producer_access.name,
                                          ast->data.producer_access.field,
                                          (const cxpr_ast* const*)ast->data.producer_access.args,
                                          ast->data.producer_access.argc,
                                          ctx, reg, err);
        if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
        return value;
    }

    case CXPR_NODE_TERNARY: {
        cxpr_field_value condition = cxpr_eval_node(ast->data.ternary.condition, ctx, reg, err);
        if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
        if (!cxpr_require_type(condition, CXPR_FIELD_BOOL, err,
                               "Ternary condition must be bool")) {
            return cxpr_fv_double(NAN);
        }

        if (condition.b) {
            return cxpr_eval_node(ast->data.ternary.true_branch, ctx, reg, err);
        }
        return cxpr_eval_node(ast->data.ternary.false_branch, ctx, reg, err);
    }
    }

    return cxpr_eval_error(err, CXPR_ERR_SYNTAX, "Unknown AST node type");
}

cxpr_field_value cxpr_ast_eval(const cxpr_ast* ast, const cxpr_context* ctx,
                               const cxpr_registry* reg, cxpr_error* err) {
    if (err) *err = (cxpr_error){0};
    return cxpr_eval_node(ast, ctx, reg, err);
}

double cxpr_ast_eval_double(const cxpr_ast* ast, const cxpr_context* ctx,
                            const cxpr_registry* reg, cxpr_error* err) {
    cxpr_field_value value = cxpr_ast_eval(ast, ctx, reg, err);
    if (err && err->code != CXPR_OK) return NAN;
    if (value.type != CXPR_FIELD_DOUBLE) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expression did not evaluate to double";
        }
        return NAN;
    }
    return value.d;
}

bool cxpr_ast_eval_bool(const cxpr_ast* ast, const cxpr_context* ctx,
                        const cxpr_registry* reg, cxpr_error* err) {
    cxpr_field_value value = cxpr_ast_eval(ast, ctx, reg, err);
    if (err && err->code != CXPR_OK) return false;
    if (value.type != CXPR_FIELD_BOOL) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expression did not evaluate to bool";
        }
        return false;
    }
    return value.b;
}
