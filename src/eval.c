/**
 * @file eval.c
 * @brief Expression evaluator for cxpr.
 *
 * Recursive tree-walk evaluator that traverses AST nodes.
 * Suitable for embedding in tools, services, and applications that need
 * deterministic expression evaluation.
 */

#include "internal.h"
#include <math.h>
#include <stdio.h>

static double cxpr_eval_node(const cxpr_ast* ast, const cxpr_context* ctx,
                             const cxpr_registry* reg, cxpr_error* err);

static double cxpr_eval_defined_function(cxpr_func_entry* entry, const cxpr_ast* call_ast,
                                         const cxpr_context* ctx, const cxpr_registry* reg,
                                         cxpr_error* err) {
    const size_t argc = call_ast->data.function_call.argc;
    cxpr_context* tmp;
    double scalar_args[32];
    bool scalar_only = true;

    if (argc != entry->defined_param_count) {
        if (err) { err->code = CXPR_ERR_WRONG_ARITY; err->message = "Wrong number of arguments"; }
        return NAN;
    }

    tmp = cxpr_context_overlay_new(ctx);
    if (!tmp) {
        if (err) { err->code = CXPR_ERR_OUT_OF_MEMORY; err->message = "Out of memory"; }
        return NAN;
    }

    for (size_t i = 0; i < entry->defined_param_count; i++) {
        const char* pname = entry->defined_param_names[i];
        const cxpr_ast* arg = call_ast->data.function_call.args[i];

        if (entry->defined_param_fields[i] && entry->defined_param_field_counts[i] > 0) {
            scalar_only = false;
            if (arg->type != CXPR_NODE_IDENTIFIER) {
                if (err) { err->code = CXPR_ERR_SYNTAX; err->message = "Struct argument must be an identifier"; }
                cxpr_context_free(tmp);
                return NAN;
            }

            for (size_t f = 0; f < entry->defined_param_field_counts[i]; f++) {
                const char* fld = entry->defined_param_fields[i][f];
                const char* obj = arg->data.identifier.name;
                char src_key[256], dst_key[256];
                bool found = false;
                double val;

                snprintf(src_key, sizeof(src_key), "%s.%s", obj, fld);
                snprintf(dst_key, sizeof(dst_key), "%s.%s", pname, fld);
                val = cxpr_context_get(ctx, src_key, &found);
                if (!found) {
                    if (err) { err->code = CXPR_ERR_UNKNOWN_IDENTIFIER; err->message = "Unknown struct field"; }
                    cxpr_context_free(tmp);
                    return NAN;
                }
                cxpr_context_set(tmp, dst_key, val);
            }
        } else {
            const double val = cxpr_eval_node(arg, ctx, reg, err);
            if (err && err->code != CXPR_OK) {
                cxpr_context_free(tmp);
                return NAN;
            }
            if (i < 32) scalar_args[i] = val;
            cxpr_context_set(tmp, pname, val);
        }
    }

    if (!entry->defined_program && !entry->defined_program_failed) {
        cxpr_error compile_err = {0};
        if (scalar_only && argc <= 32) {
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
        } else {
            entry->defined_program = cxpr_compile(entry->defined_body, reg, &compile_err);
        }
        if (!entry->defined_program) {
            entry->defined_program_failed = true;
        }
    }

    if (entry->defined_program) {
        const double result =
            (scalar_only && argc <= 32)
                ? cxpr_ir_eval_with_locals(&entry->defined_program->ir, ctx, reg,
                                           scalar_args, argc, err)
                : cxpr_program_eval(entry->defined_program, tmp, reg, err);
        cxpr_context_free(tmp);
        return result;
    }

    {
        const double result = cxpr_eval_node(entry->defined_body, tmp, reg, err);
        cxpr_context_free(tmp);
        return result;
    }
}

/**
 * @brief Internal recursive evaluator.
 */
static double cxpr_eval_node(const cxpr_ast* ast, const cxpr_context* ctx,
                            const cxpr_registry* reg, cxpr_error* err) {
    if (!ast) {
        if (err) { err->code = CXPR_ERR_SYNTAX; err->message = "NULL AST node"; }
        return NAN;
    }

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        return ast->data.number.value;

    case CXPR_NODE_IDENTIFIER: {
        bool found = false;
        double val = cxpr_context_get(ctx, ast->data.identifier.name, &found);
        if (!found) {
            if (err) {
                err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                err->message = "Unknown identifier";
            }
            return NAN;
        }
        return val;
    }

    case CXPR_NODE_VARIABLE: {
        bool found = false;
        double val = cxpr_context_get_param(ctx, ast->data.variable.name, &found);
        if (!found) {
            if (err) {
                err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                err->message = "Unknown parameter variable";
            }
            return NAN;
        }
        return val;
    }

    case CXPR_NODE_FIELD_ACCESS: {
        /* Lookup flat key (e.g., "macd.histogram") */
        bool found = false;
        double val = cxpr_context_get(ctx, ast->data.field_access.full_key, &found);
        if (!found) {
            if (err) {
                err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                err->message = "Unknown field access";
            }
            return NAN;
        }
        return val;
    }

    case CXPR_NODE_BINARY_OP: {
        /* Evaluate left and right */
        int op = ast->data.binary_op.op;

        /* Short-circuit for logical AND/OR */
        if (op == CXPR_TOK_AND) {
            double left = cxpr_eval_node(ast->data.binary_op.left, ctx, reg, err);
            if (err && err->code != CXPR_OK) return NAN;
            if (left == 0.0) return 0.0; /* Short-circuit */
            double right = cxpr_eval_node(ast->data.binary_op.right, ctx, reg, err);
            if (err && err->code != CXPR_OK) return NAN;
            return (right != 0.0) ? 1.0 : 0.0;
        }
        if (op == CXPR_TOK_OR) {
            double left = cxpr_eval_node(ast->data.binary_op.left, ctx, reg, err);
            if (err && err->code != CXPR_OK) return NAN;
            if (left != 0.0) return 1.0; /* Short-circuit */
            double right = cxpr_eval_node(ast->data.binary_op.right, ctx, reg, err);
            if (err && err->code != CXPR_OK) return NAN;
            return (right != 0.0) ? 1.0 : 0.0;
        }

        double left = cxpr_eval_node(ast->data.binary_op.left, ctx, reg, err);
        if (err && err->code != CXPR_OK) return NAN;
        double right = cxpr_eval_node(ast->data.binary_op.right, ctx, reg, err);
        if (err && err->code != CXPR_OK) return NAN;

        switch (op) {
        /* Arithmetic */
        case CXPR_TOK_PLUS:    return left + right;
        case CXPR_TOK_MINUS:   return left - right;
        case CXPR_TOK_STAR:    return left * right;
        case CXPR_TOK_SLASH:
            if (right == 0.0) {
                if (err) { err->code = CXPR_ERR_DIVISION_BY_ZERO; err->message = "Division by zero"; }
                return NAN;
            }
            return left / right;
        case CXPR_TOK_PERCENT:
            if (right == 0.0) {
                if (err) { err->code = CXPR_ERR_DIVISION_BY_ZERO; err->message = "Modulo by zero"; }
                return NAN;
            }
            return fmod(left, right);
        case CXPR_TOK_POWER:   return pow(left, right);

        /* Comparison */
        case CXPR_TOK_EQ:      return (left == right) ? 1.0 : 0.0;
        case CXPR_TOK_NEQ:     return (left != right) ? 1.0 : 0.0;
        case CXPR_TOK_LT:      return (left < right) ? 1.0 : 0.0;
        case CXPR_TOK_GT:      return (left > right) ? 1.0 : 0.0;
        case CXPR_TOK_LTE:     return (left <= right) ? 1.0 : 0.0;
        case CXPR_TOK_GTE:     return (left >= right) ? 1.0 : 0.0;

        default:
            if (err) { err->code = CXPR_ERR_SYNTAX; err->message = "Unknown binary operator"; }
            return NAN;
        }
    }

    case CXPR_NODE_UNARY_OP: {
        double operand = cxpr_eval_node(ast->data.unary_op.operand, ctx, reg, err);
        if (err && err->code != CXPR_OK) return NAN;

        switch (ast->data.unary_op.op) {
        case CXPR_TOK_MINUS: return -operand;
        case CXPR_TOK_NOT:   return (operand == 0.0) ? 1.0 : 0.0;
        default:
            if (err) { err->code = CXPR_ERR_SYNTAX; err->message = "Unknown unary operator"; }
            return NAN;
        }
    }

    case CXPR_NODE_FUNCTION_CALL: {
        const char* name = ast->data.function_call.name;
        size_t argc = ast->data.function_call.argc;

        /* Look up in registry */
        cxpr_func_entry* entry = cxpr_registry_find(reg, name);
        if (!entry) {
            if (err) { err->code = CXPR_ERR_UNKNOWN_FUNCTION; err->message = "Unknown function"; }
            return NAN;
        }

        /* Defined function (expression-based, registered via cxpr_registry_define) */
        if (entry->defined_body) {
            return cxpr_eval_defined_function(entry, ast, ctx, reg, err);
        }

        /* Struct-aware expansion: distance3(goal, pose) → goal.x,goal.y,goal.z,pose.x,... */
        if (entry->struct_fields) {
            if (argc != entry->struct_argc) {
                if (err) { err->code = CXPR_ERR_WRONG_ARITY; err->message = "Wrong number of struct arguments"; }
                return NAN;
            }
            double args[32];
            size_t out = 0;
            size_t expanded = entry->struct_argc * entry->fields_per_arg;
            if (expanded > 32) expanded = 32;
            for (size_t i = 0; i < entry->struct_argc && out < 32; i++) {
                const cxpr_ast* arg = ast->data.function_call.args[i];
                if (arg->type != CXPR_NODE_IDENTIFIER) {
                    if (err) { err->code = CXPR_ERR_SYNTAX; err->message = "Struct argument must be an identifier"; }
                    return NAN;
                }
                const char* obj = arg->data.identifier.name;
                for (size_t f = 0; f < entry->fields_per_arg && out < 32; f++) {
                    char key[256];
                    snprintf(key, sizeof(key), "%s.%s", obj, entry->struct_fields[f]);
                    bool found = false;
                    args[out++] = cxpr_context_get(ctx, key, &found);
                    if (!found) {
                        if (err) { err->code = CXPR_ERR_UNKNOWN_IDENTIFIER; err->message = "Unknown struct field"; }
                        return NAN;
                    }
                }
            }
            return entry->sync_func(args, out, entry->userdata);
        }

        /* Arity check */
        if (argc < entry->min_args || argc > entry->max_args) {
            if (err) { err->code = CXPR_ERR_WRONG_ARITY; err->message = "Wrong number of arguments"; }
            return NAN;
        }

        /* Evaluate arguments */
        double args[32]; /* Max 32 arguments */
        if (argc > 32) argc = 32;
        for (size_t i = 0; i < argc; i++) {
            args[i] = cxpr_eval_node(ast->data.function_call.args[i], ctx, reg, err);
            if (err && err->code != CXPR_OK) return NAN;
        }

        /* Call function */
        return entry->sync_func(args, argc, entry->userdata);
    }

    case CXPR_NODE_TERNARY: {
        double condition = cxpr_eval_node(ast->data.ternary.condition, ctx, reg, err);
        if (err && err->code != CXPR_OK) return NAN;

        if (condition != 0.0) {
            return cxpr_eval_node(ast->data.ternary.true_branch, ctx, reg, err);
        } else {
            return cxpr_eval_node(ast->data.ternary.false_branch, ctx, reg, err);
        }
    }

    default:
        if (err) { err->code = CXPR_ERR_SYNTAX; err->message = "Unknown AST node type"; }
        return NAN;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Evaluate an AST and return its numeric result.
 * @param[in] ast    Parsed AST to evaluate
 * @param[in] ctx    Variable/parameter context
 * @param[in] reg    Function registry
 * @param[out] err   Error output (can be NULL)
 * @return Evaluated result, or NAN on error
 */
double cxpr_eval(const cxpr_ast* ast, const cxpr_context* ctx,
               const cxpr_registry* reg, cxpr_error* err) {
    if (err) *err = (cxpr_error){0};
    return cxpr_eval_node(ast, ctx, reg, err);
}

/**
 * @brief Evaluate an AST as a boolean (non-zero = true).
 * @param[in] ast    Parsed AST to evaluate
 * @param[in] ctx    Variable/parameter context
 * @param[in] reg    Function registry
 * @param[out] err   Error output (can be NULL)
 * @return true if result is non-zero, false otherwise
 */
bool cxpr_eval_bool(const cxpr_ast* ast, const cxpr_context* ctx,
                  const cxpr_registry* reg, cxpr_error* err) {
    double result = cxpr_eval(ast, ctx, reg, err);
    if (err && err->code != CXPR_OK) return false;
    return result != 0.0;
}
