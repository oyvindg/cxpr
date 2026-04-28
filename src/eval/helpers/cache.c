/**
 * @file cache.c
 * @brief Cached lookup and struct-result helpers.
 */

#include "internal.h" // IWYU pragma: keep
#include "../../context/state.h"
#include "limits.h"

static unsigned long cxpr_eval_hash_mix(unsigned long hash, unsigned long value) {
    hash ^= value + 0x9e3779b9UL + (hash << 6) + (hash >> 2);
    return hash;
}

static unsigned long cxpr_eval_hash_string(unsigned long hash, const char* text) {
    const unsigned char* p = (const unsigned char*)(text ? text : "");
    while (*p) {
        hash ^= (unsigned long)(*p++);
        hash *= 1099511628211UL;
    }
    return hash;
}

static int cxpr_eval_opt_string_equal(const char* lhs, const char* rhs) {
    if (lhs == rhs) return 1;
    if (!lhs || !rhs) return 0;
    return strcmp(lhs, rhs) == 0;
}

static int cxpr_eval_arg_names_equal(char** lhs, char** rhs, size_t count) {
    size_t i;
    if (lhs == rhs) return 1;
    for (i = 0u; i < count; ++i) {
        const char* l = lhs ? lhs[i] : NULL;
        const char* r = rhs ? rhs[i] : NULL;
        if (!cxpr_eval_opt_string_equal(l, r)) return 0;
    }
    return 1;
}

unsigned long cxpr_eval_ast_hash(const cxpr_ast* ast) {
    unsigned long hash = 1469598103934665603UL;
    size_t i;

    if (!ast) return 0u;
    hash = cxpr_eval_hash_mix(hash, (unsigned long)ast->type);
    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        {
            union { double d; unsigned long u; } v;
            v.d = ast->data.number.value;
            hash = cxpr_eval_hash_mix(hash, v.u);
        }
        break;
    case CXPR_NODE_BOOL:
        hash = cxpr_eval_hash_mix(hash, ast->data.boolean.value ? 1u : 0u);
        break;
    case CXPR_NODE_STRING:
        hash = cxpr_eval_hash_string(hash, ast->data.string.value);
        break;
    case CXPR_NODE_IDENTIFIER:
        hash = cxpr_eval_hash_string(hash, ast->data.identifier.name);
        break;
    case CXPR_NODE_VARIABLE:
        hash = cxpr_eval_hash_string(hash, ast->data.variable.name);
        break;
    case CXPR_NODE_FIELD_ACCESS:
        hash = cxpr_eval_hash_string(hash, ast->data.field_access.object);
        hash = cxpr_eval_hash_string(hash, ast->data.field_access.field);
        break;
    case CXPR_NODE_CHAIN_ACCESS:
        for (i = 0u; i < ast->data.chain_access.depth; ++i) {
            hash = cxpr_eval_hash_string(hash, ast->data.chain_access.path[i]);
        }
        hash = cxpr_eval_hash_mix(hash, (unsigned long)ast->data.chain_access.depth);
        break;
    case CXPR_NODE_PRODUCER_ACCESS:
        hash = cxpr_eval_hash_string(hash, ast->data.producer_access.name);
        hash = cxpr_eval_hash_string(hash, ast->data.producer_access.field);
        for (i = 0u; i < ast->data.producer_access.argc; ++i) {
            hash = cxpr_eval_hash_string(
                hash,
                ast->data.producer_access.arg_names ? ast->data.producer_access.arg_names[i] : NULL);
            hash = cxpr_eval_hash_mix(hash, cxpr_eval_ast_hash(ast->data.producer_access.args[i]));
        }
        hash = cxpr_eval_hash_mix(hash, (unsigned long)ast->data.producer_access.argc);
        break;
    case CXPR_NODE_BINARY_OP:
        hash = cxpr_eval_hash_mix(hash, (unsigned long)ast->data.binary_op.op);
        hash = cxpr_eval_hash_mix(hash, cxpr_eval_ast_hash(ast->data.binary_op.left));
        hash = cxpr_eval_hash_mix(hash, cxpr_eval_ast_hash(ast->data.binary_op.right));
        break;
    case CXPR_NODE_UNARY_OP:
        hash = cxpr_eval_hash_mix(hash, (unsigned long)ast->data.unary_op.op);
        hash = cxpr_eval_hash_mix(hash, cxpr_eval_ast_hash(ast->data.unary_op.operand));
        break;
    case CXPR_NODE_FUNCTION_CALL:
        hash = cxpr_eval_hash_string(hash, ast->data.function_call.name);
        for (i = 0u; i < ast->data.function_call.argc; ++i) {
            hash = cxpr_eval_hash_string(
                hash,
                ast->data.function_call.arg_names ? ast->data.function_call.arg_names[i] : NULL);
            hash = cxpr_eval_hash_mix(hash, cxpr_eval_ast_hash(ast->data.function_call.args[i]));
        }
        hash = cxpr_eval_hash_mix(hash, (unsigned long)ast->data.function_call.argc);
        break;
    case CXPR_NODE_LOOKBACK:
        hash = cxpr_eval_hash_mix(hash, cxpr_eval_ast_hash(ast->data.lookback.target));
        hash = cxpr_eval_hash_mix(hash, cxpr_eval_ast_hash(ast->data.lookback.index));
        break;
    case CXPR_NODE_TERNARY:
        hash = cxpr_eval_hash_mix(hash, cxpr_eval_ast_hash(ast->data.ternary.condition));
        hash = cxpr_eval_hash_mix(hash, cxpr_eval_ast_hash(ast->data.ternary.true_branch));
        hash = cxpr_eval_hash_mix(hash, cxpr_eval_ast_hash(ast->data.ternary.false_branch));
        break;
    }
    return hash;
}

bool cxpr_eval_ast_equal(const cxpr_ast* lhs, const cxpr_ast* rhs) {
    size_t i;

    if (lhs == rhs) return true;
    if (!lhs || !rhs || lhs->type != rhs->type) return false;
    switch (lhs->type) {
    case CXPR_NODE_NUMBER:
        return lhs->data.number.value == rhs->data.number.value;
    case CXPR_NODE_BOOL:
        return lhs->data.boolean.value == rhs->data.boolean.value;
    case CXPR_NODE_STRING:
        return cxpr_eval_opt_string_equal(lhs->data.string.value, rhs->data.string.value);
    case CXPR_NODE_IDENTIFIER:
        return cxpr_eval_opt_string_equal(lhs->data.identifier.name, rhs->data.identifier.name);
    case CXPR_NODE_VARIABLE:
        return cxpr_eval_opt_string_equal(lhs->data.variable.name, rhs->data.variable.name);
    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_eval_opt_string_equal(lhs->data.field_access.object, rhs->data.field_access.object) &&
               cxpr_eval_opt_string_equal(lhs->data.field_access.field, rhs->data.field_access.field);
    case CXPR_NODE_CHAIN_ACCESS:
        if (lhs->data.chain_access.depth != rhs->data.chain_access.depth) return false;
        for (i = 0u; i < lhs->data.chain_access.depth; ++i) {
            if (!cxpr_eval_opt_string_equal(
                    lhs->data.chain_access.path[i],
                    rhs->data.chain_access.path[i])) return false;
        }
        return true;
    case CXPR_NODE_PRODUCER_ACCESS:
        if (!cxpr_eval_opt_string_equal(lhs->data.producer_access.name, rhs->data.producer_access.name) ||
            !cxpr_eval_opt_string_equal(lhs->data.producer_access.field, rhs->data.producer_access.field) ||
            lhs->data.producer_access.argc != rhs->data.producer_access.argc ||
            !cxpr_eval_arg_names_equal(
                lhs->data.producer_access.arg_names,
                rhs->data.producer_access.arg_names,
                lhs->data.producer_access.argc)) return false;
        for (i = 0u; i < lhs->data.producer_access.argc; ++i) {
            if (!cxpr_eval_ast_equal(
                    lhs->data.producer_access.args[i],
                    rhs->data.producer_access.args[i])) return false;
        }
        return true;
    case CXPR_NODE_BINARY_OP:
        return lhs->data.binary_op.op == rhs->data.binary_op.op &&
               cxpr_eval_ast_equal(lhs->data.binary_op.left, rhs->data.binary_op.left) &&
               cxpr_eval_ast_equal(lhs->data.binary_op.right, rhs->data.binary_op.right);
    case CXPR_NODE_UNARY_OP:
        return lhs->data.unary_op.op == rhs->data.unary_op.op &&
               cxpr_eval_ast_equal(lhs->data.unary_op.operand, rhs->data.unary_op.operand);
    case CXPR_NODE_FUNCTION_CALL:
        if (!cxpr_eval_opt_string_equal(lhs->data.function_call.name, rhs->data.function_call.name) ||
            lhs->data.function_call.argc != rhs->data.function_call.argc ||
            !cxpr_eval_arg_names_equal(
                lhs->data.function_call.arg_names,
                rhs->data.function_call.arg_names,
                lhs->data.function_call.argc)) return false;
        for (i = 0u; i < lhs->data.function_call.argc; ++i) {
            if (!cxpr_eval_ast_equal(
                    lhs->data.function_call.args[i],
                    rhs->data.function_call.args[i])) return false;
        }
        return true;
    case CXPR_NODE_LOOKBACK:
        return cxpr_eval_ast_equal(lhs->data.lookback.target, rhs->data.lookback.target) &&
               cxpr_eval_ast_equal(lhs->data.lookback.index, rhs->data.lookback.index);
    case CXPR_NODE_TERNARY:
        return cxpr_eval_ast_equal(lhs->data.ternary.condition, rhs->data.ternary.condition) &&
               cxpr_eval_ast_equal(lhs->data.ternary.true_branch, rhs->data.ternary.true_branch) &&
               cxpr_eval_ast_equal(lhs->data.ternary.false_branch, rhs->data.ternary.false_branch);
    }
    return false;
}

bool cxpr_eval_ast_memoable(const cxpr_ast* ast, const cxpr_registry* reg) {
    size_t i;

    if (!ast) return false;
    switch (ast->type) {
    case CXPR_NODE_NUMBER:
    case CXPR_NODE_BOOL:
    case CXPR_NODE_IDENTIFIER:
    case CXPR_NODE_VARIABLE:
        return true;
    case CXPR_NODE_STRING:
    case CXPR_NODE_LOOKBACK:
    case CXPR_NODE_PRODUCER_ACCESS:
    case CXPR_NODE_FIELD_ACCESS:
    case CXPR_NODE_CHAIN_ACCESS:
        return false;
    case CXPR_NODE_BINARY_OP:
        return cxpr_eval_ast_memoable(ast->data.binary_op.left, reg) &&
               cxpr_eval_ast_memoable(ast->data.binary_op.right, reg);
    case CXPR_NODE_UNARY_OP:
        return cxpr_eval_ast_memoable(ast->data.unary_op.operand, reg);
    case CXPR_NODE_FUNCTION_CALL: {
        cxpr_func_entry* entry =
            reg ? cxpr_registry_find(reg, ast->data.function_call.name) : NULL;
        if (!entry || entry->ast_func_overlay || entry->ast_func) return false;
        for (i = 0u; i < ast->data.function_call.argc; ++i) {
            if (!cxpr_eval_ast_memoable(ast->data.function_call.args[i], reg)) return false;
        }
        return true;
    }
    case CXPR_NODE_TERNARY:
        return cxpr_eval_ast_memoable(ast->data.ternary.condition, reg) &&
               cxpr_eval_ast_memoable(ast->data.ternary.true_branch, reg) &&
               cxpr_eval_ast_memoable(ast->data.ternary.false_branch, reg);
    }
    return false;
}

bool cxpr_eval_memo_get(const cxpr_context* ctx,
                        const cxpr_ast* ast,
                        unsigned long hash,
                        cxpr_value* out_value) {
    size_t i;

    if (!ctx || !ast || !out_value) return false;
    for (i = 0u; i < ctx->eval_memo.count; ++i) {
        const cxpr_eval_memo_entry* entry = &ctx->eval_memo.entries[i];
        if (entry->hash == hash && cxpr_eval_ast_equal(entry->ast, ast)) {
            *out_value = entry->value;
            return true;
        }
    }
    return false;
}

bool cxpr_eval_memo_set(const cxpr_context* ctx,
                        const cxpr_ast* ast,
                        unsigned long hash,
                        cxpr_value value) {
    cxpr_context* mutable_ctx = (cxpr_context*)ctx;
    cxpr_eval_memo_entry* grown;

    if (!mutable_ctx || !ast) return false;
    if (value.type != CXPR_VALUE_NUMBER && value.type != CXPR_VALUE_BOOL) return true;
    if (mutable_ctx->eval_memo.count == mutable_ctx->eval_memo.capacity) {
        size_t next_cap = mutable_ctx->eval_memo.capacity ? mutable_ctx->eval_memo.capacity * 2u : 64u;
        grown = (cxpr_eval_memo_entry*)realloc(
            mutable_ctx->eval_memo.entries,
            next_cap * sizeof(*grown));
        if (!grown) return false;
        mutable_ctx->eval_memo.entries = grown;
        mutable_ctx->eval_memo.capacity = next_cap;
    }
    mutable_ctx->eval_memo.entries[mutable_ctx->eval_memo.count].ast = ast;
    mutable_ctx->eval_memo.entries[mutable_ctx->eval_memo.count].hash = hash;
    mutable_ctx->eval_memo.entries[mutable_ctx->eval_memo.count].value = value;
    mutable_ctx->eval_memo.count += 1u;
    return true;
}

void cxpr_eval_memo_clear(cxpr_context* ctx) {
    if (!ctx) return;
    ctx->eval_memo.count = 0u;
    ctx->eval_memo.depth = 0u;
}

void cxpr_eval_memo_enter(cxpr_context* ctx) {
    if (!ctx) return;
    if (ctx->eval_memo.depth == 0u) ctx->eval_memo.count = 0u;
    ctx->eval_memo.depth += 1u;
}

void cxpr_eval_memo_leave(cxpr_context* ctx) {
    if (!ctx || ctx->eval_memo.depth == 0u) return;
    ctx->eval_memo.depth -= 1u;
    if (ctx->eval_memo.depth == 0u) ctx->eval_memo.count = 0u;
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
