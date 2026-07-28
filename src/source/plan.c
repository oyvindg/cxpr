/**
 * @file source_plan.c
 * @brief Bridge source-plan parsing implementation.
 */

#include "internal.h"

#include <cxpr/runtime.h>
#include <cxpr/scope.h>

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

static size_t cxpr_source_plan_positional_count(const cxpr_expr_ast* ast) {
    size_t argc;
    size_t i;

    argc = cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS
               ? cxpr_expr_ast_producer_arg_count(ast)
               : cxpr_expr_ast_call_arg_count(ast);
    for (i = 0u; i < argc; ++i) {
        const char* arg_name =
            cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS
                ? cxpr_expr_ast_producer_arg_name(ast, i)
                : cxpr_expr_ast_call_arg_name(ast, i);
        if (arg_name) return i;
    }
    return argc;
}

static const cxpr_expr_ast* cxpr_source_plan_arg_raw(
    const cxpr_expr_ast* ast,
    size_t index) {
    if (!ast) return NULL;
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS) {
        return cxpr_expr_ast_producer_arg(ast, index);
    }
    return cxpr_expr_ast_call_arg(ast, index);
}

static const char* cxpr_source_plan_arg_name(
    const cxpr_expr_ast* ast,
    size_t index) {
    if (!ast) return NULL;
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS) {
        return cxpr_expr_ast_producer_arg_name(ast, index);
    }
    return cxpr_expr_ast_call_arg_name(ast, index);
}

static const cxpr_expr_ast* cxpr_source_plan_find_named_arg(
    const cxpr_expr_ast* ast,
    const char* name) {
    size_t argc;
    size_t i;

    if (!ast || !name || name[0] == '\0') return NULL;
    argc = cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS
               ? cxpr_expr_ast_producer_arg_count(ast)
               : cxpr_expr_ast_call_arg_count(ast);
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

static const cxpr_expr_ast* cxpr_source_plan_numeric_arg(
    const cxpr_provider* provider,
    const char* name,
    const cxpr_expr_ast* ast,
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

void cxpr_free_source_plan_bindings(cxpr_source_plan_bindings* bindings) {
    if (!bindings) return;
    free(bindings->handles);
    memset(bindings, 0, sizeof(*bindings));
}

static int cxpr_source_plan_bound_arg_append(const cxpr_expr_ast* ast,
                                             cxpr_source_plan_ast* plan,
                                             size_t* out_slot) {
    const cxpr_expr_ast** grown;
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
                                       const cxpr_expr_ast* ast,
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
                                                const cxpr_expr_ast* ast) {
    if (!ast) return 0;
    switch (cxpr_expr_ast_kind_of(ast)) {
        case CXPR_NODE_IDENTIFIER:
            return cxpr_source_plan_is_direct_source_name(provider, cxpr_expr_ast_identifier_name(ast));
        case CXPR_NODE_FUNCTION_CALL:
        case CXPR_NODE_PRODUCER_ACCESS:
            return 1;
        case CXPR_NODE_VARIABLE:
            return 0;
        case CXPR_NODE_BINARY_OP:
            return cxpr_source_plan_ast_has_series_reference(provider, cxpr_expr_ast_binary_left(ast)) ||
                   cxpr_source_plan_ast_has_series_reference(provider, cxpr_expr_ast_binary_right(ast));
        case CXPR_NODE_UNARY_OP:
            return cxpr_source_plan_ast_has_series_reference(provider, cxpr_expr_ast_unary_operand(ast));
        case CXPR_NODE_LOOKBACK:
            return cxpr_source_plan_ast_has_series_reference(provider, cxpr_expr_ast_lookback_target(ast));
        case CXPR_NODE_TERNARY:
            return cxpr_source_plan_ast_has_series_reference(provider, cxpr_expr_ast_ternary_condition(ast)) ||
                   cxpr_source_plan_ast_has_series_reference(provider, cxpr_expr_ast_ternary_true(ast)) ||
                   cxpr_source_plan_ast_has_series_reference(provider, cxpr_expr_ast_ternary_false(ast));
        default:
            return 0;
    }
}

static int cxpr_source_plan_parse_function_source(const char* name,
                                      size_t argc,
                                      const cxpr_provider* provider,
                                      const cxpr_expr_ast* ast,
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
    const cxpr_expr_ast* source_ast = NULL;
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
                const cxpr_expr_ast* arg_ast = NULL;
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
                                       const cxpr_expr_ast* ast,
                                       cxpr_source_plan_ast* plan,
                                       cxpr_source_plan_node* out) {
    const char* name;

    if (!ast || !plan || !out) return 0;

    switch (cxpr_expr_ast_kind_of(ast)) {
        case CXPR_NODE_IDENTIFIER:
            name = cxpr_expr_ast_identifier_name(ast);
            if (!cxpr_source_plan_is_direct_source_name(provider, name)) return 0;
            out->kind = CXPR_SOURCE_PLAN_FIELD;
            out->name = cxpr_source_plan_strdup(name);
            if (!out->name) return 0;
            return cxpr_source_plan_finalize_node_canonical(plan, out);
        case CXPR_NODE_LOOKBACK: {
            const cxpr_expr_ast* target = cxpr_expr_ast_lookback_target(ast);
            const cxpr_expr_ast* index = cxpr_expr_ast_lookback_index(ast);

            if (!target || !index) return 0;
            if (!cxpr_source_plan_node_parse(provider, target, plan, out)) return 0;
            if (!cxpr_source_plan_bound_arg_append(index, plan, &out->lookback_slot)) return 0;
            return cxpr_source_plan_finalize_node_canonical(plan, out);
        }
        case CXPR_NODE_FUNCTION_CALL: {
            int parsed = cxpr_source_plan_parse_function_source(
                cxpr_expr_ast_call_name(ast),
                cxpr_expr_ast_call_arg_count(ast),
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
                cxpr_expr_ast_producer_name(ast),
                cxpr_expr_ast_producer_arg_count(ast),
                provider,
                ast,
                cxpr_expr_ast_producer_field(ast),
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
                                        const cxpr_expr_ast* ast,
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

static int cxpr_source_plan_bindings_append(
    cxpr_source_plan_bindings* out,
    uint64_t handle) {
    uint64_t* grown;
    size_t next_count;

    if (!out) return 0;
    next_count = out->count + 1u;
    grown = (uint64_t*)realloc(out->handles, next_count * sizeof(*grown));
    if (!grown) return 0;
    out->handles = grown;
    out->handles[out->count] = handle;
    out->count = next_count;
    return 1;
}

static int cxpr_source_plan_collect_node_args(
    const cxpr_source_plan_node* node,
    const double* plan_args,
    size_t plan_arg_count,
    double** out_args,
    size_t* out_count) {
    double* args = NULL;
    size_t i;

    if (out_args) *out_args = NULL;
    if (out_count) *out_count = 0u;
    if (!node || !out_args || !out_count) return 0;
    if (node->arg_count == 0u) return 1;
    if (!node->arg_slots || !plan_args) return 0;

    args = (double*)calloc(node->arg_count, sizeof(*args));
    if (!args) return 0;
    for (i = 0u; i < node->arg_count; ++i) {
        size_t slot = node->arg_slots[i];
        if (slot >= plan_arg_count) {
            free(args);
            return 0;
        }
        args[i] = plan_args[slot];
    }
    *out_args = args;
    *out_count = node->arg_count;
    return 1;
}

static int cxpr_source_plan_bind_leaf_nodes(
    const cxpr_source_plan_node* node,
    const double* plan_args,
    size_t plan_arg_count,
    cxpr_source_plan_bind_fn bind,
    void* userdata,
    cxpr_source_plan_bindings* out) {
    double* node_args = NULL;
    size_t node_arg_count = 0u;
    uint64_t handle = 0u;
    int ok;

    if (!node || !bind || !out) return 0;
    /* Source-input wrappers such as ema(close, 14) materialize from their
       child source; bind the leaf that actually selects the host series. */
    if (node->source) {
        return cxpr_source_plan_bind_leaf_nodes(
            node->source,
            plan_args,
            plan_arg_count,
            bind,
            userdata,
            out);
    }
    if (node->kind == CXPR_SOURCE_PLAN_INVALID) return 0;

    if (!cxpr_source_plan_collect_node_args(
            node,
            plan_args,
            plan_arg_count,
            &node_args,
            &node_arg_count)) {
        return 0;
    }
    ok = bind(node, node_args, node_arg_count, &handle, userdata);
    free(node_args);
    if (!ok) return 0;
    return cxpr_source_plan_bindings_append(out, handle);
}

static int cxpr_source_plan_bind_parsed_plan(
    const cxpr_source_plan_ast* plan,
    const cxpr_context* ctx,
    const cxpr_registry* reg,
    cxpr_source_plan_bind_fn bind,
    void* userdata,
    cxpr_source_plan_bindings* out,
    cxpr_error* err) {
    double* plan_args = NULL;
    int ok;

    if (!plan || !bind || !out) return 0;
    if (plan->arg_count > 0u) {
        plan_args = (double*)calloc(plan->arg_count, sizeof(*plan_args));
        if (!plan_args) return 0;
    }
    ok = cxpr_eval_source_plan_bound_args(
        plan,
        ctx,
        reg,
        plan_args,
        plan->arg_count,
        err);
    if (ok) {
        ok = cxpr_source_plan_bind_leaf_nodes(
            &plan->root,
            plan_args,
            plan->arg_count,
            bind,
            userdata,
            out);
    }
    free(plan_args);
    return ok;
}

static int cxpr_source_plan_should_parse_at_node(const cxpr_expr_ast* ast) {
    if (!ast) return 0;
    switch (cxpr_expr_ast_kind_of(ast)) {
    case CXPR_NODE_IDENTIFIER:
    case CXPR_NODE_FUNCTION_CALL:
    case CXPR_NODE_PRODUCER_ACCESS:
    case CXPR_NODE_LOOKBACK:
        return 1;
    default:
        return 0;
    }
}

static int cxpr_plan_bind_sources_walk(
    const cxpr_provider* provider,
    const cxpr_expr_ast* ast,
    const cxpr_context* ctx,
    const cxpr_registry* reg,
    cxpr_source_plan_bind_fn bind,
    void* userdata,
    cxpr_source_plan_bindings* out,
    cxpr_error* err) {
    cxpr_source_plan_ast plan = {0};
    size_t i;

    if (!ast) return 1;
    /* Try provider source-plan parsing at source-shaped roots. If it succeeds,
       cxpr owns the subtree and the host only sees parsed leaf nodes. */
    if (cxpr_source_plan_should_parse_at_node(ast) &&
        cxpr_parse_provider_source_plan_ast(provider, ast, &plan)) {
        int ok = cxpr_source_plan_bind_parsed_plan(
            &plan,
            ctx,
            reg,
            bind,
            userdata,
            out,
            err);
        cxpr_free_source_plan_ast(&plan);
        return ok;
    }

    switch (cxpr_expr_ast_kind_of(ast)) {
    case CXPR_NODE_BINARY_OP:
        return cxpr_plan_bind_sources_walk(provider, cxpr_expr_ast_binary_left(ast), ctx, reg, bind, userdata, out, err) &&
               cxpr_plan_bind_sources_walk(provider, cxpr_expr_ast_binary_right(ast), ctx, reg, bind, userdata, out, err);
    case CXPR_NODE_UNARY_OP:
        return cxpr_plan_bind_sources_walk(provider, cxpr_expr_ast_unary_operand(ast), ctx, reg, bind, userdata, out, err);
    case CXPR_NODE_FUNCTION_CALL:
        for (i = 0u; i < cxpr_expr_ast_call_arg_count(ast); ++i) {
            if (!cxpr_plan_bind_sources_walk(
                    provider,
                    cxpr_expr_ast_call_arg(ast, i),
                    ctx,
                    reg,
                    bind,
                    userdata,
                    out,
                    err)) {
                return 0;
            }
        }
        return 1;
    case CXPR_NODE_PRODUCER_ACCESS:
        for (i = 0u; i < cxpr_expr_ast_producer_arg_count(ast); ++i) {
            if (!cxpr_plan_bind_sources_walk(
                    provider,
                    cxpr_expr_ast_producer_arg(ast, i),
                    ctx,
                    reg,
                    bind,
                    userdata,
                    out,
                    err)) {
                return 0;
            }
        }
        return 1;
    case CXPR_NODE_LOOKBACK:
        return cxpr_plan_bind_sources_walk(provider, cxpr_expr_ast_lookback_target(ast), ctx, reg, bind, userdata, out, err) &&
               cxpr_plan_bind_sources_walk(provider, cxpr_expr_ast_lookback_index(ast), ctx, reg, bind, userdata, out, err);
    case CXPR_NODE_TERNARY:
        return cxpr_plan_bind_sources_walk(provider, cxpr_expr_ast_ternary_condition(ast), ctx, reg, bind, userdata, out, err) &&
               cxpr_plan_bind_sources_walk(provider, cxpr_expr_ast_ternary_true(ast), ctx, reg, bind, userdata, out, err) &&
               cxpr_plan_bind_sources_walk(provider, cxpr_expr_ast_ternary_false(ast), ctx, reg, bind, userdata, out, err);
    default:
        return 1;
    }
}

int cxpr_plan_bind_sources(
    const cxpr_provider* provider,
    const cxpr_expr_ast* expr,
    const cxpr_context* ctx,
    cxpr_registry* reg,
    const cxpr_plan_config* config,
    cxpr_source_plan_bindings* out,
    cxpr_error* err) {
    cxpr_source_plan_bindings tmp = {0};
    const cxpr_provider_source_spec* const* source_specs = NULL;
    cxpr_scoped_source_spec* scoped_specs = NULL;
    cxpr_scope_resolver resolver;
    size_t source_count = 0u;
    size_t scoped_count = 0u;
    size_t i;

    if (out) memset(out, 0, sizeof(*out));
    if (!provider || !expr || !config || !config->bind || !out) return 0;

    if (reg && config->resolve) {
        source_specs = cxpr_provider_source_specs(provider, &source_count);
        if (source_specs && source_count > 0u) {
            scoped_specs = (cxpr_scoped_source_spec*)calloc(source_count, sizeof(*scoped_specs));
            if (!scoped_specs) return 0;
            for (i = 0u; i < source_count; ++i) {
                const cxpr_provider_source_spec* source = source_specs[i];
                if (!source || !source->name || source->name[0] == '\0' || !source->scope) continue;
                scoped_specs[scoped_count].name = source->name;
                scoped_specs[scoped_count].min_args = source->min_args;
                scoped_specs[scoped_count].max_args = source->max_args;
                scoped_specs[scoped_count].scope = source->scope;
                scoped_count += 1u;
            }
            if (scoped_count > 0u) {
                resolver.resolve = config->resolve;
                resolver.userdata = config->userdata;
                cxpr_scoped_source_functions_register(
                    reg,
                    scoped_specs,
                    scoped_count,
                    &resolver,
                    NULL);
            }
            free(scoped_specs);
        }
    }

    if (!cxpr_plan_bind_sources_walk(
            provider,
            expr,
            ctx,
            reg,
            config->bind,
            config->userdata,
            &tmp,
            err)) {
        cxpr_free_source_plan_bindings(&tmp);
        return 0;
    }
    *out = tmp;
    return 1;
}

typedef struct {
    const cxpr_source_handle_entry* table;
    size_t table_count;
} cxpr_source_plan_table_bind_ctx;

static int cxpr_source_plan_scope_matches(const char* left, const char* right) {
    const char* a = left ? left : "";
    const char* b = right ? right : "";
    return strcmp(a, b) == 0;
}

static int cxpr_source_plan_table_bind(
    const cxpr_source_plan_node* node,
    const double* bound_args,
    size_t arg_count,
    uint64_t* out_handle,
    void* userdata) {
    cxpr_source_plan_table_bind_ctx* ctx = (cxpr_source_plan_table_bind_ctx*)userdata;
    size_t i;

    (void)bound_args;
    (void)arg_count;
    if (!ctx || !node || !node->name || !out_handle) return 0;
    for (i = 0u; i < ctx->table_count; ++i) {
        const cxpr_source_handle_entry* entry = &ctx->table[i];
        if (!entry->name || strcmp(entry->name, node->name) != 0) continue;
        if (!cxpr_source_plan_scope_matches(entry->scope_value, node->scope_value)) continue;
        *out_handle = entry->handle;
        return 1;
    }
    return 0;
}

int cxpr_plan_bind_sources_from_table(
    const cxpr_provider* provider,
    const cxpr_expr_ast* expr,
    const cxpr_context* ctx,
    cxpr_registry* reg,
    const cxpr_source_handle_entry* table,
    size_t table_count,
    cxpr_source_plan_bindings* out,
    cxpr_error* err) {
    cxpr_source_plan_table_bind_ctx bind_ctx;
    cxpr_plan_config config;

    if (!table && table_count > 0u) {
        if (out) memset(out, 0, sizeof(*out));
        return 0;
    }
    bind_ctx.table = table;
    bind_ctx.table_count = table_count;
    memset(&config, 0, sizeof(config));
    config.bind = cxpr_source_plan_table_bind;
    config.userdata = &bind_ctx;
    return cxpr_plan_bind_sources(
        provider,
        expr,
        ctx,
        reg,
        &config,
        out,
        err);
}
