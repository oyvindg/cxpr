/**
 * @file ir.c
 * @brief Internal IR / compiled plan support for cxpr.
 *
 * This module starts as internal scaffolding only. The initial goal is to
 * establish a compilation/evaluation unit that can be expanded incrementally
 * without changing the public API.
 */

#include "internal.h"
#include <math.h>

#define CXPR_IR_INLINE_DEPTH_LIMIT 8

typedef struct cxpr_ir_subst_frame {
    const char* const* names;
    const cxpr_ast* const* args;
    size_t count;
    const struct cxpr_ir_subst_frame* parent;
} cxpr_ir_subst_frame;

/**
 * @brief Free the storage owned by an internal IR program.
 */
void cxpr_ir_program_reset(cxpr_ir_program* program) {
    if (!program) return;
    free(program->code);
    program->code = NULL;
    program->count = 0;
    program->capacity = 0;
}

/**
 * @brief Append one instruction to an IR program.
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

/** @brief Reserve one instruction slot and return its index. */
static size_t cxpr_ir_next_index(const cxpr_ir_program* program) {
    return program->count;
}

/** @brief Patch the jump target of an already-emitted instruction. */
static void cxpr_ir_patch_target(cxpr_ir_program* program, size_t at, size_t target) {
    if (!program || at >= program->count) return;
    program->code[at].index = target;
}

/** @brief Try to evaluate an AST subtree as a pure constant expression. */
static bool cxpr_ir_constant_value(const cxpr_ast* ast, double* out) {
    double left, right;

    if (!ast || !out) return false;

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        *out = ast->data.number.value;
        return true;

    case CXPR_NODE_UNARY_OP:
        if (!cxpr_ir_constant_value(ast->data.unary_op.operand, out)) return false;
        if (ast->data.unary_op.op == CXPR_TOK_MINUS) {
            *out = -*out;
            return true;
        }
        if (ast->data.unary_op.op == CXPR_TOK_NOT) {
            *out = (*out == 0.0) ? 1.0 : 0.0;
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
        case CXPR_TOK_EQ: *out = (left == right) ? 1.0 : 0.0; return true;
        case CXPR_TOK_NEQ: *out = (left != right) ? 1.0 : 0.0; return true;
        case CXPR_TOK_LT: *out = (left < right) ? 1.0 : 0.0; return true;
        case CXPR_TOK_LTE: *out = (left <= right) ? 1.0 : 0.0; return true;
        case CXPR_TOK_GT: *out = (left > right) ? 1.0 : 0.0; return true;
        case CXPR_TOK_GTE: *out = (left >= right) ? 1.0 : 0.0; return true;
        case CXPR_TOK_AND: *out = (left != 0.0 && right != 0.0) ? 1.0 : 0.0; return true;
        case CXPR_TOK_OR: *out = (left != 0.0 || right != 0.0) ? 1.0 : 0.0; return true;
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

/** @brief Check whether two AST subtrees are structurally identical. */
static bool cxpr_ir_ast_equal(const cxpr_ast* left, const cxpr_ast* right) {
    size_t i;

    if (left == right) return true;
    if (!left || !right || left->type != right->type) return false;

    switch (left->type) {
    case CXPR_NODE_NUMBER:
        return left->data.number.value == right->data.number.value;

    case CXPR_NODE_IDENTIFIER:
        return strcmp(left->data.identifier.name, right->data.identifier.name) == 0;

    case CXPR_NODE_VARIABLE:
        return strcmp(left->data.variable.name, right->data.variable.name) == 0;

    case CXPR_NODE_FIELD_ACCESS:
        return strcmp(left->data.field_access.full_key, right->data.field_access.full_key) == 0;

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

/** @brief Report a syntax-flavored IR runtime error and return NAN. */
static double cxpr_ir_runtime_error(cxpr_error* err, const char* message) {
    if (err) {
        err->code = CXPR_ERR_SYNTAX;
        err->message = message;
    }
    return NAN;
}

/** @brief Push one value onto the IR evaluation stack. */
static bool cxpr_ir_stack_push(double* stack, size_t* sp, double value,
                               size_t capacity, cxpr_error* err) {
    (void)stack;
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

/** @brief Ensure the IR stack contains at least the requested number of values. */
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

static bool cxpr_ir_defined_is_scalar_only(const cxpr_func_entry* entry) {
    if (!entry || !entry->defined_body) return false;
    for (size_t i = 0; i < entry->defined_param_count; ++i) {
        if (entry->defined_param_fields[i] && entry->defined_param_field_counts[i] > 0) {
            return false;
        }
    }
    return true;
}

static size_t cxpr_ir_local_index(const char* name, const char* const* local_names,
                                  size_t local_count) {
    size_t i;
    if (!name || !local_names) return (size_t)-1;
    for (i = 0; i < local_count; ++i) {
        if (local_names[i] && strcmp(local_names[i], name) == 0) return i;
    }
    return (size_t)-1;
}

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

/** @brief Emit a leaf load for an AST subtree, optionally squared in-place. */
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

static double cxpr_ir_context_get_prehashed(const cxpr_context* ctx, const char* name,
                                            unsigned long hash, bool* found) {
    if (!ctx) {
        if (found) *found = false;
        return 0.0;
    }

    {
        const double value = cxpr_hashmap_get_prehashed(&ctx->variables, name, hash, found);
        if (found && *found) return value;
    }
    if (ctx->parent) return cxpr_ir_context_get_prehashed(ctx->parent, name, hash, found);
    if (found) *found = false;
    return 0.0;
}

static double cxpr_ir_context_get_param_prehashed(const cxpr_context* ctx, const char* name,
                                                  unsigned long hash, bool* found) {
    if (!ctx) {
        if (found) *found = false;
        return 0.0;
    }

    {
        const double value = cxpr_hashmap_get_prehashed(&ctx->params, name, hash, found);
        if (found && *found) return value;
    }
    if (ctx->parent) {
        return cxpr_ir_context_get_param_prehashed(ctx->parent, name, hash, found);
    }
    if (found) *found = false;
    return 0.0;
}

static double cxpr_ir_call_defined_scalar(cxpr_func_entry* entry, const cxpr_context* ctx,
                                          const cxpr_registry* reg, const double* args,
                                          size_t argc, cxpr_error* err) {
    if (!entry || !entry->defined_body) {
        return cxpr_ir_runtime_error(err, "NULL IR defined function entry");
    }
    if (argc != entry->defined_param_count) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Wrong number of arguments";
        }
        return NAN;
    }

    if (!entry->defined_program && !entry->defined_program_failed) {
        cxpr_error compile_err = {0};
        entry->defined_program = (cxpr_program*)calloc(1, sizeof(cxpr_program));
        if (entry->defined_program) {
            if (!cxpr_ir_compile_with_locals(entry->defined_body, reg,
                                            (const char* const*)entry->defined_param_names,
                                            entry->defined_param_count,
                                            &entry->defined_program->ir, &compile_err)) {
                free(entry->defined_program);
                entry->defined_program = NULL;
            }
        } else {
            compile_err.code = CXPR_ERR_OUT_OF_MEMORY;
            compile_err.message = "Out of memory";
        }
        if (!entry->defined_program) {
            entry->defined_program_failed = true;
        }
    }

    if (entry->defined_program) {
        return cxpr_ir_eval_with_locals(&entry->defined_program->ir, ctx, reg, args, argc, err);
    }

    return cxpr_ir_runtime_error(err, "Scalar defined function missing compiled program");
}

/**
 * @brief Recursively compile a supported AST subtree into IR instructions.
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
                                .value = 0.0,
                                .name = ast->data.identifier.name,
                                .hash = cxpr_hash_string(ast->data.identifier.name),
                            },
                            err);

    case CXPR_NODE_VARIABLE:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_LOAD_PARAM,
                                .value = 0.0,
                                .name = ast->data.variable.name,
                                .hash = cxpr_hash_string(ast->data.variable.name),
                            },
                            err);

    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = CXPR_OP_LOAD_FIELD,
                                .value = 0.0,
                                .name = ast->data.field_access.full_key,
                                .hash = cxpr_hash_string(ast->data.field_access.full_key),
                            },
                            err);

    case CXPR_NODE_FUNCTION_CALL: {
        cxpr_func_entry* entry = cxpr_registry_find(reg, ast->data.function_call.name);
        const char* fname = ast->data.function_call.name;
        if (!entry) {
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = CXPR_OP_CALL_AST,
                                    .value = 0.0,
                                    .ast = ast,
                                },
                                err);
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

        if (entry->sync_func && !entry->struct_fields && !entry->defined_body) {
            size_t i;
            for (i = 0; i < ast->data.function_call.argc; ++i) {
                if (!cxpr_ir_compile_node(ast->data.function_call.args[i], program, reg,
                                          local_names, local_count, subst, inline_depth, err)) {
                    return false;
                }
            }
            return cxpr_ir_emit(program,
                                (cxpr_ir_instr){
                                    .op = CXPR_OP_CALL_FUNC,
                                    .func = entry,
                                    .index = ast->data.function_call.argc,
                                },
                                err);
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
                                .value = 0.0,
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
                              (cxpr_ir_instr){ .op = CXPR_OP_PUSH_CONST, .value = 1.0 },
                              err)) return false;
            end_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP }, err)) return false;

            cxpr_ir_patch_target(program, left_false_jump, cxpr_ir_next_index(program));
            cxpr_ir_patch_target(program, right_false_jump, cxpr_ir_next_index(program));
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_PUSH_CONST, .value = 0.0 },
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
                              (cxpr_ir_instr){ .op = CXPR_OP_PUSH_CONST, .value = 1.0 },
                              err)) return false;
            end_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP }, err)) return false;

            cxpr_ir_patch_target(program, left_true_jump, cxpr_ir_next_index(program));
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_PUSH_CONST, .value = 1.0 },
                              err)) return false;
            size_t skip_false_jump = cxpr_ir_next_index(program);
            if (!cxpr_ir_emit(program, (cxpr_ir_instr){ .op = CXPR_OP_JUMP }, err)) return false;

            cxpr_ir_patch_target(program, right_false_jump, cxpr_ir_next_index(program));
            if (!cxpr_ir_emit(program,
                              (cxpr_ir_instr){ .op = CXPR_OP_PUSH_CONST, .value = 0.0 },
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
double cxpr_ir_eval_with_locals(const cxpr_ir_program* program, const cxpr_context* ctx,
                                const cxpr_registry* reg, const double* locals,
                                size_t local_count, cxpr_error* err) {
    if (err) *err = (cxpr_error){0};

    if (!program || !program->code || program->count == 0) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Empty IR program";
        }
        return NAN;
    }

    double stack[64];
    const size_t stack_capacity = sizeof(stack) / sizeof(stack[0]);
    size_t sp = 0;
    const cxpr_ir_instr* code = program->code;

    for (size_t ip = 0; ip < program->count; ip++) {
        const cxpr_ir_instr* instr = &code[ip];
        switch (instr->op) {
        case CXPR_OP_PUSH_CONST:
            if (!cxpr_ir_stack_push(stack, &sp, instr->value, stack_capacity, err)) return NAN;
            break;

        case CXPR_OP_LOAD_LOCAL:
            if (!locals || instr->index >= local_count) {
                return cxpr_ir_runtime_error(err, "Invalid IR local slot");
            }
            if (!cxpr_ir_stack_push(stack, &sp, locals[instr->index], stack_capacity, err)) {
                return NAN;
            }
            break;

        case CXPR_OP_LOAD_LOCAL_SQUARE: {
            double value;
            if (!locals || instr->index >= local_count) {
                return cxpr_ir_runtime_error(err, "Invalid IR local slot");
            }
            value = locals[instr->index];
            if (!cxpr_ir_stack_push(stack, &sp, value * value, stack_capacity, err)) {
                return NAN;
            }
            break;
        }

        case CXPR_OP_LOAD_VAR: {
            bool found = false;
            double value = cxpr_ir_context_get_prehashed(ctx, instr->name, instr->hash, &found);
            if (!found) {
                if (err) {
                    err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                    err->message = "Unknown identifier";
                }
                return NAN;
            }
            if (!cxpr_ir_stack_push(stack, &sp, value, stack_capacity, err)) return NAN;
            break;
        }

        case CXPR_OP_LOAD_VAR_SQUARE: {
            bool found = false;
            double value = cxpr_ir_context_get_prehashed(ctx, instr->name, instr->hash, &found);
            if (!found) {
                if (err) {
                    err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                    err->message = "Unknown identifier";
                }
                return NAN;
            }
            if (!cxpr_ir_stack_push(stack, &sp, value * value, stack_capacity, err)) return NAN;
            break;
        }

        case CXPR_OP_LOAD_PARAM: {
            bool found = false;
            double value =
                cxpr_ir_context_get_param_prehashed(ctx, instr->name, instr->hash, &found);
            if (!found) {
                if (err) {
                    err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                    err->message = "Unknown parameter variable";
                }
                return NAN;
            }
            if (!cxpr_ir_stack_push(stack, &sp, value, stack_capacity, err)) return NAN;
            break;
        }

        case CXPR_OP_LOAD_PARAM_SQUARE: {
            bool found = false;
            double value =
                cxpr_ir_context_get_param_prehashed(ctx, instr->name, instr->hash, &found);
            if (!found) {
                if (err) {
                    err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                    err->message = "Unknown parameter variable";
                }
                return NAN;
            }
            if (!cxpr_ir_stack_push(stack, &sp, value * value, stack_capacity, err)) return NAN;
            break;
        }

        case CXPR_OP_LOAD_FIELD: {
            bool found = false;
            double value = cxpr_ir_context_get_prehashed(ctx, instr->name, instr->hash, &found);
            if (!found) {
                if (err) {
                    err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                    err->message = "Unknown field access";
                }
                return NAN;
            }
            if (!cxpr_ir_stack_push(stack, &sp, value, stack_capacity, err)) return NAN;
            break;
        }

        case CXPR_OP_LOAD_FIELD_SQUARE: {
            bool found = false;
            double value = cxpr_ir_context_get_prehashed(ctx, instr->name, instr->hash, &found);
            if (!found) {
                if (err) {
                    err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                    err->message = "Unknown field access";
                }
                return NAN;
            }
            if (!cxpr_ir_stack_push(stack, &sp, value * value, stack_capacity, err)) return NAN;
            break;
        }

        case CXPR_OP_ADD:
            if (!cxpr_ir_require_stack(sp, 2, err)) return NAN;
            stack[sp - 2] = stack[sp - 2] + stack[sp - 1];
            sp--;
            break;

        case CXPR_OP_SUB:
            if (!cxpr_ir_require_stack(sp, 2, err)) return NAN;
            stack[sp - 2] = stack[sp - 2] - stack[sp - 1];
            sp--;
            break;

        case CXPR_OP_MUL:
            if (!cxpr_ir_require_stack(sp, 2, err)) return NAN;
            stack[sp - 2] = stack[sp - 2] * stack[sp - 1];
            sp--;
            break;

        case CXPR_OP_SQUARE:
            if (!cxpr_ir_require_stack(sp, 1, err)) return NAN;
            stack[sp - 1] = stack[sp - 1] * stack[sp - 1];
            break;

        case CXPR_OP_DIV:
            if (!cxpr_ir_require_stack(sp, 2, err)) return NAN;
            if (stack[sp - 1] == 0.0) {
                if (err) {
                    err->code = CXPR_ERR_DIVISION_BY_ZERO;
                    err->message = "Division by zero";
                }
                return NAN;
            }
            stack[sp - 2] = stack[sp - 2] / stack[sp - 1];
            sp--;
            break;

        case CXPR_OP_CMP_EQ:
            if (!cxpr_ir_require_stack(sp, 2, err)) return NAN;
            stack[sp - 2] = (stack[sp - 2] == stack[sp - 1]) ? 1.0 : 0.0;
            sp--;
            break;

        case CXPR_OP_CMP_NEQ:
            if (!cxpr_ir_require_stack(sp, 2, err)) return NAN;
            stack[sp - 2] = (stack[sp - 2] != stack[sp - 1]) ? 1.0 : 0.0;
            sp--;
            break;

        case CXPR_OP_CMP_LT:
            if (!cxpr_ir_require_stack(sp, 2, err)) return NAN;
            stack[sp - 2] = (stack[sp - 2] < stack[sp - 1]) ? 1.0 : 0.0;
            sp--;
            break;

        case CXPR_OP_CMP_LTE:
            if (!cxpr_ir_require_stack(sp, 2, err)) return NAN;
            stack[sp - 2] = (stack[sp - 2] <= stack[sp - 1]) ? 1.0 : 0.0;
            sp--;
            break;

        case CXPR_OP_CMP_GT:
            if (!cxpr_ir_require_stack(sp, 2, err)) return NAN;
            stack[sp - 2] = (stack[sp - 2] > stack[sp - 1]) ? 1.0 : 0.0;
            sp--;
            break;

        case CXPR_OP_CMP_GTE:
            if (!cxpr_ir_require_stack(sp, 2, err)) return NAN;
            stack[sp - 2] = (stack[sp - 2] >= stack[sp - 1]) ? 1.0 : 0.0;
            sp--;
            break;

        case CXPR_OP_NOT:
            if (!cxpr_ir_require_stack(sp, 1, err)) return NAN;
            stack[sp - 1] = (stack[sp - 1] == 0.0) ? 1.0 : 0.0;
            break;

        case CXPR_OP_NEG:
            if (!cxpr_ir_require_stack(sp, 1, err)) return NAN;
            stack[sp - 1] = -stack[sp - 1];
            break;

        case CXPR_OP_SQRT:
            if (!cxpr_ir_require_stack(sp, 1, err)) return NAN;
            stack[sp - 1] = sqrt(stack[sp - 1]);
            break;

        case CXPR_OP_ABS:
            if (!cxpr_ir_require_stack(sp, 1, err)) return NAN;
            stack[sp - 1] = fabs(stack[sp - 1]);
            break;

        case CXPR_OP_POW:
            if (!cxpr_ir_require_stack(sp, 2, err)) return NAN;
            stack[sp - 2] = pow(stack[sp - 2], stack[sp - 1]);
            sp--;
            break;

        case CXPR_OP_CALL_FUNC: {
            const size_t argc = instr->index;
            double args[32];
            size_t i;

            if (!cxpr_ir_require_stack(sp, argc, err)) return NAN;
            if (!instr->func) return cxpr_ir_runtime_error(err, "NULL IR function entry");
            if (argc < instr->func->min_args || argc > instr->func->max_args) {
                if (err) {
                    err->code = CXPR_ERR_WRONG_ARITY;
                    err->message = "Wrong number of arguments";
                }
                return NAN;
            }
            if (argc > 32) return cxpr_ir_runtime_error(err, "IR function argc overflow");

            for (i = 0; i < argc; ++i) {
                args[i] = stack[sp - argc + i];
            }
            sp -= argc;

            if (!cxpr_ir_stack_push(stack, &sp,
                                    instr->func->sync_func(args, argc, instr->func->userdata),
                                    stack_capacity, err)) {
                return NAN;
            }
            break;
        }

        case CXPR_OP_CALL_DEFINED: {
            const size_t argc = instr->index;
            double args[32];
            size_t i;

            if (!cxpr_ir_require_stack(sp, argc, err)) return NAN;
            if (!instr->func) return cxpr_ir_runtime_error(err, "NULL IR defined function entry");
            if (argc > 32) return cxpr_ir_runtime_error(err, "IR defined argc overflow");

            for (i = 0; i < argc; ++i) {
                args[i] = stack[sp - argc + i];
            }
            sp -= argc;

            if (!cxpr_ir_stack_push(stack, &sp,
                                    cxpr_ir_call_defined_scalar((cxpr_func_entry*)instr->func,
                                                                ctx, reg, args, argc, err),
                                    stack_capacity, err)) {
                return NAN;
            }
            if (err && err->code != CXPR_OK) return NAN;
            break;
        }

        case CXPR_OP_CALL_AST: {
            double value = cxpr_eval(instr->ast, ctx, reg, err);
            if (err && err->code != CXPR_OK) return NAN;
            if (!cxpr_ir_stack_push(stack, &sp, value, stack_capacity, err)) return NAN;
            break;
        }

        case CXPR_OP_JUMP:
            ip = instr->index - 1;
            break;

        case CXPR_OP_JUMP_IF_FALSE:
            if (!cxpr_ir_require_stack(sp, 1, err)) return NAN;
            if (stack[--sp] == 0.0) ip = instr->index - 1;
            break;

        case CXPR_OP_JUMP_IF_TRUE:
            if (!cxpr_ir_require_stack(sp, 1, err)) return NAN;
            if (stack[--sp] != 0.0) ip = instr->index - 1;
            break;

        case CXPR_OP_RETURN:
            if (!cxpr_ir_require_stack(sp, 1, err)) return NAN;
            return stack[sp - 1];

        default:
            return cxpr_ir_runtime_error(err, "Unsupported IR opcode");
        }
    }

    return cxpr_ir_runtime_error(err, "IR program missing return");
}

double cxpr_ir_eval(const cxpr_ir_program* program, const cxpr_context* ctx,
                    const cxpr_registry* reg, cxpr_error* err) {
    return cxpr_ir_eval_with_locals(program, ctx, reg, NULL, 0, err);
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

    if (!cxpr_ir_compile(ast, reg, &prog->ir, err)) {
        free(prog);
        return NULL;
    }

    return prog;
}

double cxpr_program_eval(const cxpr_program* prog, const cxpr_context* ctx,
                         const cxpr_registry* reg, cxpr_error* err) {
    if (!prog) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "NULL compiled program";
        }
        return NAN;
    }
    return cxpr_ir_eval(&prog->ir, ctx, reg, err);
}

bool cxpr_program_eval_bool(const cxpr_program* prog, const cxpr_context* ctx,
                            const cxpr_registry* reg, cxpr_error* err) {
    const double result = cxpr_program_eval(prog, ctx, reg, err);
    if (err && err->code != CXPR_OK) return false;
    return result != 0.0;
}

void cxpr_program_free(cxpr_program* prog) {
    if (!prog) return;
    cxpr_ir_program_reset(&prog->ir);
    free(prog);
}
