/**
 * @file ir_program.c
 * @brief Shared IR helpers and public compiled-program scaffolding.
 */

#include "expression/internal.h"
#include "internal.h"
#include "ast/internal.h"
#include "context/internal.h"
#include <math.h>
#include <stdint.h>

bool cxpr_ir_is_special_builtin_name(const char* name) {
    if (!name) return false;
    return strcmp(name, "if") == 0 ||
           strcmp(name, "sqrt") == 0 ||
           strcmp(name, "abs") == 0 ||
           strcmp(name, "pow") == 0 ||
           strcmp(name, "sign") == 0 ||
           strcmp(name, "floor") == 0 ||
           strcmp(name, "ceil") == 0 ||
           strcmp(name, "round") == 0 ||
           strcmp(name, "clamp") == 0;
}

void cxpr_ir_program_reset(cxpr_ir_program* program) {
    if (!program) return;
    for (size_t i = 0; i < program->count; ++i) {
        if (program->code[i].op == CXPR_OP_CALL_PRODUCER_CONST ||
            program->code[i].op == CXPR_OP_CALL_PRODUCER_CONST_FIELD) {
            free((char*)program->code[i].name);
        }
        if (program->code[i].op == CXPR_OP_CALL_PRODUCER_CONST_FIELD) {
            free((void*)program->code[i].payload);
        }
    }
    free(program->code);
    free(program->lookup_cache);
    program->code = NULL;
    program->lookup_cache = NULL;
    program->count = 0;
    program->capacity = 0;
    program->ast = NULL;
    program->fast_result_kind = CXPR_IR_RESULT_UNKNOWN;
}

bool cxpr_ir_emit(cxpr_ir_program* program, cxpr_ir_instr instr, cxpr_error* err) {
    if (program->count == program->capacity) {
        if (program->capacity > SIZE_MAX / 2) {
            if (err) { err->code = CXPR_ERR_OUT_OF_MEMORY; err->message = "Out of memory"; }
            return false;
        }
        size_t new_capacity = (program->capacity == 0) ? 8 : program->capacity * 2;
        cxpr_ir_instr* new_code =
            (cxpr_ir_instr*)realloc(program->code, new_capacity * sizeof(cxpr_ir_instr));
        if (!new_code) {
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return false;
        }
        program->code = new_code;
        program->capacity = new_capacity;
    }

    program->code[program->count++] = instr;
    return true;
}

const char* cxpr_ir_opcode_name(cxpr_opcode op) {
    switch (op) {
    case CXPR_OP_PUSH_CONST: return "PUSH_CONST";
    case CXPR_OP_PUSH_BOOL: return "PUSH_BOOL";
    case CXPR_OP_LOAD_LOCAL: return "LOAD_LOCAL";
    case CXPR_OP_LOAD_LOCAL_SQUARE: return "LOAD_LOCAL_SQUARE";
    case CXPR_OP_LOAD_VAR: return "LOAD_VAR";
    case CXPR_OP_LOAD_VAR_SQUARE: return "LOAD_VAR_SQUARE";
    case CXPR_OP_LOAD_PARAM: return "LOAD_PARAM";
    case CXPR_OP_LOAD_PARAM_SQUARE: return "LOAD_PARAM_SQUARE";
    case CXPR_OP_LOAD_FIELD: return "LOAD_FIELD";
    case CXPR_OP_LOAD_FIELD_SQUARE: return "LOAD_FIELD_SQUARE";
    case CXPR_OP_LOAD_CHAIN: return "LOAD_CHAIN";
    case CXPR_OP_ADD: return "ADD";
    case CXPR_OP_SUB: return "SUB";
    case CXPR_OP_MUL: return "MUL";
    case CXPR_OP_SQUARE: return "SQUARE";
    case CXPR_OP_DIV: return "DIV";
    case CXPR_OP_MOD: return "MOD";
    case CXPR_OP_CMP_EQ: return "CMP_EQ";
    case CXPR_OP_CMP_NEQ: return "CMP_NEQ";
    case CXPR_OP_CMP_LT: return "CMP_LT";
    case CXPR_OP_CMP_LTE: return "CMP_LTE";
    case CXPR_OP_CMP_GT: return "CMP_GT";
    case CXPR_OP_CMP_GTE: return "CMP_GTE";
    case CXPR_OP_NOT: return "NOT";
    case CXPR_OP_NEG: return "NEG";
    case CXPR_OP_SIGN: return "SIGN";
    case CXPR_OP_SQRT: return "SQRT";
    case CXPR_OP_ABS: return "ABS";
    case CXPR_OP_FLOOR: return "FLOOR";
    case CXPR_OP_CEIL: return "CEIL";
    case CXPR_OP_ROUND: return "ROUND";
    case CXPR_OP_POW: return "POW";
    case CXPR_OP_CLAMP: return "CLAMP";
    case CXPR_OP_CALL_PRODUCER: return "CALL_PRODUCER";
    case CXPR_OP_CALL_PRODUCER_CONST: return "CALL_PRODUCER_CONST";
    case CXPR_OP_CALL_PRODUCER_CONST_FIELD: return "CALL_PRODUCER_CONST_FIELD";
    case CXPR_OP_GET_FIELD: return "GET_FIELD";
    case CXPR_OP_CALL_UNARY: return "CALL_UNARY";
    case CXPR_OP_CALL_BINARY: return "CALL_BINARY";
    case CXPR_OP_CALL_TERNARY: return "CALL_TERNARY";
    case CXPR_OP_CALL_FUNC: return "CALL_FUNC";
    case CXPR_OP_CALL_DEFINED: return "CALL_DEFINED";
    case CXPR_OP_CALL_AST: return "CALL_AST";
    case CXPR_OP_JUMP: return "JUMP";
    case CXPR_OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
    case CXPR_OP_JUMP_IF_TRUE: return "JUMP_IF_TRUE";
    case CXPR_OP_RETURN: return "RETURN";
    default: return "UNKNOWN";
    }
}

size_t cxpr_ir_next_index(const cxpr_ir_program* program) {
    return program->count;
}

void cxpr_ir_patch_target(cxpr_ir_program* program, size_t at, size_t target) {
    if (!program || at >= program->count) return;
    program->code[at].index = target;
}

bool cxpr_ir_constant_typed_value(const cxpr_ast* ast, const cxpr_registry* reg,
                                  cxpr_value* out) {
    cxpr_value left, right;
    double numeric;

    if (!ast || !out) return false;

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        *out = cxpr_fv_double(ast->data.number.value);
        return true;

    case CXPR_NODE_BOOL:
        *out = cxpr_fv_bool(ast->data.boolean.value);
        return true;

    case CXPR_NODE_CHAIN_ACCESS:
    case CXPR_NODE_LOOKBACK:
        return false;

    case CXPR_NODE_UNARY_OP:
        if (!cxpr_ir_constant_typed_value(ast->data.unary_op.operand, reg, &left)) return false;
        if (ast->data.unary_op.op == CXPR_TOK_MINUS) {
            if (left.type != CXPR_VALUE_NUMBER) return false;
            *out = cxpr_fv_double(-left.d);
            return true;
        }
        if (ast->data.unary_op.op == CXPR_TOK_NOT) {
            if (left.type != CXPR_VALUE_BOOL) return false;
            *out = cxpr_fv_bool(!left.b);
            return true;
        }
        return false;

    case CXPR_NODE_BINARY_OP:
        if (!cxpr_ir_constant_typed_value(ast->data.binary_op.left, reg, &left)) return false;
        if (!cxpr_ir_constant_typed_value(ast->data.binary_op.right, reg, &right)) return false;
        switch (ast->data.binary_op.op) {
        case CXPR_TOK_PLUS:
        case CXPR_TOK_MINUS:
        case CXPR_TOK_STAR:
        case CXPR_TOK_SLASH:
        case CXPR_TOK_PERCENT:
        case CXPR_TOK_POWER:
            if (left.type != CXPR_VALUE_NUMBER || right.type != CXPR_VALUE_NUMBER) return false;
            break;
        case CXPR_TOK_LT:
        case CXPR_TOK_LTE:
        case CXPR_TOK_GT:
        case CXPR_TOK_GTE:
            if (left.type != CXPR_VALUE_NUMBER || right.type != CXPR_VALUE_NUMBER) return false;
            break;
        case CXPR_TOK_AND:
        case CXPR_TOK_OR:
            if (left.type != CXPR_VALUE_BOOL || right.type != CXPR_VALUE_BOOL) return false;
            break;
        case CXPR_TOK_EQ:
        case CXPR_TOK_NEQ:
            if (left.type != right.type ||
                (left.type != CXPR_VALUE_NUMBER && left.type != CXPR_VALUE_BOOL)) {
                return false;
            }
            break;
        default:
            return false;
        }

        switch (ast->data.binary_op.op) {
        case CXPR_TOK_PLUS: *out = cxpr_fv_double(left.d + right.d); return true;
        case CXPR_TOK_MINUS: *out = cxpr_fv_double(left.d - right.d); return true;
        case CXPR_TOK_STAR: *out = cxpr_fv_double(left.d * right.d); return true;
        case CXPR_TOK_SLASH:
            if (right.d == 0.0) return false;
            *out = cxpr_fv_double(left.d / right.d);
            return true;
        case CXPR_TOK_PERCENT:
            if (right.d == 0.0) return false;
            *out = cxpr_fv_double(fmod(left.d, right.d));
            return true;
        case CXPR_TOK_POWER:
            numeric = pow(left.d, right.d);
            if (!isfinite(numeric)) return false;
            *out = cxpr_fv_double(numeric);
            return true;
        case CXPR_TOK_EQ:
            *out = left.type == CXPR_VALUE_NUMBER
                       ? cxpr_fv_bool(left.d == right.d)
                       : cxpr_fv_bool(left.b == right.b);
            return true;
        case CXPR_TOK_NEQ:
            *out = left.type == CXPR_VALUE_NUMBER
                       ? cxpr_fv_bool(left.d != right.d)
                       : cxpr_fv_bool(left.b != right.b);
            return true;
        case CXPR_TOK_LT: *out = cxpr_fv_bool(left.d < right.d); return true;
        case CXPR_TOK_LTE: *out = cxpr_fv_bool(left.d <= right.d); return true;
        case CXPR_TOK_GT: *out = cxpr_fv_bool(left.d > right.d); return true;
        case CXPR_TOK_GTE: *out = cxpr_fv_bool(left.d >= right.d); return true;
        case CXPR_TOK_AND: *out = cxpr_fv_bool(left.b && right.b); return true;
        case CXPR_TOK_OR: *out = cxpr_fv_bool(left.b || right.b); return true;
        default:
            return false;
        }

    case CXPR_NODE_FUNCTION_CALL: {
        double args[CXPR_MAX_CALL_ARGS];
        const cxpr_func_entry* entry;
        size_t argc = ast->data.function_call.argc;

        if (!reg || cxpr_ast_call_uses_named_args(ast) || argc > CXPR_MAX_CALL_ARGS) return false;

        if (strcmp(ast->data.function_call.name, "if") == 0 && argc == 3u) {
            cxpr_value condition;
            if (!cxpr_ir_constant_typed_value(ast->data.function_call.args[0], reg, &condition)) {
                return false;
            }
            if (condition.type != CXPR_VALUE_BOOL) return false;
            return cxpr_ir_constant_typed_value(
                condition.b ? ast->data.function_call.args[1] : ast->data.function_call.args[2],
                reg,
                out);
        }

        entry = cxpr_registry_find(reg, ast->data.function_call.name);
        if (!entry || !entry->sync_func || entry->value_func || entry->typed_func ||
            entry->ast_func || entry->ast_func_overlay || entry->struct_producer ||
            entry->struct_fields || entry->defined_body ||
            argc < entry->min_args || argc > entry->max_args) {
            return false;
        }

        for (size_t i = 0; i < argc; ++i) {
            if (!cxpr_ir_constant_value(ast->data.function_call.args[i], reg, &args[i])) {
                return false;
            }
        }

        numeric = entry->sync_func(args, argc, entry->userdata);
        if (!isfinite(numeric)) return false;
        *out = cxpr_fv_double(numeric);
        return true;
    }

    case CXPR_NODE_TERNARY:
        if (!cxpr_ir_constant_typed_value(ast->data.ternary.condition, reg, &left)) return false;
        if (left.type != CXPR_VALUE_BOOL) return false;
        return cxpr_ir_constant_typed_value(left.b ? ast->data.ternary.true_branch
                                                   : ast->data.ternary.false_branch,
                                            reg, out);

    default:
        return false;
    }
}

bool cxpr_ir_constant_value(const cxpr_ast* ast, const cxpr_registry* reg, double* out) {
    cxpr_value value;

    if (!out) return false;
    if (!cxpr_ir_constant_typed_value(ast, reg, &value)) return false;
    if (value.type != CXPR_VALUE_NUMBER) return false;
    *out = value.d;
    return true;
}

bool cxpr_ir_ast_equal(const cxpr_ast* left, const cxpr_ast* right) {
    size_t i;

    if (left == right) return true;
    if (!left || !right || left->type != right->type) return false;

    switch (left->type) {
    case CXPR_NODE_NUMBER:
        return left->data.number.value == right->data.number.value;

    case CXPR_NODE_BOOL:
        return left->data.boolean.value == right->data.boolean.value;

    case CXPR_NODE_IDENTIFIER:
        return strcmp(left->data.identifier.name, right->data.identifier.name) == 0;

    case CXPR_NODE_VARIABLE:
        return strcmp(left->data.variable.name, right->data.variable.name) == 0;

    case CXPR_NODE_FIELD_ACCESS:
        return strcmp(left->data.field_access.full_key, right->data.field_access.full_key) == 0;

    case CXPR_NODE_CHAIN_ACCESS:
        if (left->data.chain_access.depth != right->data.chain_access.depth) return false;
        for (i = 0; i < left->data.chain_access.depth; ++i) {
            if (strcmp(left->data.chain_access.path[i], right->data.chain_access.path[i]) != 0) {
                return false;
            }
        }
        return true;

    case CXPR_NODE_PRODUCER_ACCESS:
        if (strcmp(left->data.producer_access.name, right->data.producer_access.name) != 0 ||
            strcmp(left->data.producer_access.field, right->data.producer_access.field) != 0 ||
            left->data.producer_access.argc != right->data.producer_access.argc) {
            return false;
        }
        for (i = 0; i < left->data.producer_access.argc; ++i) {
            const char* left_name = left->data.producer_access.arg_names ?
                                    left->data.producer_access.arg_names[i] : NULL;
            const char* right_name = right->data.producer_access.arg_names ?
                                     right->data.producer_access.arg_names[i] : NULL;
            if ((left_name == NULL) != (right_name == NULL)) return false;
            if (left_name && strcmp(left_name, right_name) != 0) return false;
            if (!cxpr_ir_ast_equal(left->data.producer_access.args[i],
                                   right->data.producer_access.args[i])) {
                return false;
            }
        }
        return true;

    case CXPR_NODE_UNARY_OP:
        return left->data.unary_op.op == right->data.unary_op.op &&
               cxpr_ir_ast_equal(left->data.unary_op.operand, right->data.unary_op.operand);

    case CXPR_NODE_BINARY_OP:
        return left->data.binary_op.op == right->data.binary_op.op &&
               cxpr_ir_ast_equal(left->data.binary_op.left, right->data.binary_op.left) &&
               cxpr_ir_ast_equal(left->data.binary_op.right, right->data.binary_op.right);

    case CXPR_NODE_FUNCTION_CALL:
        if (strcmp(left->data.function_call.name, right->data.function_call.name) != 0 ||
            left->data.function_call.argc != right->data.function_call.argc) {
            return false;
        }
        for (i = 0; i < left->data.function_call.argc; ++i) {
            const char* left_name = left->data.function_call.arg_names ?
                                    left->data.function_call.arg_names[i] : NULL;
            const char* right_name = right->data.function_call.arg_names ?
                                     right->data.function_call.arg_names[i] : NULL;
            if ((left_name == NULL) != (right_name == NULL)) return false;
            if (left_name && strcmp(left_name, right_name) != 0) return false;
            if (!cxpr_ir_ast_equal(left->data.function_call.args[i],
                                   right->data.function_call.args[i])) {
                return false;
            }
        }
        return true;

    case CXPR_NODE_LOOKBACK:
        return cxpr_ir_ast_equal(left->data.lookback.target, right->data.lookback.target) &&
               cxpr_ir_ast_equal(left->data.lookback.index, right->data.lookback.index);

    case CXPR_NODE_TERNARY:
        return cxpr_ir_ast_equal(left->data.ternary.condition,
                                 right->data.ternary.condition) &&
               cxpr_ir_ast_equal(left->data.ternary.true_branch,
                                 right->data.ternary.true_branch) &&
               cxpr_ir_ast_equal(left->data.ternary.false_branch,
                                 right->data.ternary.false_branch);

    default:
        return false;
    }
}

cxpr_value cxpr_ir_runtime_error(cxpr_error* err, const char* message) {
    if (err) {
        err->code = CXPR_ERR_SYNTAX;
        err->message = message;
    }
    return cxpr_fv_double(NAN);
}

bool cxpr_ir_stack_push(cxpr_value* stack, size_t* sp, cxpr_value value,
                        size_t capacity, cxpr_error* err) {
    if (*sp >= capacity) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "IR stack overflow";
        }
        return false;
    }
    stack[(*sp)++] = value;
    return true;
}

bool cxpr_ir_require_stack(size_t sp, size_t need, cxpr_error* err) {
    if (sp < need) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "IR stack underflow";
        }
        return false;
    }
    return true;
}

bool cxpr_ir_require_type(cxpr_value value, cxpr_value_type type,
                          cxpr_error* err, const char* message) {
    if (value.type != type) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = message;
        }
        return false;
    }
    return true;
}

cxpr_value cxpr_ir_make_not_found(cxpr_error* err, const char* message) {
    if (err) {
        err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
        err->message = message;
    }
    return cxpr_fv_double(NAN);
}

static unsigned long cxpr_ir_lookup_version(const cxpr_context* ctx, bool param_lookup) {
    if (!ctx) return 0;
    return param_lookup ? ctx->params_version : ctx->variables_version;
}

static unsigned long cxpr_ir_lookup_shadow_version(const cxpr_context* request_ctx,
                                                   const cxpr_context* owner_ctx,
                                                   bool param_lookup) {
    const cxpr_context* current = request_ctx;
    unsigned long fingerprint = 1469598103934665603UL;

    while (current) {
        fingerprint ^= cxpr_ir_lookup_version(current, param_lookup) + 0x9e3779b97f4a7c15UL +
                       (fingerprint << 6) + (fingerprint >> 2);
        if (current == owner_ctx) break;
        current = current->parent;
    }

    return fingerprint;
}

double cxpr_ir_lookup_cached_scalar(const cxpr_context* ctx, const cxpr_ir_instr* instr,
                                    cxpr_ir_lookup_cache* cache, bool param_lookup,
                                    bool* found) {
    cxpr_hashmap_entry* map_entries =
        param_lookup ? ctx->params.entries : ctx->variables.entries;
    const cxpr_context* current;
    unsigned long version;

    /* expression_scope results take priority over context variables */
    if (!param_lookup && ctx->expression_scope) {
        bool scope_found = false;
        cxpr_value typed = cxpr_expression_lookup_typed_result(ctx->expression_scope,
                                                               instr->name, &scope_found);
        if (scope_found) {
            if (found) *found = true;
            if (typed.type == CXPR_VALUE_NUMBER) return typed.d;
            if (typed.type == CXPR_VALUE_BOOL) return typed.b ? 1.0 : 0.0;
            if (found) *found = false;
            return 0.0;
        }
    }

    if (!param_lookup) {
        bool bool_found = false;
        bool bool_value = cxpr_context_get_local_bool(ctx, instr->name, &bool_found);
        if (bool_found) {
            if (cache) {
                cache->request_ctx = NULL;
                cache->owner_ctx = NULL;
                cache->entries_base = NULL;
                cache->slot = 0;
                cache->shadow_version = 0;
            }
            if (found) *found = true;
            return bool_value ? 1.0 : 0.0;
        }
    }

    if ((!param_lookup || ctx->bools.count == 0u) && !ctx->parent && !ctx->expression_scope) {
        version = cxpr_ir_lookup_version(ctx, param_lookup);
        if (cache && cache->request_ctx == ctx && cache->owner_ctx == ctx &&
            cache->entries_base == map_entries && cache->shadow_version == version) {
            if (found) *found = true;
            return cache->entries_base[cache->slot].value;
        }

        {
            const cxpr_hashmap* map = param_lookup ? &ctx->params : &ctx->variables;
            const cxpr_hashmap_entry* entry =
                cxpr_hashmap_find_prehashed_entry(map, instr->name, instr->hash);
            if (entry) {
                if (cache) {
                    cache->request_ctx = ctx;
                    cache->owner_ctx = ctx;
                    cache->entries_base = map->entries;
                    cache->slot = (size_t)(entry - map->entries);
                    cache->shadow_version = version;
                }
                if (found) *found = true;
                return entry->value;
            }
        }

        if (cache) {
            cache->request_ctx = NULL;
            cache->owner_ctx = NULL;
            cache->entries_base = NULL;
            cache->slot = 0;
            cache->shadow_version = 0;
        }
        if (found) *found = false;
        return 0.0;
    }

    /* IR lookup cache: direct entry hit (same ctx, same entries array) */
    if (cache && cache->request_ctx == ctx && cache->owner_ctx == ctx &&
        cache->entries_base == map_entries &&
        cache->shadow_version == cxpr_ir_lookup_shadow_version(ctx, ctx, param_lookup)) {
        if (found) *found = true;
        return cache->entries_base[cache->slot].value;
    }

    /* IR lookup cache: parent-context hit with version fingerprint */
    if (cache && cache->request_ctx == ctx && cache->owner_ctx && cache->entries_base &&
        cache->entries_base ==
            (cxpr_hashmap_entry*)(param_lookup ? cache->owner_ctx->params.entries
                                               : cache->owner_ctx->variables.entries) &&
        cache->shadow_version ==
            cxpr_ir_lookup_shadow_version(ctx, cache->owner_ctx, param_lookup)) {
        if (found) *found = true;
        return cache->entries_base[cache->slot].value;
    }

    current = ctx;
    while (current) {
        const cxpr_hashmap* cur_map =
            param_lookup ? &current->params : &current->variables;
        const cxpr_hashmap_entry* entry =
            cxpr_hashmap_find_prehashed_entry(cur_map, instr->name, instr->hash);
        if (!param_lookup) {
            bool bool_found = false;
            bool bool_value = cxpr_context_get_local_bool(current, instr->name, &bool_found);
            if (bool_found) {
                if (cache) {
                    cache->request_ctx = NULL;
                    cache->owner_ctx = NULL;
                    cache->entries_base = NULL;
                    cache->slot = 0;
                    cache->shadow_version = 0;
                }
                if (found) *found = true;
                return bool_value ? 1.0 : 0.0;
            }
        }
        if (entry) {
            if (cache) {
                cache->request_ctx = ctx;
                cache->owner_ctx = current;
                cache->entries_base = cur_map->entries;
                cache->slot = (size_t)(entry - cur_map->entries);
                cache->shadow_version =
                    cxpr_ir_lookup_shadow_version(ctx, current, param_lookup);
            }
            if (found) *found = true;
            return entry->value;
        }
        current = current->parent;
    }

    if (cache) {
        cache->request_ctx = NULL;
        cache->owner_ctx = NULL;
        cache->entries_base = NULL;
        cache->slot = 0;
        cache->shadow_version = 0;
    }
    if (found) *found = false;
    return 0.0;
}

bool cxpr_ir_defined_is_scalar_only(const cxpr_func_entry* entry) {
    if (!entry || !entry->defined_body) return false;
    for (size_t i = 0; i < entry->defined_param_count; ++i) {
        if (entry->defined_param_fields[i] && entry->defined_param_field_counts[i] > 0) {
            return false;
        }
    }
    return true;
}

cxpr_program* cxpr_compile(const cxpr_ast* ast, const cxpr_registry* reg,
                           cxpr_error* err) {
    (void)reg;
    if (err) *err = (cxpr_error){0};

    cxpr_program* prog = (cxpr_program*)calloc(1, sizeof(cxpr_program));
    if (!prog) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return NULL;
    }

    prog->ast = ast;
    if (!cxpr_ir_compile(ast, reg, &prog->ir, err)) {
        free(prog);
        return NULL;
    }

    return prog;
}

void cxpr_program_free(cxpr_program* prog) {
    if (!prog) return;
    cxpr_ir_program_reset(&prog->ir);
    free(prog);
}

void cxpr_program_dump(const cxpr_program* prog, FILE* out) {
    size_t i;
    FILE* stream = out ? out : stdout;

    if (!prog || !prog->ir.code) {
        fprintf(stream, "<empty program>\n");
        return;
    }

    for (i = 0; i < prog->ir.count; ++i) {
        const cxpr_ir_instr* instr = &prog->ir.code[i];
        fprintf(stream, "%zu: %s", i, cxpr_ir_opcode_name(instr->op));
        if (instr->name) fprintf(stream, " name=%s", instr->name);
        if (instr->aux_name) fprintf(stream, " aux=%s", instr->aux_name);
        if (instr->func) fprintf(stream, " argc=%zu func=%s", instr->index, instr->func->name);
        else if (instr->op == CXPR_OP_PUSH_CONST) fprintf(stream, " value=%.17g", instr->value);
        else if (instr->op == CXPR_OP_JUMP || instr->op == CXPR_OP_JUMP_IF_FALSE ||
                 instr->op == CXPR_OP_JUMP_IF_TRUE || instr->op == CXPR_OP_LOAD_LOCAL ||
                 instr->op == CXPR_OP_LOAD_LOCAL_SQUARE) {
            fprintf(stream, " index=%zu", instr->index);
        }
        fprintf(stream, "\n");
    }
}
