/**
 * @file ir.c
 * @brief Internal IR / compiled plan support for cxpr.
 *
 * This module starts as internal scaffolding only. The initial goal is to
 * establish a compilation/evaluation unit that can be expanded incrementally
 * without changing the public API.
 */

#include "internal.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>

#define CXPR_IR_INLINE_DEPTH_LIMIT 8
#define CXPR_IR_INFER_DEPTH_LIMIT 32
#define CXPR_IR_RESULT_UNKNOWN 0
#define CXPR_IR_RESULT_DOUBLE 1
#define CXPR_IR_RESULT_BOOL 2
#define CXPR_IR_STACK_CAPACITY 64

typedef struct cxpr_ir_subst_frame {
    const char* const* names;
    const cxpr_ast* const* args;
    size_t count;
    const struct cxpr_ir_subst_frame* parent;
} cxpr_ir_subst_frame;

static double cxpr_ir_context_get_prehashed(const cxpr_context* ctx, const char* name,
                                            unsigned long hash, bool* found);
static double cxpr_ir_context_get_param_prehashed(const cxpr_context* ctx, const char* name,
                                                  unsigned long hash, bool* found);
static cxpr_field_value cxpr_ir_call_defined_scalar(cxpr_func_entry* entry,
                                                    const cxpr_context* ctx,
                                                    const cxpr_registry* reg,
                                                    const cxpr_field_value* args,
                                                    size_t argc, cxpr_error* err);
static cxpr_field_value cxpr_ir_call_producer(cxpr_func_entry* entry, const char* name,
                                              const cxpr_context* ctx,
                                              const cxpr_field_value* stack_args,
                                              size_t argc, cxpr_error* err);
static cxpr_field_value cxpr_ir_call_producer_cached(cxpr_func_entry* entry, const char* name,
                                                     const char* cache_key,
                                                     const cxpr_context* ctx,
                                                     const cxpr_field_value* stack_args,
                                                     size_t argc, cxpr_error* err);
static const char* cxpr_ir_build_struct_cache_key(const char* name, const double* args, size_t argc,
                                                  char* local_buf, size_t local_cap,
                                                  char** heap_buf);
static bool cxpr_ir_defined_is_scalar_only(const cxpr_func_entry* entry);
static bool cxpr_ir_validate_scalar_fast_program(const cxpr_ir_program* program);

/**
 * @brief Free the storage owned by an internal IR program.
 *
 * @param program Program to clear. May be NULL.
 */
void cxpr_ir_program_reset(cxpr_ir_program* program) {
    if (!program) return;
    for (size_t i = 0; i < program->count; ++i) {
        if (program->code[i].op == CXPR_OP_CALL_PRODUCER_CONST) {
            free((char*)program->code[i].name);
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

/**
 * @brief Append one instruction to an IR program.
 *
 * @param program Program receiving the instruction.
 * @param instr Instruction to append.
 * @param err Optional error sink updated on allocation failure.
 * @return true when the instruction was appended.
 */
static bool cxpr_ir_emit(cxpr_ir_program* program, cxpr_ir_instr instr,
                         cxpr_error* err) {
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

/**
 * @brief Return the display name for one IR opcode.
 *
 * @param op Opcode to stringify.
 * @return Static opcode name string.
 */
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

/**
 * @brief Reserve one instruction slot and return its index.
 *
 * @param program Program whose next instruction index is requested.
 * @return Current instruction count.
 */
static size_t cxpr_ir_next_index(const cxpr_ir_program* program) {
    return program->count;
}

/**
 * @brief Patch the jump target of an already-emitted instruction.
 *
 * @param program Program that owns the instruction.
 * @param at Instruction index to patch.
 * @param target Destination instruction index.
 */
static void cxpr_ir_patch_target(cxpr_ir_program* program, size_t at, size_t target) {
    if (!program || at >= program->count) return;
    program->code[at].index = target;
}

/**
 * @brief Try to evaluate an AST subtree as a pure constant expression.
 *
 * @param ast AST subtree to inspect.
 * @param out Output location for the folded value.
 * @return true when the subtree was reduced to a constant.
 */
static bool cxpr_ir_constant_value(const cxpr_ast* ast, double* out) {
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

/**
 * @brief Check whether two AST subtrees are structurally identical.
 *
 * @param left First subtree to compare.
 * @param right Second subtree to compare.
 * @return true when both subtrees have the same shape and payload.
 */
static bool cxpr_ir_ast_equal(const cxpr_ast* left, const cxpr_ast* right) {
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

/**
 * @brief Report an IR runtime error and return a NaN sentinel value.
 *
 * @param err Optional error sink to populate.
 * @param message Static error message to store.
 * @return Double field value containing NaN.
 */
static cxpr_field_value cxpr_ir_runtime_error(cxpr_error* err, const char* message) {
    if (err) {
        err->code = CXPR_ERR_SYNTAX;
        err->message = message;
    }
    return cxpr_fv_double(NAN);
}

/**
 * @brief Push one value onto the IR evaluation stack.
 *
 * @param stack Evaluation stack storage.
 * @param sp In-out stack pointer.
 * @param value Value to push.
 * @param capacity Maximum stack capacity.
 * @param err Optional error sink updated on overflow.
 * @return true when the push succeeded.
 */
static bool cxpr_ir_stack_push(cxpr_field_value* stack, size_t* sp, cxpr_field_value value,
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

/**
 * @brief Ensure the IR stack contains at least the requested number of values.
 *
 * @param sp Current stack depth.
 * @param need Minimum number of values required.
 * @param err Optional error sink updated on underflow.
 * @return true when the stack depth is sufficient.
 */
static bool cxpr_ir_require_stack(size_t sp, size_t need, cxpr_error* err) {
    if (sp < need) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "IR stack underflow";
        }
        return false;
    }
    return true;
}

/**
 * @brief Validate that one field value has the expected type.
 *
 * @param value Value to inspect.
 * @param type Required field type.
 * @param err Optional error sink updated on mismatch.
 * @param message Static error message to store on mismatch.
 * @return true when the value type matches.
 */
static bool cxpr_ir_require_type(cxpr_field_value value, cxpr_field_type type,
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

/**
 * @brief Report a missing identifier-like lookup and return a NaN sentinel.
 *
 * @param err Optional error sink to populate.
 * @param message Static error message to store.
 * @return Double field value containing NaN.
 */
static cxpr_field_value cxpr_ir_make_not_found(cxpr_error* err, const char* message) {
    if (err) {
        err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
        err->message = message;
    }
    return cxpr_fv_double(NAN);
}

/**
 * @brief Return stack pop/push counts for one scalar fast-path instruction.
 *
 * @param instr Instruction to classify.
 * @param pops Output count of values consumed from the stack.
 * @param pushes Output count of values produced onto the stack.
 * @return true when the opcode is supported by scalar fast-path validation.
 */
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

/**
 * @brief Prove that a compiled scalar fast-path program has safe stack usage.
 *
 * @param program Compiled IR program to validate.
 * @return true when all reachable paths have valid stack depth and return shape.
 */
static bool cxpr_ir_validate_scalar_fast_program(const cxpr_ir_program* program) {
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

/**
 * @brief Build a cache key for producer calls with constant arguments.
 *
 * @param name Producer name.
 * @param args AST arguments to fold.
 * @param argc Number of arguments in @p args.
 * @return Newly allocated cache key string, or NULL when folding fails.
 */
static char* cxpr_ir_build_constant_producer_key(const char* name, const cxpr_ast* const* args,
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

/**
 * @brief Return the version counter relevant to one cached scalar lookup.
 *
 * @param ctx Context that owns the candidate hash-map entry.
 * @param param_lookup Whether the lookup targets the param map instead of variables.
 * @return Matching version counter for cache validation.
 */
static unsigned long cxpr_ir_lookup_version(const cxpr_context* ctx, bool param_lookup) {
    if (!ctx) return 0;
    return param_lookup ? ctx->params_version : ctx->variables_version;
}

/**
 * @brief Summarize structural versions on the path between request and owner contexts.
 *
 * @param request_ctx Context from which lookup begins.
 * @param owner_ctx Context that currently owns the resolved entry.
 * @param param_lookup Whether the lookup targets the param map instead of variables.
 * @return Combined version fingerprint for contexts below the owner.
 */
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

/**
 * @brief Resolve one scalar identifier lookup and refresh the per-instruction cache.
 *
 * Fast path for the common case where the request context directly owns the entry:
 * three pointer/base comparisons then one indexed array read, with no version counter load.
 *
 * Overlay contexts fall back to the shadow-version path; entries_base still replaces the
 * old per-entry pointer so the dereference is a single indexed access in both branches.
 *
 * @param ctx Context chain to search.
 * @param instr IR load instruction carrying key name/hash.
 * @param cache Mutable cache entry associated with the instruction.
 * @param param_lookup Whether to search params instead of variables.
 * @param found Optional output set to true on success.
 * @return Resolved scalar value, or 0.0 when missing.
 */
static double cxpr_ir_lookup_cached_scalar(const cxpr_context* ctx, const cxpr_ir_instr* instr,
                                           cxpr_ir_lookup_cache* cache, bool param_lookup,
                                           bool* found) {
    cxpr_hashmap_entry* map_entries =
        param_lookup ? ctx->params.entries : ctx->variables.entries;
    const cxpr_context* current;

    /* Direct-owner hot path: three comparisons, no version load, one indexed read. */
    if (cache && cache->request_ctx == ctx && cache->owner_ctx == ctx &&
        cache->entries_base == map_entries) {
        if (found) *found = true;
        return cache->entries_base[cache->slot].value;
    }

    /* Overlay path: owner is an ancestor; verify the chain below it is unchanged. */
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

/**
 * @brief Infer whether a subtree can use the scalar fast-path and which type it returns.
 *
 * @param ast AST subtree to inspect.
 * @param reg Registry used for function metadata lookups.
 * @param depth Current recursion depth guard.
 * @return One of the `CXPR_IR_RESULT_*` result-kind constants.
 */
static unsigned char cxpr_ir_infer_fast_result_kind(const cxpr_ast* ast, const cxpr_registry* reg,
                                                    size_t depth) {
    unsigned char left_kind;
    unsigned char right_kind;
    unsigned char cond_kind;
    cxpr_func_entry* entry;
    size_t i;

    if (!ast || depth > CXPR_IR_INFER_DEPTH_LIMIT) return CXPR_IR_RESULT_UNKNOWN;

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
    case CXPR_NODE_IDENTIFIER:
    case CXPR_NODE_VARIABLE:
        return CXPR_IR_RESULT_DOUBLE;

    case CXPR_NODE_BOOL:
        return CXPR_IR_RESULT_BOOL;

    case CXPR_NODE_FIELD_ACCESS:
    case CXPR_NODE_CHAIN_ACCESS:
    case CXPR_NODE_PRODUCER_ACCESS:
        return CXPR_IR_RESULT_UNKNOWN;

    case CXPR_NODE_UNARY_OP:
        left_kind = cxpr_ir_infer_fast_result_kind(ast->data.unary_op.operand, reg, depth + 1);
        if (ast->data.unary_op.op == CXPR_TOK_MINUS &&
            left_kind == CXPR_IR_RESULT_DOUBLE) {
            return CXPR_IR_RESULT_DOUBLE;
        }
        if (ast->data.unary_op.op == CXPR_TOK_NOT &&
            left_kind == CXPR_IR_RESULT_BOOL) {
            return CXPR_IR_RESULT_BOOL;
        }
        return CXPR_IR_RESULT_UNKNOWN;

    case CXPR_NODE_BINARY_OP:
        left_kind = cxpr_ir_infer_fast_result_kind(ast->data.binary_op.left, reg, depth + 1);
        right_kind = cxpr_ir_infer_fast_result_kind(ast->data.binary_op.right, reg, depth + 1);
        if (!left_kind || !right_kind) return CXPR_IR_RESULT_UNKNOWN;
        switch (ast->data.binary_op.op) {
        case CXPR_TOK_PLUS:
        case CXPR_TOK_MINUS:
        case CXPR_TOK_STAR:
        case CXPR_TOK_SLASH:
        case CXPR_TOK_PERCENT:
        case CXPR_TOK_POWER:
            return (left_kind == CXPR_IR_RESULT_DOUBLE &&
                    right_kind == CXPR_IR_RESULT_DOUBLE)
                       ? CXPR_IR_RESULT_DOUBLE
                       : CXPR_IR_RESULT_UNKNOWN;
        case CXPR_TOK_LT:
        case CXPR_TOK_LTE:
        case CXPR_TOK_GT:
        case CXPR_TOK_GTE:
            return (left_kind == CXPR_IR_RESULT_DOUBLE &&
                    right_kind == CXPR_IR_RESULT_DOUBLE)
                       ? CXPR_IR_RESULT_BOOL
                       : CXPR_IR_RESULT_UNKNOWN;
        case CXPR_TOK_AND:
        case CXPR_TOK_OR:
            return (left_kind == CXPR_IR_RESULT_BOOL &&
                    right_kind == CXPR_IR_RESULT_BOOL)
                       ? CXPR_IR_RESULT_BOOL
                       : CXPR_IR_RESULT_UNKNOWN;
        case CXPR_TOK_EQ:
        case CXPR_TOK_NEQ:
            return (left_kind == right_kind &&
                    (left_kind == CXPR_IR_RESULT_DOUBLE ||
                     left_kind == CXPR_IR_RESULT_BOOL))
                       ? CXPR_IR_RESULT_BOOL
                       : CXPR_IR_RESULT_UNKNOWN;
        default:
            return CXPR_IR_RESULT_UNKNOWN;
        }

    case CXPR_NODE_TERNARY:
        cond_kind = cxpr_ir_infer_fast_result_kind(ast->data.ternary.condition, reg, depth + 1);
        left_kind = cxpr_ir_infer_fast_result_kind(ast->data.ternary.true_branch, reg, depth + 1);
        right_kind =
            cxpr_ir_infer_fast_result_kind(ast->data.ternary.false_branch, reg, depth + 1);
        if (cond_kind == CXPR_IR_RESULT_BOOL && left_kind == right_kind &&
            (left_kind == CXPR_IR_RESULT_DOUBLE || left_kind == CXPR_IR_RESULT_BOOL)) {
            return left_kind;
        }
        return CXPR_IR_RESULT_UNKNOWN;

    case CXPR_NODE_FUNCTION_CALL:
        if (strcmp(ast->data.function_call.name, "if") == 0 &&
            ast->data.function_call.argc == 3) {
            cond_kind =
                cxpr_ir_infer_fast_result_kind(ast->data.function_call.args[0], reg, depth + 1);
            left_kind =
                cxpr_ir_infer_fast_result_kind(ast->data.function_call.args[1], reg, depth + 1);
            right_kind =
                cxpr_ir_infer_fast_result_kind(ast->data.function_call.args[2], reg, depth + 1);
            if ((cond_kind == CXPR_IR_RESULT_BOOL || cond_kind == CXPR_IR_RESULT_DOUBLE) &&
                left_kind == CXPR_IR_RESULT_DOUBLE &&
                right_kind == CXPR_IR_RESULT_DOUBLE) {
                return CXPR_IR_RESULT_DOUBLE;
            }
            return CXPR_IR_RESULT_UNKNOWN;
        }

        entry = cxpr_registry_find(reg, ast->data.function_call.name);
        if (!entry || entry->struct_fields || entry->struct_producer) {
            return CXPR_IR_RESULT_UNKNOWN;
        }

        for (i = 0; i < ast->data.function_call.argc; ++i) {
            if (cxpr_ir_infer_fast_result_kind(ast->data.function_call.args[i], reg, depth + 1) !=
                CXPR_IR_RESULT_DOUBLE) {
                return CXPR_IR_RESULT_UNKNOWN;
            }
        }

        if (entry->sync_func && !entry->defined_body) return CXPR_IR_RESULT_DOUBLE;
        if (entry->defined_body && cxpr_ir_defined_is_scalar_only(entry)) {
            return cxpr_ir_infer_fast_result_kind(entry->defined_body, reg, depth + 1);
        }
        return CXPR_IR_RESULT_UNKNOWN;

    default:
        return CXPR_IR_RESULT_UNKNOWN;
    }
}

/**
 * @brief Load one field access expression from the current context.
 *
 * @param ctx Evaluation context.
 * @param reg Registry used for zero-argument producer fallback.
 * @param instr IR instruction carrying the field name/hash.
 * @param err Optional error sink updated on failure.
 * @return Loaded field value, or NaN-wrapped scalar on failure.
 */
static cxpr_field_value cxpr_ir_load_field_value(const cxpr_context* ctx, const cxpr_registry* reg,
                                                 const cxpr_ir_instr* instr, cxpr_error* err) {
    const char* dot;
    bool found = false;
    cxpr_field_value value;
    char root[128];
    size_t root_len;

    if (!ctx || !instr || !instr->name) {
        return cxpr_ir_runtime_error(err, "Invalid field access");
    }

    dot = strchr(instr->name, '.');
    if (!dot) {
        return cxpr_ir_runtime_error(err, "Malformed field access");
    }

    root_len = (size_t)(dot - instr->name);
    if (root_len == 0 || root_len >= sizeof(root)) {
        return cxpr_ir_runtime_error(err, "Field access root too long");
    }

    memcpy(root, instr->name, root_len);
    root[root_len] = '\0';

    value = cxpr_context_get_field(ctx, root, dot + 1, &found);
    if (found) return value;

    if (reg) {
        cxpr_func_entry* producer = cxpr_registry_find(reg, root);
        if (producer && producer->struct_producer &&
            producer->min_args == 0 && producer->max_args == 0) {
            cxpr_field_value produced = cxpr_ir_call_producer(producer, root, ctx, NULL, 0, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            value = cxpr_context_get_field(ctx, root, dot + 1, &found);
            if (found) return value;
            (void)produced;
        }
    }

    value = cxpr_fv_double(cxpr_ir_context_get_prehashed(ctx, instr->name, instr->hash, &found));
    if (!found) {
        return cxpr_ir_make_not_found(err, "Unknown field access");
    }
    return value;
}

/**
 * @brief Read one field from a struct value by name.
 *
 * @param value Struct value to query.
 * @param field Field name to look up.
 * @param found Optional output set to true when the field exists.
 * @return Field value, or NaN-wrapped scalar when missing.
 */
static cxpr_field_value cxpr_ir_struct_get_field(const cxpr_struct_value* value,
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

/**
 * @brief Build a cache key for a struct producer invocation.
 *
 * @param name Producer name.
 * @param args Scalar arguments included in the key.
 * @param argc Number of arguments in @p args.
 * @param local_buf Optional caller-owned fallback buffer.
 * @param local_cap Capacity of @p local_buf.
 * @param heap_buf Optional output receiving heap allocation ownership.
 * @return Pointer to the constructed key, or NULL on allocation/format failure.
 */
static const char* cxpr_ir_build_struct_cache_key(const char* name, const double* args, size_t argc,
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

/**
 * @brief Resolve a chained struct-field access from the current context.
 *
 * @param ctx Evaluation context.
 * @param instr IR instruction carrying the dotted access path.
 * @param err Optional error sink updated on failure.
 * @return Resolved field value, or NaN-wrapped scalar on failure.
 */
static cxpr_field_value cxpr_ir_load_chain_value(const cxpr_context* ctx, const cxpr_ir_instr* instr,
                                                 cxpr_error* err) {
    char* path;
    char* segment;
    char* saveptr = NULL;
    const cxpr_struct_value* current;
    cxpr_field_value value = cxpr_fv_double(NAN);
    bool found = false;

    if (!ctx || !instr || !instr->name) {
        return cxpr_ir_runtime_error(err, "Invalid chain access");
    }

    path = cxpr_strdup(instr->name);
    if (!path) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return cxpr_fv_double(NAN);
    }

    segment = cxpr_strtok_r(path, ".", &saveptr);
    if (!segment) {
        free(path);
        return cxpr_ir_runtime_error(err, "Malformed chain access");
    }

    current = cxpr_context_get_struct(ctx, segment);
    if (!current) {
        free(path);
        return cxpr_ir_make_not_found(err, "Unknown identifier");
    }

    segment = cxpr_strtok_r(NULL, ".", &saveptr);
    while (segment) {
        char* next = cxpr_strtok_r(NULL, ".", &saveptr);
        found = false;
        for (size_t i = 0; i < current->field_count; ++i) {
            if (strcmp(current->field_names[i], segment) == 0) {
                value = current->field_values[i];
                found = true;
                break;
            }
        }
        if (!found) {
            free(path);
            return cxpr_ir_make_not_found(err, "Unknown identifier");
        }
        if (!next) {
            free(path);
            return value;
        }
        if (!cxpr_ir_require_type(value, CXPR_FIELD_STRUCT, err,
                                  "Chained access requires struct intermediate")) {
            free(path);
            return cxpr_fv_double(NAN);
        }
        current = value.s;
        segment = next;
    }

    free(path);
    return cxpr_ir_runtime_error(err, "Malformed chain access");
}

/**
 * @brief Invoke a struct producer with optional result caching in the context.
 *
 * @param entry Producer function metadata.
 * @param name Producer name.
 * @param cache_key Optional precomputed cache key.
 * @param ctx Evaluation context receiving cached structs.
 * @param stack_args Scalar arguments from the IR stack.
 * @param argc Number of arguments in @p stack_args.
 * @param err Optional error sink updated on failure.
 * @return Produced struct value, or NaN-wrapped scalar on failure.
 */
static cxpr_field_value cxpr_ir_call_producer_cached(cxpr_func_entry* entry, const char* name,
                                                     const char* cache_key,
                                                     const cxpr_context* ctx,
                                                     const cxpr_field_value* stack_args,
                                                     size_t argc, cxpr_error* err) {
    cxpr_context* mutable_ctx = (cxpr_context*)ctx;
    const cxpr_struct_value* existing;
    cxpr_field_value outputs[64];
    cxpr_struct_value* produced;
    double args[32];
    char cache_key_local[256];
    char* cache_key_heap = NULL;
    const char* resolved_cache_key = cache_key;

    if (!entry || !entry->struct_producer) {
        return cxpr_ir_runtime_error(err, "Invalid producer opcode");
    }

    if (argc < entry->min_args || argc > entry->max_args) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Wrong number of arguments";
        }
        return cxpr_fv_double(NAN);
    }
    if (argc > 32 || entry->fields_per_arg > 64) {
        return cxpr_ir_runtime_error(err, "Producer arity too large");
    }

    for (size_t i = 0; i < argc; ++i) {
        if (!cxpr_ir_require_type(stack_args[i], CXPR_FIELD_DOUBLE, err,
                                  "Producer arguments must be doubles")) {
            return cxpr_fv_double(NAN);
        }
        args[i] = stack_args[i].d;
    }

    if (!resolved_cache_key) {
        resolved_cache_key = cxpr_ir_build_struct_cache_key(name, args, argc,
                                                            cache_key_local,
                                                            sizeof(cache_key_local),
                                                            &cache_key_heap);
        if (!resolved_cache_key) {
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return cxpr_fv_double(NAN);
        }
    }

    existing = cxpr_context_get_struct(ctx, resolved_cache_key);
    if (!existing && argc == 0) {
        existing = cxpr_context_get_struct(ctx, name);
    }
    if (existing) {
        free(cache_key_heap);
        return cxpr_fv_struct((cxpr_struct_value*)existing);
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
        return cxpr_fv_double(NAN);
    }
    cxpr_context_set_struct(mutable_ctx, resolved_cache_key, produced);
    cxpr_struct_value_free(produced);
    existing = cxpr_context_get_struct(ctx, resolved_cache_key);
    free(cache_key_heap);
    return cxpr_fv_struct((cxpr_struct_value*)existing);
}

/**
 * @brief Invoke a struct producer without a precomputed cache key.
 *
 * @param entry Producer function metadata.
 * @param name Producer name.
 * @param ctx Evaluation context receiving cached structs.
 * @param stack_args Scalar arguments from the IR stack.
 * @param argc Number of arguments in @p stack_args.
 * @param err Optional error sink updated on failure.
 * @return Produced struct value, or NaN-wrapped scalar on failure.
 */
static cxpr_field_value cxpr_ir_call_producer(cxpr_func_entry* entry, const char* name,
                                              const cxpr_context* ctx,
                                              const cxpr_field_value* stack_args,
                                              size_t argc, cxpr_error* err) {
    return cxpr_ir_call_producer_cached(entry, name, NULL, ctx, stack_args, argc, err);
}

/**
 * @brief Square a scalar value and push the result onto the evaluation stack.
 *
 * @param stack Evaluation stack storage.
 * @param sp In-out stack pointer.
 * @param value Value to square and push.
 * @param err Optional error sink updated on type mismatch or overflow.
 * @return true when the squared value was pushed.
 */
static bool cxpr_ir_push_squared(cxpr_field_value* stack, size_t* sp, cxpr_field_value value,
                                 cxpr_error* err) {
    if (!cxpr_ir_require_type(value, CXPR_FIELD_DOUBLE, err,
                              "Square operation requires double operand")) {
        return false;
    }
    value.d *= value.d;
    return cxpr_ir_stack_push(stack, sp, value, 64, err);
}

/**
 * @brief Pop one value from the evaluation stack.
 *
 * @param stack Evaluation stack storage.
 * @param sp In-out stack pointer.
 * @param out Output location for the popped value.
 * @param err Optional error sink updated on underflow.
 * @return true when one value was popped.
 */
static bool cxpr_ir_pop1(cxpr_field_value* stack, size_t* sp, cxpr_field_value* out,
                         cxpr_error* err) {
    if (!cxpr_ir_require_stack(*sp, 1, err)) return false;
    *out = stack[--(*sp)];
    return true;
}

/**
 * @brief Pop two values from the evaluation stack in left-to-right order.
 *
 * @param stack Evaluation stack storage.
 * @param sp In-out stack pointer.
 * @param left Output location for the older stack value.
 * @param right Output location for the newer stack value.
 * @param err Optional error sink updated on underflow.
 * @return true when two values were popped.
 */
static bool cxpr_ir_pop2(cxpr_field_value* stack, size_t* sp, cxpr_field_value* left,
                         cxpr_field_value* right, cxpr_error* err) {
    if (!cxpr_ir_require_stack(*sp, 2, err)) return false;
    *right = stack[--(*sp)];
    *left = stack[--(*sp)];
    return true;
}

/**
 * @brief Execute an IR program with full runtime type tracking.
 *
 * @param program Program to execute.
 * @param ctx Evaluation context.
 * @param reg Registry used for function dispatch.
 * @param locals Optional array of local scalar values.
 * @param local_count Number of entries in @p locals.
 * @param err Optional error sink updated on failure.
 * @return Resulting typed field value.
 */
static cxpr_field_value cxpr_ir_exec_typed(const cxpr_ir_program* program, const cxpr_context* ctx,
                                           const cxpr_registry* reg, const double* locals,
                                           size_t local_count, cxpr_error* err) {
    cxpr_field_value stack[64];
    size_t sp = 0;
    size_t ip = 0;

    if (err) *err = (cxpr_error){0};
    if (!program || !program->code) {
        return cxpr_ir_runtime_error(err, "Empty IR program");
    }

    while (ip < program->count) {
        const cxpr_ir_instr* instr = &program->code[ip];
        cxpr_field_value a, b, result;
        double args[32];

        switch (instr->op) {
        case CXPR_OP_PUSH_CONST:
            if (!cxpr_ir_stack_push(stack, &sp, cxpr_fv_double(instr->value), 64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_PUSH_BOOL:
            if (!cxpr_ir_stack_push(stack, &sp, cxpr_fv_bool(instr->value != 0.0), 64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_LOAD_LOCAL:
            if (instr->index >= local_count) {
                return cxpr_ir_runtime_error(err, "Unknown local variable");
            }
            if (!cxpr_ir_stack_push(stack, &sp, cxpr_fv_double(locals[instr->index]), 64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_LOAD_LOCAL_SQUARE:
            if (instr->index >= local_count) {
                return cxpr_ir_runtime_error(err, "Unknown local variable");
            }
            if (!cxpr_ir_push_squared(stack, &sp, cxpr_fv_double(locals[instr->index]), err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_LOAD_VAR:
            {
                bool found = false;
                result = cxpr_fv_double(cxpr_ir_lookup_cached_scalar(
                    ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, false,
                    &found));
                if (!found) return cxpr_ir_make_not_found(err, "Unknown identifier");
            }
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_LOAD_VAR_SQUARE:
            {
                bool found = false;
                result = cxpr_fv_double(cxpr_ir_lookup_cached_scalar(
                    ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, false,
                    &found));
                if (!found) return cxpr_ir_make_not_found(err, "Unknown identifier");
            }
            if (!cxpr_ir_push_squared(stack, &sp, result, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_LOAD_PARAM:
            {
                bool found = false;
                result = cxpr_fv_double(cxpr_ir_lookup_cached_scalar(
                    ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, true,
                    &found));
                if (!found) return cxpr_ir_make_not_found(err, "Unknown parameter variable");
            }
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_LOAD_PARAM_SQUARE:
            {
                bool found = false;
                result = cxpr_fv_double(cxpr_ir_lookup_cached_scalar(
                    ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, true,
                    &found));
                if (!found) return cxpr_ir_make_not_found(err, "Unknown parameter variable");
            }
            if (!cxpr_ir_push_squared(stack, &sp, result, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_LOAD_FIELD:
            result = cxpr_ir_load_field_value(ctx, reg, instr, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_LOAD_FIELD_SQUARE:
            result = cxpr_ir_load_field_value(ctx, reg, instr, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_ir_push_squared(stack, &sp, result, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_LOAD_CHAIN:
            result = cxpr_ir_load_chain_value(ctx, instr, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
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
            if (!cxpr_ir_pop2(stack, &sp, &a, &b, err)) return cxpr_fv_double(NAN);
            switch (instr->op) {
            case CXPR_OP_ADD:
            case CXPR_OP_SUB:
            case CXPR_OP_MUL:
            case CXPR_OP_DIV:
            case CXPR_OP_MOD:
            case CXPR_OP_POW:
                if (!cxpr_ir_require_type(a, CXPR_FIELD_DOUBLE, err,
                                          "Arithmetic requires double operands") ||
                    !cxpr_ir_require_type(b, CXPR_FIELD_DOUBLE, err,
                                          "Arithmetic requires double operands")) {
                    return cxpr_fv_double(NAN);
                }
                if (instr->op == CXPR_OP_DIV && b.d == 0.0) {
                    if (err) {
                        err->code = CXPR_ERR_DIVISION_BY_ZERO;
                        err->message = "Division by zero";
                    }
                    return cxpr_fv_double(NAN);
                }
                if (instr->op == CXPR_OP_MOD && b.d == 0.0) {
                    if (err) {
                        err->code = CXPR_ERR_DIVISION_BY_ZERO;
                        err->message = "Modulo by zero";
                    }
                    return cxpr_fv_double(NAN);
                }
                switch (instr->op) {
                case CXPR_OP_ADD: result = cxpr_fv_double(a.d + b.d); break;
                case CXPR_OP_SUB: result = cxpr_fv_double(a.d - b.d); break;
                case CXPR_OP_MUL: result = cxpr_fv_double(a.d * b.d); break;
                case CXPR_OP_DIV: result = cxpr_fv_double(a.d / b.d); break;
                case CXPR_OP_MOD: result = cxpr_fv_double(fmod(a.d, b.d)); break;
                default: result = cxpr_fv_double(pow(a.d, b.d)); break;
                }
                break;
            case CXPR_OP_CMP_EQ:
            case CXPR_OP_CMP_NEQ:
                if (a.type != b.type ||
                    (a.type != CXPR_FIELD_DOUBLE && a.type != CXPR_FIELD_BOOL)) {
                    if (err) {
                        err->code = CXPR_ERR_TYPE_MISMATCH;
                        err->message = "Equality requires matching double/bool operands";
                    }
                    return cxpr_fv_double(NAN);
                }
                if (a.type == CXPR_FIELD_DOUBLE) {
                    result = cxpr_fv_bool(instr->op == CXPR_OP_CMP_EQ ? (a.d == b.d) : (a.d != b.d));
                } else {
                    result = cxpr_fv_bool(instr->op == CXPR_OP_CMP_EQ ? (a.b == b.b) : (a.b != b.b));
                }
                break;
            default:
                if (!cxpr_ir_require_type(a, CXPR_FIELD_DOUBLE, err,
                                          "Comparison requires double operands") ||
                    !cxpr_ir_require_type(b, CXPR_FIELD_DOUBLE, err,
                                          "Comparison requires double operands")) {
                    return cxpr_fv_double(NAN);
                }
                switch (instr->op) {
                case CXPR_OP_CMP_LT: result = cxpr_fv_bool(a.d < b.d); break;
                case CXPR_OP_CMP_LTE: result = cxpr_fv_bool(a.d <= b.d); break;
                case CXPR_OP_CMP_GT: result = cxpr_fv_bool(a.d > b.d); break;
                default: result = cxpr_fv_bool(a.d >= b.d); break;
                }
                break;
            }
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_SQUARE:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_FIELD_DOUBLE, err,
                                      "Square operation requires double operand")) {
                return cxpr_fv_double(NAN);
            }
            if (!cxpr_ir_stack_push(stack, &sp, cxpr_fv_double(a.d * a.d), 64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_NOT:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_FIELD_BOOL, err,
                                      "Logical not requires bool operand")) {
                return cxpr_fv_double(NAN);
            }
            if (!cxpr_ir_stack_push(stack, &sp, cxpr_fv_bool(!a.b), 64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_NEG:
        case CXPR_OP_SIGN:
        case CXPR_OP_SQRT:
        case CXPR_OP_ABS:
        case CXPR_OP_FLOOR:
        case CXPR_OP_CEIL:
        case CXPR_OP_ROUND:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_FIELD_DOUBLE, err,
                                      "Numeric intrinsic requires double operand")) {
                return cxpr_fv_double(NAN);
            }
            switch (instr->op) {
            case CXPR_OP_NEG: result = cxpr_fv_double(-a.d); break;
            case CXPR_OP_SIGN: result = cxpr_fv_double((a.d > 0.0) - (a.d < 0.0)); break;
            case CXPR_OP_SQRT: result = cxpr_fv_double(sqrt(a.d)); break;
            case CXPR_OP_ABS: result = cxpr_fv_double(fabs(a.d)); break;
            case CXPR_OP_FLOOR: result = cxpr_fv_double(floor(a.d)); break;
            case CXPR_OP_CEIL: result = cxpr_fv_double(ceil(a.d)); break;
            default: result = cxpr_fv_double(round(a.d)); break;
            }
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_CLAMP:
            if (!cxpr_ir_pop2(stack, &sp, &result, &b, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_FIELD_DOUBLE, err,
                                      "clamp() requires double operands") ||
                !cxpr_ir_require_type(result, CXPR_FIELD_DOUBLE, err,
                                      "clamp() requires double operands") ||
                !cxpr_ir_require_type(b, CXPR_FIELD_DOUBLE, err,
                                      "clamp() requires double operands")) {
                return cxpr_fv_double(NAN);
            }
            if (a.d < result.d) a.d = result.d;
            if (a.d > b.d) a.d = b.d;
            if (!cxpr_ir_stack_push(stack, &sp, a, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_CALL_UNARY:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_FIELD_DOUBLE, err,
                                      "Function arguments must be doubles")) {
                return cxpr_fv_double(NAN);
            }
            if (!cxpr_ir_stack_push(stack, &sp,
                                    cxpr_fv_double(instr->func->native_scalar.unary(a.d)),
                                    64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_CALL_BINARY:
            if (!cxpr_ir_pop2(stack, &sp, &a, &b, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_FIELD_DOUBLE, err,
                                      "Function arguments must be doubles") ||
                !cxpr_ir_require_type(b, CXPR_FIELD_DOUBLE, err,
                                      "Function arguments must be doubles")) {
                return cxpr_fv_double(NAN);
            }
            if (!cxpr_ir_stack_push(stack, &sp,
                                    cxpr_fv_double(instr->func->native_scalar.binary(a.d, b.d)),
                                    64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_CALL_TERNARY:
            if (!cxpr_ir_pop2(stack, &sp, &b, &result, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_FIELD_DOUBLE, err,
                                      "Function arguments must be doubles") ||
                !cxpr_ir_require_type(b, CXPR_FIELD_DOUBLE, err,
                                      "Function arguments must be doubles") ||
                !cxpr_ir_require_type(result, CXPR_FIELD_DOUBLE, err,
                                      "Function arguments must be doubles")) {
                return cxpr_fv_double(NAN);
            }
            if (!cxpr_ir_stack_push(
                    stack, &sp,
                    cxpr_fv_double(instr->func->native_scalar.ternary(a.d, b.d, result.d)),
                    64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_CALL_FUNC:
            if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_fv_double(NAN);
            if (instr->index > 32) return cxpr_ir_runtime_error(err, "Too many function arguments");
            for (size_t i = 0; i < instr->index; ++i) {
                a = stack[sp - instr->index + i];
                if (!cxpr_ir_require_type(a, CXPR_FIELD_DOUBLE, err,
                                          "Function arguments must be doubles")) {
                    return cxpr_fv_double(NAN);
                }
                args[i] = a.d;
            }
            sp -= instr->index;
            if (!cxpr_ir_stack_push(stack, &sp,
                                    cxpr_fv_double(instr->func->sync_func(args, instr->index,
                                                                          instr->func->userdata)),
                                    64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_CALL_DEFINED:
            if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_fv_double(NAN);
            result = cxpr_ir_call_defined_scalar((cxpr_func_entry*)instr->func, ctx, reg,
                                                 &stack[sp - instr->index], instr->index, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            sp -= instr->index;
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_CALL_PRODUCER:
            if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_fv_double(NAN);
            result = cxpr_ir_call_producer((cxpr_func_entry*)instr->func, instr->name, ctx,
                                           &stack[sp - instr->index], instr->index, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            sp -= instr->index;
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_CALL_PRODUCER_CONST:
            if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_fv_double(NAN);
            result = cxpr_ir_call_producer_cached((cxpr_func_entry*)instr->func,
                                                  instr->func->name, instr->name, ctx,
                                                  &stack[sp - instr->index], instr->index, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            sp -= instr->index;
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_GET_FIELD:
            {
                bool found = false;
    
                if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
                if (!cxpr_ir_require_type(a, CXPR_FIELD_STRUCT, err,
                                          "Field access requires struct operand")) {
                    return cxpr_fv_double(NAN);
                }
                result = cxpr_ir_struct_get_field(a.s, instr->name, &found);
                if (!found) return cxpr_ir_make_not_found(err, "Unknown field access");
                if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_CALL_AST:
            result = cxpr_ast_eval(instr->ast, ctx, reg, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_JUMP:
            ip = instr->index;
            continue;
        case CXPR_OP_JUMP_IF_FALSE:
        case CXPR_OP_JUMP_IF_TRUE:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_FIELD_BOOL, err,
                                      "Conditional jump requires bool operand")) {
                return cxpr_fv_double(NAN);
            }
            if ((instr->op == CXPR_OP_JUMP_IF_FALSE && !a.b) ||
                (instr->op == CXPR_OP_JUMP_IF_TRUE && a.b)) {
                ip = instr->index;
                continue;
            }
            break;
        case CXPR_OP_RETURN:
            if (!cxpr_ir_pop1(stack, &sp, &result, err)) return cxpr_fv_double(NAN);
            if (sp != 0) {
                return cxpr_ir_runtime_error(err, "IR stack not empty at return");
            }
            return result;
        default:
            return cxpr_ir_runtime_error(err, "Unsupported IR opcode");
        }

        ++ip;
    }

    return cxpr_ir_runtime_error(err, "IR program fell off end without return");
}

/**
 * @brief Execute a scalar-only IR program using the unchecked fast-path.
 *
 * @param program Program to execute.
 * @param ctx Evaluation context.
 * @param reg Registry used for defined-function fallback.
 * @param locals Optional array of local scalar values.
 * @param local_count Number of entries in @p locals.
 * @param err Optional error sink updated on failure.
 * @return Scalar result, or NaN on failure.
 */
static double cxpr_ir_exec_scalar_fast(const cxpr_ir_program* program, const cxpr_context* ctx,
                                       const cxpr_registry* reg, const double* locals,
                                       size_t local_count, cxpr_error* err) {
    double stack[CXPR_IR_STACK_CAPACITY];
    size_t sp = 0;
    size_t ip = 0;

    if (err) *err = (cxpr_error){0};
    if (!program || !program->code) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Empty IR program";
        }
        return NAN;
    }

    while (ip < program->count) {
        const cxpr_ir_instr* instr = &program->code[ip];
        double a, b, value;
        double args[32];

        switch (instr->op) {
        case CXPR_OP_PUSH_CONST:
            stack[sp++] = instr->value;
            break;
        case CXPR_OP_PUSH_BOOL:
            stack[sp++] = instr->value != 0.0 ? 1.0 : 0.0;
            break;
        case CXPR_OP_LOAD_LOCAL:
            if (instr->index >= local_count) {
                return cxpr_ir_runtime_error(err, "Unknown local variable").d;
            }
            stack[sp++] = locals[instr->index];
            break;
        case CXPR_OP_LOAD_LOCAL_SQUARE:
            if (instr->index >= local_count) {
                return cxpr_ir_runtime_error(err, "Unknown local variable").d;
            }
            value = locals[instr->index];
            stack[sp++] = value * value;
            break;
        case CXPR_OP_LOAD_VAR: {
            bool found = false;
            value = cxpr_ir_lookup_cached_scalar(
                ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, false,
                &found);
            if (!found) return cxpr_ir_make_not_found(err, "Unknown identifier").d;
            stack[sp++] = value;
            break;
        }
        case CXPR_OP_LOAD_VAR_SQUARE: {
            bool found = false;
            value = cxpr_ir_lookup_cached_scalar(
                ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, false,
                &found);
            if (!found) return cxpr_ir_make_not_found(err, "Unknown identifier").d;
            stack[sp++] = value * value;
            break;
        }
        case CXPR_OP_LOAD_PARAM: {
            bool found = false;
            value = cxpr_ir_lookup_cached_scalar(
                ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, true,
                &found);
            if (!found) return cxpr_ir_make_not_found(err, "Unknown parameter variable").d;
            stack[sp++] = value;
            break;
        }
        case CXPR_OP_LOAD_PARAM_SQUARE: {
            bool found = false;
            value = cxpr_ir_lookup_cached_scalar(
                ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, true,
                &found);
            if (!found) return cxpr_ir_make_not_found(err, "Unknown parameter variable").d;
            stack[sp++] = value * value;
            break;
        }
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
            b = stack[--sp];
            a = stack[--sp];
            switch (instr->op) {
            case CXPR_OP_ADD: value = a + b; break;
            case CXPR_OP_SUB: value = a - b; break;
            case CXPR_OP_MUL: value = a * b; break;
            case CXPR_OP_DIV:
                if (b == 0.0) {
                    if (err) {
                        err->code = CXPR_ERR_DIVISION_BY_ZERO;
                        err->message = "Division by zero";
                    }
                    return NAN;
                }
                value = a / b;
                break;
            case CXPR_OP_MOD:
                if (b == 0.0) {
                    if (err) {
                        err->code = CXPR_ERR_DIVISION_BY_ZERO;
                        err->message = "Modulo by zero";
                    }
                    return NAN;
                }
                value = fmod(a, b);
                break;
            case CXPR_OP_POW: value = pow(a, b); break;
            case CXPR_OP_CMP_EQ: value = (a == b) ? 1.0 : 0.0; break;
            case CXPR_OP_CMP_NEQ: value = (a != b) ? 1.0 : 0.0; break;
            case CXPR_OP_CMP_LT: value = (a < b) ? 1.0 : 0.0; break;
            case CXPR_OP_CMP_LTE: value = (a <= b) ? 1.0 : 0.0; break;
            case CXPR_OP_CMP_GT: value = (a > b) ? 1.0 : 0.0; break;
            default: value = (a >= b) ? 1.0 : 0.0; break;
            }
            stack[sp++] = value;
            break;
        case CXPR_OP_SQUARE:
            stack[sp - 1] *= stack[sp - 1];
            break;
        case CXPR_OP_NOT:
            stack[sp - 1] = stack[sp - 1] == 0.0 ? 1.0 : 0.0;
            break;
        case CXPR_OP_NEG:
            stack[sp - 1] = -stack[sp - 1];
            break;
        case CXPR_OP_SIGN:
            value = stack[sp - 1];
            stack[sp - 1] = (value > 0.0) - (value < 0.0);
            break;
        case CXPR_OP_SQRT:
            stack[sp - 1] = sqrt(stack[sp - 1]);
            break;
        case CXPR_OP_ABS:
            stack[sp - 1] = fabs(stack[sp - 1]);
            break;
        case CXPR_OP_FLOOR:
            stack[sp - 1] = floor(stack[sp - 1]);
            break;
        case CXPR_OP_CEIL:
            stack[sp - 1] = ceil(stack[sp - 1]);
            break;
        case CXPR_OP_ROUND:
            stack[sp - 1] = round(stack[sp - 1]);
            break;
        case CXPR_OP_CLAMP:
            b = stack[--sp];
            a = stack[--sp];
            value = stack[--sp];
            if (value < a) value = a;
            if (value > b) value = b;
            stack[sp++] = value;
            break;
        case CXPR_OP_CALL_UNARY:
            a = stack[--sp];
            stack[sp++] = instr->func->native_scalar.unary(a);
            break;
        case CXPR_OP_CALL_BINARY:
            b = stack[--sp];
            a = stack[--sp];
            stack[sp++] = instr->func->native_scalar.binary(a, b);
            break;
        case CXPR_OP_CALL_TERNARY:
            value = stack[--sp];
            b = stack[--sp];
            a = stack[--sp];
            stack[sp++] = instr->func->native_scalar.ternary(a, b, value);
            break;
        case CXPR_OP_CALL_FUNC:
            for (size_t i = 0; i < instr->index; ++i) {
                args[i] = stack[sp - instr->index + i];
            }
            sp -= instr->index;
            stack[sp++] = instr->func->sync_func(args, instr->index, instr->func->userdata);
            break;
        case CXPR_OP_CALL_DEFINED: {
            cxpr_field_value result;
            cxpr_field_value scalar_args[32];
            for (size_t i = 0; i < instr->index; ++i) {
                scalar_args[i] = cxpr_fv_double(stack[sp - instr->index + i]);
            }
            result = cxpr_ir_call_defined_scalar((cxpr_func_entry*)instr->func, ctx, reg,
                                                 scalar_args, instr->index, err);
            if (err && err->code != CXPR_OK) return NAN;
            sp -= instr->index;
            if (result.type == CXPR_FIELD_DOUBLE) value = result.d;
            else if (result.type == CXPR_FIELD_BOOL) value = result.b ? 1.0 : 0.0;
            else {
                if (err) {
                    err->code = CXPR_ERR_TYPE_MISMATCH;
                    err->message = "Defined function returned non-scalar";
                }
                return NAN;
            }
            stack[sp++] = value;
            break;
        }
        case CXPR_OP_JUMP:
            ip = instr->index;
            continue;
        case CXPR_OP_JUMP_IF_FALSE:
            if (stack[--sp] == 0.0) {
                ip = instr->index;
                continue;
            }
            break;
        case CXPR_OP_JUMP_IF_TRUE:
            if (stack[--sp] != 0.0) {
                ip = instr->index;
                continue;
            }
            break;
        case CXPR_OP_RETURN:
            assert(sp == 1);
            value = stack[--sp];
            return value;
        default:
            return cxpr_ir_runtime_error(err, "Unsupported fast IR opcode").d;
        }

        ++ip;
    }

    return cxpr_ir_runtime_error(err, "IR program fell off end without return").d;
}

/**
 * @brief Check whether a defined function uses only scalar parameters and results.
 *
 * @param entry Function metadata to inspect.
 * @return true when the defined body is scalar-only.
 */
static bool cxpr_ir_defined_is_scalar_only(const cxpr_func_entry* entry) {
    if (!entry || !entry->defined_body) return false;
    for (size_t i = 0; i < entry->defined_param_count; ++i) {
        if (entry->defined_param_fields[i] && entry->defined_param_field_counts[i] > 0) {
            return false;
        }
    }
    return true;
}

bool cxpr_ir_prepare_defined_program(cxpr_func_entry* entry, const cxpr_registry* reg,
                                     cxpr_error* err) {
    if (!entry || !entry->defined_body || !cxpr_ir_defined_is_scalar_only(entry)) {
        return false;
    }
    if (entry->defined_program || entry->defined_program_failed) {
        return entry->defined_program != NULL;
    }
    if (err) *err = (cxpr_error){0};

    entry->defined_program = (cxpr_program*)calloc(1, sizeof(cxpr_program));
    if (!entry->defined_program) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return false;
    }

    if (!cxpr_ir_compile_with_locals(entry->defined_body, reg,
                                     (const char* const*)entry->defined_param_names,
                                     entry->defined_param_count, &entry->defined_program->ir,
                                     err)) {
        entry->defined_program_failed = true;
        cxpr_program_free(entry->defined_program);
        entry->defined_program = NULL;
        return false;
    }

    entry->defined_program->ast = entry->defined_body;
    return true;
}

/**
 * @brief Map one local name to its positional index.
 *
 * @param name Local name to resolve.
 * @param local_names Array of local names.
 * @param local_count Number of entries in @p local_names.
 * @return Matching local index, or `(size_t)-1` when missing.
 */
static size_t cxpr_ir_local_index(const char* name, const char* const* local_names,
                                  size_t local_count) {
    size_t i;
    if (!name || !local_names) return (size_t)-1;
    for (i = 0; i < local_count; ++i) {
        if (local_names[i] && strcmp(local_names[i], name) == 0) return i;
    }
    return (size_t)-1;
}

/**
 * @brief Resolve one substitution binding from the inline-substitution stack.
 *
 * @param frame Current substitution frame chain.
 * @param name Parameter name to resolve.
 * @param owner Optional output receiving the owning frame.
 * @return Substituted AST node, or NULL when no binding exists.
 */
static const cxpr_ast* cxpr_ir_subst_lookup(const cxpr_ir_subst_frame* frame, const char* name,
                                            const cxpr_ir_subst_frame** owner) {
    size_t i;
    for (; frame; frame = frame->parent) {
        for (i = 0; i < frame->count; ++i) {
            if (frame->names[i] && strcmp(frame->names[i], name) == 0) {
                if (owner) *owner = frame;
                return frame->args[i];
            }
        }
    }
    if (owner) *owner = NULL;
    return NULL;
}

/**
 * @brief Emit a leaf load for an AST subtree, optionally squared in-place.
 *
 * @param ast AST subtree to lower.
 * @param program Program receiving the emitted instruction.
 * @param local_names Array of in-scope local names.
 * @param local_count Number of entries in @p local_names.
 * @param subst Inline substitution bindings for defined functions.
 * @param square Whether the loaded scalar should be squared in-place.
 * @param err Optional error sink updated on failure.
 * @return true when the subtree was emitted as a leaf load.
 */
static bool cxpr_ir_emit_leaf_load(const cxpr_ast* ast, cxpr_ir_program* program,
                                   const char* const* local_names, size_t local_count,
                                   const cxpr_ir_subst_frame* subst, bool square,
                                   cxpr_error* err) {
    if (!ast) return false;

    switch (ast->type) {
    case CXPR_NODE_IDENTIFIER: {
        const cxpr_ir_subst_frame* owner = NULL;
        const cxpr_ast* mapped = cxpr_ir_subst_lookup(subst, ast->data.identifier.name, &owner);
        if (mapped) {
            return cxpr_ir_emit_leaf_load(mapped, program, local_names, local_count,
                                          owner ? owner->parent : NULL, square, err);
        }
        {
            const size_t local_index =
                cxpr_ir_local_index(ast->data.identifier.name, local_names, local_count);
            if (local_index != (size_t)-1) {
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = square ? CXPR_OP_LOAD_LOCAL_SQUARE : CXPR_OP_LOAD_LOCAL,
                                        .index = local_index,
                                    },
                                    err);
            }
        }
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = square ? CXPR_OP_LOAD_VAR_SQUARE : CXPR_OP_LOAD_VAR,
                                .name = ast->data.identifier.name,
                                .hash = cxpr_hash_string(ast->data.identifier.name),
                            },
                            err);
    }

    case CXPR_NODE_VARIABLE:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = square ? CXPR_OP_LOAD_PARAM_SQUARE : CXPR_OP_LOAD_PARAM,
                                .name = ast->data.variable.name,
                                .hash = cxpr_hash_string(ast->data.variable.name),
                            },
                            err);

    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = square ? CXPR_OP_LOAD_FIELD_SQUARE : CXPR_OP_LOAD_FIELD,
                                .name = ast->data.field_access.full_key,
                                .hash = cxpr_hash_string(ast->data.field_access.full_key),
                            },
                            err);

    default:
        return false;
    }
}

/**
 * @brief Resolve one variable lookup using a precomputed hash.
 *
 * @param ctx Context chain to search.
 * @param name Variable name to resolve.
 * @param hash Precomputed hash for @p name.
 * @param found Optional output set to true on success.
 * @return Resolved scalar value, or `0.0` when missing.
 */
static double cxpr_ir_context_get_prehashed(const cxpr_context* ctx, const char* name,
                                            unsigned long hash, bool* found) {
    double value;

    if (!ctx) {
        if (found) *found = false;
        return 0.0;
    }

    value = cxpr_hashmap_get_prehashed(&ctx->variables, name, hash, found);
    if (found && *found) return value;
    if (ctx->parent) return cxpr_ir_context_get_prehashed(ctx->parent, name, hash, found);
    if (found) *found = false;
    return 0.0;
}

/**
 * @brief Resolve one parameter lookup using a precomputed hash.
 *
 * @param ctx Context chain to search.
 * @param name Parameter name to resolve.
 * @param hash Precomputed hash for @p name.
 * @param found Optional output set to true on success.
 * @return Resolved scalar value, or `0.0` when missing.
 */
static double cxpr_ir_context_get_param_prehashed(const cxpr_context* ctx, const char* name,
                                                  unsigned long hash, bool* found) {
    double value;

    if (!ctx) {
        if (found) *found = false;
        return 0.0;
    }

    value = cxpr_hashmap_get_prehashed(&ctx->params, name, hash, found);
    if (found && *found) return value;
    if (ctx->parent) {
        return cxpr_ir_context_get_param_prehashed(ctx->parent, name, hash, found);
    }
    if (found) *found = false;
    return 0.0;
}

/**
 * @brief Evaluate a scalar-only defined function call.
 *
 * @param entry Defined function metadata.
 * @param ctx Caller evaluation context.
 * @param reg Registry used for nested lookups and compilation.
 * @param args Scalar arguments as typed field values.
 * @param argc Number of arguments in @p args.
 * @param err Optional error sink updated on failure.
 * @return Resulting field value from the defined body.
 */
static cxpr_field_value cxpr_ir_call_defined_scalar(cxpr_func_entry* entry,
                                                    const cxpr_context* ctx,
                                                    const cxpr_registry* reg,
                                                    const cxpr_field_value* args,
                                                    size_t argc, cxpr_error* err) {
    double locals[32];
    if (!entry || !entry->defined_body) {
        return cxpr_ir_runtime_error(err, "NULL IR defined function entry");
    }
    if (argc != entry->defined_param_count) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Wrong number of arguments";
        }
        return cxpr_fv_double(NAN);
    }

    for (size_t i = 0; i < argc; ++i) {
        if (args[i].type != CXPR_FIELD_DOUBLE) {
            if (err) {
                err->code = CXPR_ERR_TYPE_MISMATCH;
                err->message = "Defined function arguments must be doubles";
            }
            return cxpr_fv_double(NAN);
        }
        locals[i] = args[i].d;
    }

    if (cxpr_ir_prepare_defined_program(entry, reg, err) && entry->defined_program) {
        return cxpr_fv_double(cxpr_ir_exec_with_locals(&entry->defined_program->ir, ctx, reg,
                                                       locals, argc, err));
    }

    {
        cxpr_context* tmp = cxpr_context_overlay_new(ctx);
        if (!tmp) {
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return cxpr_fv_double(NAN);
        }
        for (size_t i = 0; i < argc; ++i) {
            cxpr_context_set(tmp, entry->defined_param_names[i], locals[i]);
        }
        {
            cxpr_field_value result = cxpr_ast_eval(entry->defined_body, tmp, reg, err);
            cxpr_context_free(tmp);
            return result;
        }
    }
}

/**
 * @brief Recursively compile a supported AST subtree into IR instructions.
 *
 * @param ast AST subtree to compile.
 * @param program Program receiving emitted instructions.
 * @param reg Registry used for function metadata lookups.
 * @param local_names Array of in-scope local names.
 * @param local_count Number of entries in @p local_names.
 * @param subst Inline substitution bindings for defined functions.
 * @param inline_depth Current defined-function inlining depth.
 * @param err Optional error sink updated on failure.
 * @return true when compilation succeeded.
 */
static bool cxpr_ir_compile_node(const cxpr_ast* ast, cxpr_ir_program* program,
                                 const cxpr_registry* reg,
                                 const char* const* local_names, size_t local_count,
                                 const cxpr_ir_subst_frame* subst,
                                 size_t inline_depth,
                                 cxpr_error* err) {
    double constant;

    if (!ast) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "NULL AST node";
        }
        return false;
    }

    if (cxpr_ir_constant_value(ast, &constant)) {
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_PUSH_CONST,
                                .value = constant,
                            },
                            err);
    }

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_PUSH_CONST,
                                .value = ast->data.number.value,
                                .name = NULL,
                            },
                            err);

    case CXPR_NODE_BOOL:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_PUSH_BOOL,
                                .value = ast->data.boolean.value ? 1.0 : 0.0,
                                .name = NULL,
                            },
                            err);

    case CXPR_NODE_IDENTIFIER:
        {
            const cxpr_ir_subst_frame* owner = NULL;
            const cxpr_ast* mapped = cxpr_ir_subst_lookup(subst, ast->data.identifier.name, &owner);
            if (mapped) {
                return cxpr_ir_compile_node(mapped, program, reg,
                                            local_names, local_count,
                                            owner ? owner->parent : NULL,
                                            inline_depth, err);
            }
            const size_t local_index =
                cxpr_ir_local_index(ast->data.identifier.name, local_names, local_count);
            if (local_index != (size_t)-1) {
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = CXPR_OP_LOAD_LOCAL,
                                        .index = local_index,
                                    },
                                    err);
            }
        }
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_LOAD_VAR,
                                .name = ast->data.identifier.name,
                                .hash = cxpr_hash_string(ast->data.identifier.name),
                            },
                            err);

    case CXPR_NODE_VARIABLE:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_LOAD_PARAM,
                                .name = ast->data.variable.name,
                                .hash = cxpr_hash_string(ast->data.variable.name),
                            },
                            err);

    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_LOAD_FIELD,
                                .name = ast->data.field_access.full_key,
                                .hash = cxpr_hash_string(ast->data.field_access.full_key),
                            },
                            err);

    case CXPR_NODE_CHAIN_ACCESS:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_LOAD_CHAIN,
                                .name = ast->data.chain_access.full_key,
                                .hash = cxpr_hash_string(ast->data.chain_access.full_key),
                            },
                            err);

    case CXPR_NODE_PRODUCER_ACCESS: {
        cxpr_func_entry* entry = cxpr_registry_find(reg, ast->data.producer_access.name);
        char* const_key = NULL;
        if (!entry || !entry->struct_producer) {
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = CXPR_OP_CALL_AST,
                                    .ast = ast,
                                },
                                err);
        }
        for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
            if (!cxpr_ir_compile_node(ast->data.producer_access.args[i], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
        }
        const_key = cxpr_ir_build_constant_producer_key(ast->data.producer_access.name,
                                                        (const cxpr_ast* const*)ast->data.producer_access.args,
                                                        ast->data.producer_access.argc);
        if (!cxpr_ir_emit(program,
                          (cxpr_ir_instr){
                              .op = const_key ? CXPR_OP_CALL_PRODUCER_CONST
                                              : CXPR_OP_CALL_PRODUCER,
                              .func = entry,
                              .name = const_key ? const_key : ast->data.producer_access.name,
                              .index = ast->data.producer_access.argc,
                          },
                          err)) {
            free(const_key);
            return false;
        }
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_GET_FIELD,
                                .name = ast->data.producer_access.field,
                            },
                            err);
    }

    case CXPR_NODE_FUNCTION_CALL: {
        cxpr_func_entry* entry = cxpr_registry_find(reg, ast->data.function_call.name);
        const char* fname = ast->data.function_call.name;
        if (!entry) {
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = CXPR_OP_CALL_AST,
                                    .ast = ast,
                                },
                                err);
        }

        if (strcmp(fname, "if") == 0 && ast->data.function_call.argc == 3) {
            size_t false_jump, end_jump;

            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            false_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP_IF_FALSE }, err)) {
                return false;
            }

            if (!cxpr_ir_compile_node(ast->data.function_call.args[1], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            end_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP }, err)) {
                return false;
            }

            cxpr_ir_patch_target(program, false_jump, cxpr_ir_next_index(program));
            if (!cxpr_ir_compile_node(ast->data.function_call.args[2], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }

            cxpr_ir_patch_target(program, end_jump, cxpr_ir_next_index(program));
            return true;
        }

        if (strcmp(fname, "sqrt") == 0 && ast->data.function_call.argc == 1) {
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_SQRT }, err);
        }

        if (strcmp(fname, "abs") == 0 && ast->data.function_call.argc == 1) {
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_ABS }, err);
        }

        if (strcmp(fname, "pow") == 0 && ast->data.function_call.argc == 2) {
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            if (!cxpr_ir_compile_node(ast->data.function_call.args[1], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_POW }, err);
        }

        if (strcmp(fname, "sign") == 0 && ast->data.function_call.argc == 1) {
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_SIGN }, err);
        }

        if (strcmp(fname, "floor") == 0 && ast->data.function_call.argc == 1) {
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_FLOOR }, err);
        }

        if (strcmp(fname, "ceil") == 0 && ast->data.function_call.argc == 1) {
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_CEIL }, err);
        }

        if (strcmp(fname, "round") == 0 && ast->data.function_call.argc == 1) {
            if (!cxpr_ir_compile_node(ast->data.function_call.args[0], program, reg,
                                      local_names, local_count, subst, inline_depth, err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_ROUND }, err);
        }

        if (strcmp(fname, "clamp") == 0 && ast->data.function_call.argc == 3) {
            size_t i;
            for (i = 0; i < 3; ++i) {
                if (!cxpr_ir_compile_node(ast->data.function_call.args[i], program, reg,
                                          local_names, local_count, subst, inline_depth, err)) {
                    return false;
                }
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_CLAMP }, err);
        }

        if (entry->sync_func && !entry->struct_fields && !entry->defined_body) {
            size_t i;
            for (i = 0; i < ast->data.function_call.argc; ++i) {
                if (!cxpr_ir_compile_node(ast->data.function_call.args[i], program, reg,
                                          local_names, local_count, subst, inline_depth, err)) {
                    return false;
                }
            }
            if (entry->native_kind == CXPR_NATIVE_KIND_UNARY &&
                ast->data.function_call.argc == 1) {
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = CXPR_OP_CALL_UNARY,
                                        .func = entry,
                                        .index = 1,
                                    },
                                    err);
            }
            if (entry->native_kind == CXPR_NATIVE_KIND_BINARY &&
                ast->data.function_call.argc == 2) {
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = CXPR_OP_CALL_BINARY,
                                        .func = entry,
                                        .index = 2,
                                    },
                                    err);
            }
            if (entry->native_kind == CXPR_NATIVE_KIND_TERNARY &&
                ast->data.function_call.argc == 3) {
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = CXPR_OP_CALL_TERNARY,
                                        .func = entry,
                                        .index = 3,
                                    },
                                    err);
            }
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = CXPR_OP_CALL_FUNC,
                                    .func = entry,
                                    .index = ast->data.function_call.argc,
                                },
                                err);
        }

        if (entry->struct_producer) {
            char* const_key = NULL;
            for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
                if (!cxpr_ir_compile_node(ast->data.function_call.args[i], program, reg,
                                          local_names, local_count, subst, inline_depth, err)) {
                    return false;
                }
            }
            const_key = cxpr_ir_build_constant_producer_key(ast->data.function_call.name,
                                                            (const cxpr_ast* const*)ast->data.function_call.args,
                                                            ast->data.function_call.argc);
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){
                                  .op = const_key ? CXPR_OP_CALL_PRODUCER_CONST
                                                  : CXPR_OP_CALL_PRODUCER,
                                  .func = entry,
                                  .name = const_key ? const_key : ast->data.function_call.name,
                                  .index = ast->data.function_call.argc,
                              },
                              err)) {
                free(const_key);
                return false;
            }
            return true;
        }

        if (entry->defined_body && cxpr_ir_defined_is_scalar_only(entry)) {
            if (inline_depth < CXPR_IR_INLINE_DEPTH_LIMIT) {
                cxpr_ir_subst_frame frame = {
                    .names = (const char* const*)entry->defined_param_names,
                    .args = (const cxpr_ast* const*)ast->data.function_call.args,
                    .count = ast->data.function_call.argc,
                    .parent = subst,
                };
                return cxpr_ir_compile_node(entry->defined_body, program, reg,
                                            local_names, local_count,
                                            &frame, inline_depth + 1, err);
            }

            {
                size_t i;
                for (i = 0; i < ast->data.function_call.argc; ++i) {
                    if (!cxpr_ir_compile_node(ast->data.function_call.args[i], program, reg,
                                              local_names, local_count, subst, inline_depth,
                                              err)) {
                        return false;
                    }
                }
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = CXPR_OP_CALL_DEFINED,
                                        .func = entry,
                                        .index = ast->data.function_call.argc,
                                    },
                                    err);
            }
        }

        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_CALL_AST,
                                .ast = ast,
                            },
                            err);
    }

    case CXPR_NODE_BINARY_OP:
        if (ast->data.binary_op.op == CXPR_TOK_AND) {
            size_t left_false_jump, right_false_jump, end_jump;

            if (!cxpr_ir_compile_node(ast->data.binary_op.left, program, reg,
                                      local_names, local_count, subst, inline_depth,
                                      err)) return false;
            left_false_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_JUMP_IF_FALSE },
                              err)) return false;

            if (!cxpr_ir_compile_node(ast->data.binary_op.right, program, reg,
                                      local_names, local_count, subst, inline_depth,
                                      err)) return false;
            right_false_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_JUMP_IF_FALSE },
                              err)) return false;

            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_PUSH_BOOL, .value = 1.0 },
                              err)) return false;
            end_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP }, err)) return false;

            cxpr_ir_patch_target(program, left_false_jump, cxpr_ir_next_index(program));
            cxpr_ir_patch_target(program, right_false_jump, cxpr_ir_next_index(program));
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_PUSH_BOOL, .value = 0.0 },
                              err)) return false;

            cxpr_ir_patch_target(program, end_jump, cxpr_ir_next_index(program));
            return true;
        }

        if (ast->data.binary_op.op == CXPR_TOK_OR) {
            size_t left_true_jump, right_false_jump, end_jump;

            if (!cxpr_ir_compile_node(ast->data.binary_op.left, program, reg,
                                      local_names, local_count, subst, inline_depth,
                                      err)) return false;
            left_true_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_JUMP_IF_TRUE },
                              err)) return false;

            if (!cxpr_ir_compile_node(ast->data.binary_op.right, program, reg,
                                      local_names, local_count, subst, inline_depth,
                                      err)) return false;
            right_false_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_JUMP_IF_FALSE },
                              err)) return false;

            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_PUSH_BOOL, .value = 1.0 },
                              err)) return false;
            end_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP }, err)) return false;

            cxpr_ir_patch_target(program, left_true_jump, cxpr_ir_next_index(program));
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_PUSH_BOOL, .value = 1.0 },
                              err)) return false;
            size_t skip_false_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP }, err)) return false;

            cxpr_ir_patch_target(program, right_false_jump, cxpr_ir_next_index(program));
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_PUSH_BOOL, .value = 0.0 },
                              err)) return false;

            cxpr_ir_patch_target(program, end_jump, cxpr_ir_next_index(program));
            cxpr_ir_patch_target(program, skip_false_jump, cxpr_ir_next_index(program));
            return true;
        }

        if (ast->data.binary_op.op == CXPR_TOK_STAR &&
            cxpr_ir_ast_equal(ast->data.binary_op.left, ast->data.binary_op.right)) {
            if (cxpr_ir_emit_leaf_load(ast->data.binary_op.left, program,
                                       local_names, local_count, subst, true, err)) {
                return true;
            }
            if (!cxpr_ir_compile_node(ast->data.binary_op.left, program, reg,
                                      local_names, local_count, subst, inline_depth,
                                      err)) {
                return false;
            }
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_SQUARE }, err);
        }

        if (!cxpr_ir_compile_node(ast->data.binary_op.left, program, reg,
                                  local_names, local_count, subst, inline_depth,
                                  err)) {
            return false;
        }
        if (!cxpr_ir_compile_node(ast->data.binary_op.right, program, reg,
                                  local_names, local_count, subst, inline_depth,
                                  err)) {
            return false;
        }
        switch (ast->data.binary_op.op) {
        case CXPR_TOK_PLUS:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_ADD, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_MINUS:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_SUB, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_STAR:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_MUL, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_SLASH:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_DIV, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_PERCENT:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_MOD, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_EQ:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_CMP_EQ, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_NEQ:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_CMP_NEQ, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_LT:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_CMP_LT, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_LTE:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_CMP_LTE, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_GT:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_CMP_GT, .value = 0.0, .name = NULL },
                                err);
        case CXPR_TOK_GTE:
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){ .op = CXPR_OP_CMP_GTE, .value = 0.0, .name = NULL },
                                err);
        default:
            if (err) {
                err->code = CXPR_ERR_SYNTAX;
                err->message = "IR v1 currently supports arithmetic and comparison binary operators";
            }
            return false;
        }

    case CXPR_NODE_UNARY_OP:
        if (ast->data.unary_op.op == CXPR_TOK_NOT) {
            if (!cxpr_ir_compile_node(ast->data.unary_op.operand, program, reg,
                                      local_names, local_count, subst, inline_depth,
                                      err)) return false;
            return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_NOT }, err);
        }

        if (ast->data.unary_op.op != CXPR_TOK_MINUS) {
            if (err) {
                err->code = CXPR_ERR_SYNTAX;
                err->message = "IR currently supports only unary minus and not";
            }
            return false;
        }
        if (!cxpr_ir_compile_node(ast->data.unary_op.operand, program, reg,
                                  local_names, local_count, subst, inline_depth,
                                  err)) {
            return false;
        }
        return cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_NEG }, err);

    case CXPR_NODE_TERNARY: {
        size_t false_jump, end_jump;

        if (!cxpr_ir_compile_node(ast->data.ternary.condition, program, reg,
                                  local_names, local_count, subst, inline_depth,
                                  err)) return false;
        false_jump = cxpr_ir_next_index(program);
        if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP_IF_FALSE }, err)) {
            return false;
        }

        if (!cxpr_ir_compile_node(ast->data.ternary.true_branch, program, reg,
                                  local_names, local_count, subst, inline_depth,
                                  err)) return false;
        end_jump = cxpr_ir_next_index(program);
        if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP }, err)) return false;

        cxpr_ir_patch_target(program, false_jump, cxpr_ir_next_index(program));
        if (!cxpr_ir_compile_node(ast->data.ternary.false_branch, program, reg,
                                  local_names, local_count, subst, inline_depth,
                                  err)) return false;

        cxpr_ir_patch_target(program, end_jump, cxpr_ir_next_index(program));
        return true;
    }

    default:
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message =
                "IR currently supports numeric literals, identifiers, parameters, field access, function-call fallback, unary -, not, arithmetic, comparisons, logical and/or, and ternary";
        }
        return false;
    }
}

/**
 * @brief Compile a supported AST into an internal IR program.
 *
 * V1 supports numeric literals, runtime identifiers, and $params.
 */
bool cxpr_ir_compile_with_locals(const cxpr_ast* ast, const cxpr_registry* reg,
                                 const char* const* local_names, size_t local_count,
                                 cxpr_ir_program* program, cxpr_error* err) {
    if (err) *err = (cxpr_error){0};
    if (!program) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "NULL IR program";
        }
        return false;
    }

    cxpr_ir_program_reset(program);
    program->ast = ast;
    program->fast_result_kind = cxpr_ir_infer_fast_result_kind(ast, reg, 0);

    if (!cxpr_ir_compile_node(ast, program, reg, local_names, local_count,
                              NULL, 0, err)) {
        cxpr_ir_program_reset(program);
        return false;
    }

    if (!cxpr_ir_emit(program,
                      (cxpr_ir_instr){
                          .op = CXPR_OP_RETURN,
                          .value = 0.0,
                          .name = NULL,
                      },
                      err)) {
        cxpr_ir_program_reset(program);
        return false;
    }

    program->lookup_cache =
        (cxpr_ir_lookup_cache*)calloc(program->count, sizeof(cxpr_ir_lookup_cache));
    if (program->count > 0 && !program->lookup_cache) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        cxpr_ir_program_reset(program);
        return false;
    }

    if (program->fast_result_kind != CXPR_IR_RESULT_UNKNOWN &&
        !cxpr_ir_validate_scalar_fast_program(program)) {
        program->fast_result_kind = CXPR_IR_RESULT_UNKNOWN;
    }

    return true;
}

bool cxpr_ir_compile(const cxpr_ast* ast, const cxpr_registry* reg, cxpr_ir_program* program,
                     cxpr_error* err) {
    return cxpr_ir_compile_with_locals(ast, reg, NULL, 0, program, err);
}

/**
 * @brief Evaluate an internal IR program against a context.
 *
 * IR currently supports loads, function-call fallback, arithmetic ops,
 * comparison ops, logical ops, jumps, and RETURN.
 */
double cxpr_ir_exec_with_locals(const cxpr_ir_program* program, const cxpr_context* ctx,
                                const cxpr_registry* reg, const double* locals,
                                size_t local_count, cxpr_error* err) {
    if (program && program->fast_result_kind == CXPR_IR_RESULT_DOUBLE) {
        return cxpr_ir_exec_scalar_fast(program, ctx, reg, locals, local_count, err);
    }
    cxpr_field_value value = cxpr_ir_exec_typed(program, ctx, reg, locals, local_count, err);
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

double cxpr_ir_exec(const cxpr_ir_program* program, const cxpr_context* ctx,
                    const cxpr_registry* reg, cxpr_error* err) {
    if (program && program->fast_result_kind == CXPR_IR_RESULT_DOUBLE) {
        return cxpr_ir_exec_scalar_fast(program, ctx, reg, NULL, 0, err);
    }
    cxpr_field_value value = cxpr_ir_exec_typed(program, ctx, reg, NULL, 0, err);
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

/* ═══════════════════════════════════════════════════════════════════════════
 * Public compiled program API
 * ═══════════════════════════════════════════════════════════════════════════ */

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

cxpr_field_value cxpr_ir_eval(const cxpr_program* prog, const cxpr_context* ctx,
                              const cxpr_registry* reg, cxpr_error* err) {
    if (!prog) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "NULL compiled program";
        }
        return cxpr_fv_double(NAN);
    }
    return cxpr_ir_exec_typed(&prog->ir, ctx, reg, NULL, 0, err);
}

double cxpr_ir_eval_double(const cxpr_program* prog, const cxpr_context* ctx,
                           const cxpr_registry* reg, cxpr_error* err) {
    if (prog && prog->ir.fast_result_kind == CXPR_IR_RESULT_DOUBLE) {
        return cxpr_ir_exec_scalar_fast(&prog->ir, ctx, reg, NULL, 0, err);
    }
    cxpr_field_value value = cxpr_ir_eval(prog, ctx, reg, err);
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

bool cxpr_ir_eval_bool(const cxpr_program* prog, const cxpr_context* ctx,
                       const cxpr_registry* reg, cxpr_error* err) {
    if (prog && prog->ir.fast_result_kind == CXPR_IR_RESULT_BOOL) {
        double value = cxpr_ir_exec_scalar_fast(&prog->ir, ctx, reg, NULL, 0, err);
        if (err && err->code != CXPR_OK) return false;
        return value != 0.0;
    }
    cxpr_field_value value = cxpr_ir_eval(prog, ctx, reg, err);
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
