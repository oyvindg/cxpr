/**
 * @file ir.c
 * @brief Shared IR helpers and public compiled-program scaffolding.
 */

#include "ir/internal.h"
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

bool cxpr_ir_constant_value(const cxpr_ast* ast, double* out) {
    double left, right;

    if (!ast || !out) return false;

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        *out = ast->data.number.value;
        return true;

    case CXPR_NODE_BOOL:
    case CXPR_NODE_CHAIN_ACCESS:
        return false;

    case CXPR_NODE_UNARY_OP:
        if (!cxpr_ir_constant_value(ast->data.unary_op.operand, out)) return false;
        if (ast->data.unary_op.op == CXPR_TOK_MINUS) {
            *out = -*out;
            return true;
        }
        return false;

    case CXPR_NODE_BINARY_OP:
        if (!cxpr_ir_constant_value(ast->data.binary_op.left, &left)) return false;
        if (!cxpr_ir_constant_value(ast->data.binary_op.right, &right)) return false;
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

    case CXPR_NODE_TERNARY:
        if (!cxpr_ir_constant_value(ast->data.ternary.condition, &left)) return false;
        if (left != 0.0) return cxpr_ir_constant_value(ast->data.ternary.true_branch, out);
        return cxpr_ir_constant_value(ast->data.ternary.false_branch, out);

    default:
        return false;
    }
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
            if (!cxpr_ir_ast_equal(left->data.function_call.args[i],
                                   right->data.function_call.args[i])) {
                return false;
            }
        }
        return true;

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

cxpr_field_value cxpr_ir_runtime_error(cxpr_error* err, const char* message) {
    if (err) {
        err->code = CXPR_ERR_SYNTAX;
        err->message = message;
    }
    return cxpr_fv_double(NAN);
}

bool cxpr_ir_stack_push(cxpr_field_value* stack, size_t* sp, cxpr_field_value value,
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

bool cxpr_ir_require_type(cxpr_field_value value, cxpr_field_type type,
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

cxpr_field_value cxpr_ir_make_not_found(cxpr_error* err, const char* message) {
    if (err) {
        err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
        err->message = message;
    }
    return cxpr_fv_double(NAN);
}

static bool cxpr_ir_scalar_stack_effect(const cxpr_ir_instr* instr, size_t* pops, size_t* pushes) {
    if (!instr || !pops || !pushes) return false;

    switch (instr->op) {
    case CXPR_OP_PUSH_CONST:
    case CXPR_OP_PUSH_BOOL:
    case CXPR_OP_LOAD_LOCAL:
    case CXPR_OP_LOAD_LOCAL_SQUARE:
    case CXPR_OP_LOAD_VAR:
    case CXPR_OP_LOAD_VAR_SQUARE:
    case CXPR_OP_LOAD_PARAM:
    case CXPR_OP_LOAD_PARAM_SQUARE:
        *pops = 0;
        *pushes = 1;
        return true;
    case CXPR_OP_ADD:
    case CXPR_OP_SUB:
    case CXPR_OP_MUL:
    case CXPR_OP_DIV:
    case CXPR_OP_MOD:
    case CXPR_OP_POW:
    case CXPR_OP_CMP_EQ:
    case CXPR_OP_CMP_NEQ:
    case CXPR_OP_CMP_LT:
    case CXPR_OP_CMP_LTE:
    case CXPR_OP_CMP_GT:
    case CXPR_OP_CMP_GTE:
    case CXPR_OP_CALL_BINARY:
        *pops = 2;
        *pushes = 1;
        return true;
    case CXPR_OP_CLAMP:
    case CXPR_OP_CALL_TERNARY:
        *pops = 3;
        *pushes = 1;
        return true;
    case CXPR_OP_SQUARE:
    case CXPR_OP_NOT:
    case CXPR_OP_NEG:
    case CXPR_OP_SIGN:
    case CXPR_OP_SQRT:
    case CXPR_OP_ABS:
    case CXPR_OP_FLOOR:
    case CXPR_OP_CEIL:
    case CXPR_OP_ROUND:
    case CXPR_OP_CALL_UNARY:
    case CXPR_OP_GET_FIELD:
        *pops = 1;
        *pushes = 1;
        return true;
    case CXPR_OP_CALL_PRODUCER_CONST_FIELD:
        if (instr->index > 32) return false;
        *pops = 0;
        *pushes = 1;
        return true;
    case CXPR_OP_CALL_FUNC:
    case CXPR_OP_CALL_DEFINED:
        if (instr->index > 32) return false;
        *pops = instr->index;
        *pushes = 1;
        return true;
    case CXPR_OP_JUMP_IF_FALSE:
    case CXPR_OP_JUMP_IF_TRUE:
        *pops = 1;
        *pushes = 0;
        return true;
    case CXPR_OP_JUMP:
        *pops = 0;
        *pushes = 0;
        return true;
    case CXPR_OP_RETURN:
        *pops = 1;
        *pushes = 0;
        return true;
    default:
        return false;
    }
}

bool cxpr_ir_validate_scalar_fast_program(const cxpr_ir_program* program) {
    size_t depths[256];
    size_t worklist[256];
    size_t work_count = 0;

    if (!program || !program->code || program->count == 0 || program->count > 256) {
        return false;
    }

    for (size_t i = 0; i < program->count; ++i) depths[i] = SIZE_MAX;
    depths[0] = 0;
    worklist[work_count++] = 0;

    while (work_count > 0) {
        size_t ip = worklist[--work_count];
        const cxpr_ir_instr* instr = &program->code[ip];
        size_t pops = 0;
        size_t pushes = 0;
        size_t depth = depths[ip];
        size_t next_depth;

        if (!cxpr_ir_scalar_stack_effect(instr, &pops, &pushes) || depth < pops) {
            return false;
        }

        next_depth = depth - pops + pushes;
        if (next_depth > CXPR_IR_STACK_CAPACITY) return false;

        if (instr->op == CXPR_OP_RETURN) {
            if (depth != 1) return false;
            continue;
        }

        if (instr->op == CXPR_OP_JUMP) {
            if (instr->index >= program->count) return false;
            if (depths[instr->index] == SIZE_MAX) {
                depths[instr->index] = next_depth;
                worklist[work_count++] = instr->index;
            } else if (depths[instr->index] != next_depth) {
                return false;
            }
            continue;
        }

        if (instr->op == CXPR_OP_JUMP_IF_FALSE || instr->op == CXPR_OP_JUMP_IF_TRUE) {
            if (instr->index >= program->count || ip + 1 >= program->count) return false;
            if (depths[instr->index] == SIZE_MAX) {
                depths[instr->index] = next_depth;
                worklist[work_count++] = instr->index;
            } else if (depths[instr->index] != next_depth) {
                return false;
            }
        }

        if (ip + 1 >= program->count) return false;
        if (depths[ip + 1] == SIZE_MAX) {
            depths[ip + 1] = next_depth;
            worklist[work_count++] = ip + 1;
        } else if (depths[ip + 1] != next_depth) {
            return false;
        }
    }

    return true;
}

const char* cxpr_ir_build_struct_cache_key(const char* name, const double* args, size_t argc,
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

char* cxpr_ir_build_constant_producer_key(const char* name, const cxpr_ast* const* args,
                                          size_t argc) {
    double values[32];
    char local_buf[256];
    char* heap_buf = NULL;
    const char* key;

    if (!name || argc > 32) return NULL;
    for (size_t i = 0; i < argc; ++i) {
        if (!cxpr_ir_constant_value(args[i], &values[i])) return NULL;
    }

    key = cxpr_ir_build_struct_cache_key(name, values, argc, local_buf, sizeof(local_buf), &heap_buf);
    if (!key) return NULL;
    if (heap_buf) return heap_buf;
    return cxpr_strdup(key);
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

    while (current && current != owner_ctx) {
        fingerprint ^= cxpr_ir_lookup_version(current, param_lookup) + 0x9e3779b97f4a7c15UL +
                       (fingerprint << 6) + (fingerprint >> 2);
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
    cxpr_field_value typed;

    if (!param_lookup) {
        typed = cxpr_context_get_typed(ctx, instr->name, found);
        if (found && *found) {
            if (typed.type == CXPR_FIELD_DOUBLE) return typed.d;
            if (typed.type == CXPR_FIELD_BOOL) return typed.b ? 1.0 : 0.0;
            *found = false;
            return 0.0;
        }
    }

    if (cache && cache->request_ctx == ctx && cache->owner_ctx == ctx &&
        cache->entries_base == map_entries) {
        if (found) *found = true;
        return cache->entries_base[cache->slot].value;
    }

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
        if (entry) {
            if (cache) {
                cache->request_ctx = ctx;
                cache->owner_ctx = current;
                cache->entries_base = cur_map->entries;
                cache->slot = (size_t)(entry - cur_map->entries);
                cache->shadow_version =
                    (current == ctx) ? 0UL
                                     : cxpr_ir_lookup_shadow_version(ctx, current, param_lookup);
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
