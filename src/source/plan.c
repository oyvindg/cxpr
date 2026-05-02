/**
 * @file source_plan.c
 * @brief Bridge source-plan parsing implementation.
 */

#include "internal.h"

#include <cxpr/runtime_call.h>

#include <stdlib.h>
#include <string.h>

static int cxpr_source_plan_is_direct_source_name(const cxpr_provider* provider, const char* name) {
    return provider != NULL &&
           name != NULL &&
           cxpr_provider_source_spec_find(provider, name) != NULL;
}

static int cxpr_source_plan_supports_scalar_source_name(const cxpr_provider* provider,
                                            const char* name,
                                            size_t* min_args,
                                            size_t* max_args) {
    const cxpr_provider_fn_spec* spec;

    if (!provider || !name) return 0;
    spec = cxpr_provider_fn_spec_find(provider, name);
    if (!spec || (spec->flags & CXPR_PROVIDER_FN_SOURCE_INPUT) == 0u) return 0;

    if (min_args) *min_args = spec->source_min_args;
    if (max_args) *max_args = spec->source_max_args;
    return 1;
}

static const cxpr_provider_fn_spec* cxpr_source_plan_fn_spec(
    const cxpr_provider* provider,
    const char* name) {
    if (!provider || !name) return NULL;
    return cxpr_provider_fn_spec_find(provider, name);
}

static size_t cxpr_source_plan_positional_count(const cxpr_ast* ast) {
    size_t argc;
    size_t i;

    argc = cxpr_ast_type(ast) == CXPR_NODE_PRODUCER_ACCESS
               ? cxpr_ast_producer_argc(ast)
               : cxpr_ast_function_argc(ast);
    for (i = 0u; i < argc; ++i) {
        const char* arg_name =
            cxpr_ast_type(ast) == CXPR_NODE_PRODUCER_ACCESS
                ? cxpr_ast_producer_arg_name(ast, i)
                : cxpr_ast_function_arg_name(ast, i);
        if (arg_name) return i;
    }
    return argc;
}

static const cxpr_ast* cxpr_source_plan_arg_raw(
    const cxpr_ast* ast,
    size_t index) {
    if (!ast) return NULL;
    if (cxpr_ast_type(ast) == CXPR_NODE_PRODUCER_ACCESS) {
        return cxpr_ast_producer_arg(ast, index);
    }
    return cxpr_ast_function_arg(ast, index);
}

static const char* cxpr_source_plan_arg_name(
    const cxpr_ast* ast,
    size_t index) {
    if (!ast) return NULL;
    if (cxpr_ast_type(ast) == CXPR_NODE_PRODUCER_ACCESS) {
        return cxpr_ast_producer_arg_name(ast, index);
    }
    return cxpr_ast_function_arg_name(ast, index);
}

static const cxpr_ast* cxpr_source_plan_find_named_arg(
    const cxpr_ast* ast,
    const char* name) {
    size_t argc;
    size_t i;

    if (!ast || !name || name[0] == '\0') return NULL;
    argc = cxpr_ast_type(ast) == CXPR_NODE_PRODUCER_ACCESS
               ? cxpr_ast_producer_argc(ast)
               : cxpr_ast_function_argc(ast);
    for (i = 0u; i < argc; ++i) {
        const char* arg_name = cxpr_source_plan_arg_name(ast, i);
        if (!arg_name || strcmp(arg_name, name) != 0) continue;
        return cxpr_source_plan_arg_raw(ast, i);
    }
    return NULL;
}

static size_t cxpr_source_plan_numeric_param_count(
    const cxpr_provider* provider,
    const char* name,
    size_t fallback_count) {
    cxpr_expr_param_spec spec;
    size_t count = 0u;
    size_t i;

    if (!provider || !name ||
        !cxpr_provider_expr_param_spec_for(provider, name, &spec)) {
        return fallback_count;
    }
    for (i = 0u; i < spec.count; ++i) {
        if (spec.kinds && spec.kinds[i] == CXPR_EXPR_ARG_SCALAR_SOURCE) continue;
        ++count;
    }
    return count;
}

static const cxpr_ast* cxpr_source_plan_numeric_arg(
    const cxpr_provider* provider,
    const char* name,
    const cxpr_ast* ast,
    size_t numeric_index) {
    cxpr_expr_param_spec spec;
    size_t count = 0u;
    size_t i;

    if (!provider || !name ||
        !cxpr_provider_expr_param_spec_for(provider, name, &spec)) {
        return cxpr_provider_runtime_call_arg(provider, ast, numeric_index);
    }
    for (i = 0u; i < spec.count; ++i) {
        if (spec.kinds && spec.kinds[i] == CXPR_EXPR_ARG_SCALAR_SOURCE) continue;
        if (count == numeric_index) {
            return cxpr_provider_runtime_call_arg(provider, ast, i);
        }
        ++count;
    }
    return NULL;
}

void cxpr_free_source_plan_ast(cxpr_source_plan_ast* plan) {
    if (!plan) return;
    cxpr_source_plan_node_clear(&plan->root);
    free(plan->bound_arg_asts);
    free(plan->canonical);
    memset(plan, 0, sizeof(*plan));
}

static int cxpr_source_plan_bound_arg_append(const cxpr_ast* ast,
                                             cxpr_source_plan_ast* plan,
                                             size_t* out_slot) {
    const cxpr_ast** grown;
    size_t next_count;

    if (!plan || !out_slot) return 0;
    next_count = plan->arg_count + 1u;
    grown = realloc(plan->bound_arg_asts, next_count * sizeof(*grown));
    if (!grown) return 0;
    plan->bound_arg_asts = grown;
    plan->bound_arg_asts[plan->arg_count] = ast;
    *out_slot = plan->arg_count;
    plan->arg_count = next_count;
    return 1;
}

static int cxpr_source_plan_node_set_scope_value(cxpr_source_plan_node* node, const char* text) {
    if (!node || !text || text[0] == '\0') return 0;

    node->scope_value = cxpr_source_plan_strdup(text);
    return node->scope_value != NULL;
}

static int cxpr_source_plan_node_parse(const cxpr_provider* provider,
                                       const cxpr_ast* ast,
                                       cxpr_source_plan_ast* plan,
                                       cxpr_source_plan_node* out);

/**
 * @brief Check whether an AST subtree references a source field or indicator.
 *
 * Returns non-zero when the tree contains at least one identifier that is a
 * known direct source name, or at least one function/producer call. Pure
 * numeric arithmetic (`5 * 2`) returns 0 so that it is not treated as a
 * series source plan.
 */
static int cxpr_source_plan_ast_has_series_reference(const cxpr_provider* provider,
                                                const cxpr_ast* ast) {
    if (!ast) return 0;
    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_IDENTIFIER:
            return cxpr_source_plan_is_direct_source_name(provider, cxpr_ast_identifier_name(ast));
        case CXPR_NODE_FUNCTION_CALL:
        case CXPR_NODE_PRODUCER_ACCESS:
            return 1;
        case CXPR_NODE_VARIABLE:
            return 0;
        case CXPR_NODE_BINARY_OP:
            return cxpr_source_plan_ast_has_series_reference(provider, cxpr_ast_left(ast)) ||
                   cxpr_source_plan_ast_has_series_reference(provider, cxpr_ast_right(ast));
        case CXPR_NODE_UNARY_OP:
            return cxpr_source_plan_ast_has_series_reference(provider, cxpr_ast_operand(ast));
        case CXPR_NODE_LOOKBACK:
            return cxpr_source_plan_ast_has_series_reference(provider, cxpr_ast_lookback_target(ast));
        case CXPR_NODE_TERNARY:
            return cxpr_source_plan_ast_has_series_reference(provider, cxpr_ast_ternary_condition(ast)) ||
                   cxpr_source_plan_ast_has_series_reference(provider, cxpr_ast_ternary_true_branch(ast)) ||
                   cxpr_source_plan_ast_has_series_reference(provider, cxpr_ast_ternary_false_branch(ast));
        default:
            return 0;
    }
}

static int cxpr_source_plan_parse_function_source(const char* name,
                                      size_t argc,
                                      const cxpr_provider* provider,
                                      const cxpr_ast* ast,
                                      const char* field_name,
                                      cxpr_source_plan_ast* plan,
                                      cxpr_source_plan_node* out) {
    const cxpr_provider_fn_spec* fn_spec = NULL;
    size_t source_min_args = 0u;
    size_t source_max_args = 0u;
    size_t bound_arg_count = argc;
    size_t source_arg_count = argc;
    size_t positional_count = 0u;
    size_t numeric_param_count = 0u;
    size_t index;
    const char* timeframe = NULL;
    const cxpr_ast* source_ast = NULL;
    cxpr_runtime_call call = {0};
    int explicit_named_source = 0;
    int explicit_positional_source = 0;
    int source_is_record_output = 0;

    if (!name || !plan || !out) return 0;
    fn_spec = cxpr_source_plan_fn_spec(provider, name);
    positional_count = cxpr_source_plan_positional_count(ast);
    numeric_param_count = fn_spec ? fn_spec->param_count : 0u;

    if (cxpr_parse_runtime_call_provider(provider, ast, &call)) {
        timeframe = call.scope_value;
        bound_arg_count = call.value_arg_count;
        source_arg_count = call.scope_value && call.arg_count > 0u
                               ? call.arg_count - 1u
                               : call.arg_count;
    }

    if (cxpr_source_plan_is_direct_source_name(provider, name) && bound_arg_count == 0u) {
        out->kind = CXPR_SOURCE_PLAN_FIELD;
        out->name = cxpr_source_plan_strdup(name);
        if (!out->name) return 0;
        if (timeframe && !cxpr_source_plan_node_set_scope_value(out, timeframe)) return 0;
        return 1;
    }

    if (cxpr_source_plan_supports_scalar_source_name(provider, name, &source_min_args, &source_max_args)) {
        source_ast = cxpr_source_plan_find_named_arg(ast, "source");
        explicit_named_source = source_ast != NULL;
        source_is_record_output = fn_spec != NULL &&
            (fn_spec->flags & CXPR_PROVIDER_FN_RECORD_OUTPUT) != 0u;
        if (!explicit_named_source &&
            source_arg_count >= 1u + source_min_args &&
            source_arg_count <= 1u + source_max_args &&
            positional_count == source_arg_count) {
            source_ast = cxpr_source_plan_arg_raw(ast, 0u);
            explicit_positional_source = source_ast != NULL;
        }
    }

    if (source_ast) {
        cxpr_source_plan_node* child = calloc(1u, sizeof(*child));
        if (!child) return 0;
        child->lookback_slot = SIZE_MAX;
        if (!cxpr_source_plan_node_parse(provider, source_ast, plan, child)) {
            free(child);
            child = NULL;
        } else {
            if (timeframe &&
                child->scope_value == NULL &&
                (child->kind == CXPR_SOURCE_PLAN_FIELD ||
                 child->kind == CXPR_SOURCE_PLAN_INDICATOR)) {
                if (!cxpr_source_plan_node_set_scope_value(child, timeframe) ||
                    !cxpr_source_plan_finalize_node_canonical(plan, child)) {
                    cxpr_source_plan_node_clear(child);
                    free(child);
                    return 0;
                }
            }
            out->kind = source_is_record_output
                            ? CXPR_SOURCE_PLAN_INDICATOR
                            : CXPR_SOURCE_PLAN_SMOOTHING;
            out->name = cxpr_source_plan_strdup(name);
            if (!out->name) {
                cxpr_source_plan_node_clear(child);
                free(child);
                return 0;
            }
            out->source = child;
            if (field_name && field_name[0] != '\0') {
                out->field_name = cxpr_source_plan_strdup(field_name);
                if (!out->field_name) return 0;
            }
            if (timeframe) {
                if (!cxpr_source_plan_node_set_scope_value(out, timeframe)) return 0;
            }
            out->arg_count = cxpr_source_plan_numeric_param_count(
                provider,
                name,
                numeric_param_count);
            if (out->arg_count > 0u) {
                out->arg_slots = calloc(out->arg_count, sizeof(*out->arg_slots));
                if (!out->arg_slots) return 0;
            }
            for (index = 0u; index < out->arg_count; ++index) {
                const cxpr_ast* arg_ast = NULL;
                if (explicit_positional_source && positional_count == source_arg_count) {
                    arg_ast = cxpr_source_plan_arg_raw(ast, index + 1u);
                } else {
                    arg_ast = cxpr_source_plan_numeric_arg(
                        provider,
                        name,
                        ast,
                        index);
                }
                if (!arg_ast ||
                    !cxpr_source_plan_bound_arg_append(
                        arg_ast,
                        plan,
                        &out->arg_slots[index])) {
                    return 0;
                }
            }
            return 1;
        }
    }

    out->kind = CXPR_SOURCE_PLAN_INDICATOR;
    out->name = cxpr_source_plan_strdup(name);
    if (!out->name) return 0;
    if (field_name && field_name[0] != '\0') {
        out->field_name = cxpr_source_plan_strdup(field_name);
        if (!out->field_name) return 0;
    }
    if (timeframe) {
        if (!cxpr_source_plan_node_set_scope_value(out, timeframe)) return 0;
    }
    out->arg_count = bound_arg_count;
    if (bound_arg_count > 0u) {
        out->arg_slots = calloc(bound_arg_count, sizeof(*out->arg_slots));
        if (!out->arg_slots) return 0;
    }
    for (index = 0u; index < bound_arg_count; ++index) {
        if (!cxpr_source_plan_bound_arg_append(
                cxpr_provider_runtime_call_arg(provider, ast, index),
                plan,
                &out->arg_slots[index])) {
            return 0;
        }
    }
    return 1;
}

static int cxpr_source_plan_node_parse(const cxpr_provider* provider,
                                       const cxpr_ast* ast,
                                       cxpr_source_plan_ast* plan,
                                       cxpr_source_plan_node* out) {
    const char* name;

    if (!ast || !plan || !out) return 0;

    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_IDENTIFIER:
            name = cxpr_ast_identifier_name(ast);
            if (!cxpr_source_plan_is_direct_source_name(provider, name)) return 0;
            out->kind = CXPR_SOURCE_PLAN_FIELD;
            out->name = cxpr_source_plan_strdup(name);
            if (!out->name) return 0;
            return cxpr_source_plan_finalize_node_canonical(plan, out);
        case CXPR_NODE_LOOKBACK: {
            const cxpr_ast* target = cxpr_ast_lookback_target(ast);
            const cxpr_ast* index = cxpr_ast_lookback_index(ast);

            if (!target || !index) return 0;
            if (!cxpr_source_plan_node_parse(provider, target, plan, out)) return 0;
            if (!cxpr_source_plan_bound_arg_append(index, plan, &out->lookback_slot)) return 0;
            return cxpr_source_plan_finalize_node_canonical(plan, out);
        }
        case CXPR_NODE_FUNCTION_CALL: {
            int parsed = cxpr_source_plan_parse_function_source(
                cxpr_ast_function_name(ast),
                cxpr_ast_function_argc(ast),
                provider,
                ast,
                NULL,
                plan,
                out);
            if (parsed == 0) return 0;
            return cxpr_source_plan_finalize_node_canonical(plan, out);
        }
        case CXPR_NODE_PRODUCER_ACCESS: {
            int parsed = cxpr_source_plan_parse_function_source(
                cxpr_ast_producer_name(ast),
                cxpr_ast_producer_argc(ast),
                provider,
                ast,
                cxpr_ast_producer_field(ast),
                plan,
                out);
            if (parsed == 0) return 0;
            return cxpr_source_plan_finalize_node_canonical(plan, out);
        }
        default: {
            char* canonical_text = NULL;
            if (!cxpr_source_plan_ast_has_series_reference(provider, ast)) return 0;
            if (!cxpr_source_plan_render_ast_canonical(ast, &canonical_text)) return 0;
            out->kind = CXPR_SOURCE_PLAN_EXPRESSION;
            out->expression_ast = ast;
            out->name = canonical_text;
            return cxpr_source_plan_finalize_node_canonical(plan, out);
        }
    }
}

int cxpr_parse_provider_source_plan_ast(const cxpr_provider* provider,
                                        const cxpr_ast* ast,
                                        cxpr_source_plan_ast* out) {
    int ok;

    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    out->root.lookback_slot = SIZE_MAX;

    ok = cxpr_source_plan_node_parse(provider, ast, out, &out->root);
    if (!ok) {
        cxpr_free_source_plan_ast(out);
        return 0;
    }
    return 1;
}

int cxpr_eval_source_plan_bound_args(
    const cxpr_source_plan_ast* plan,
    const cxpr_context* ctx,
    const cxpr_registry* reg,
    double* out_values,
    size_t out_capacity,
    cxpr_error* err) {
    size_t index;

    if (!plan) return 0;
    if (!out_values && plan->arg_count > 0u) return 0;
    if (plan->arg_count > out_capacity) return 0;

    for (index = 0u; index < plan->arg_count; ++index) {
        if (!cxpr_eval_ast_number(plan->bound_arg_asts[index],
                                  ctx,
                                  reg,
                                  &out_values[index],
                                  err)) {
            return 0;
        }
    }
    return 1;
}
