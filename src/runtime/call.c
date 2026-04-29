/**
 * @file runtime_call.c
 * @brief Bridge runtime-call helpers.
 */

#include <cxpr/runtime_call.h>

#include <string.h>

static const cxpr_ast* cxpr_runtime_call_arg_raw(const cxpr_ast* ast,
                                                             size_t index) {
    if (!ast) return NULL;

    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_FUNCTION_CALL:
            return cxpr_ast_function_arg(ast, index);
        case CXPR_NODE_PRODUCER_ACCESS:
            return cxpr_ast_producer_arg(ast, index);
        default:
            return NULL;
    }
}

static const char* cxpr_runtime_call_arg_name(const cxpr_ast* ast,
                                                          size_t index) {
    if (!ast) return NULL;

    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_FUNCTION_CALL:
            return cxpr_ast_function_arg_name(ast, index);
        case CXPR_NODE_PRODUCER_ACCESS:
            return cxpr_ast_producer_arg_name(ast, index);
        default:
            return NULL;
    }
}

static size_t cxpr_runtime_call_argc(const cxpr_ast* ast) {
    if (!ast) return 0u;

    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_FUNCTION_CALL:
            return cxpr_ast_function_argc(ast);
        case CXPR_NODE_PRODUCER_ACCESS:
            return cxpr_ast_producer_argc(ast);
        default:
            return 0u;
    }
}

static const char* cxpr_runtime_call_name(const cxpr_ast* ast) {
    if (!ast) return NULL;

    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_FUNCTION_CALL:
            return cxpr_ast_function_name(ast);
        case CXPR_NODE_PRODUCER_ACCESS:
            return cxpr_ast_producer_name(ast);
        default:
            return NULL;
    }
}

static const cxpr_ast* cxpr_runtime_call_find_named_arg_raw(
    const cxpr_ast* ast,
    const char* name) {
    size_t argc;
    size_t i;

    if (!ast || !name || name[0] == '\0') return NULL;
    argc = cxpr_runtime_call_argc(ast);
    for (i = 0u; i < argc; ++i) {
        const char* arg_name = cxpr_runtime_call_arg_name(ast, i);
        if (!arg_name || strcmp(arg_name, name) != 0) continue;
        return cxpr_runtime_call_arg_raw(ast, i);
    }
    return NULL;
}

static int cxpr_runtime_call_spec_index_for_name(
    const cxpr_expr_param_spec* spec,
    const char* name,
    size_t* out_index) {
    size_t i;

    if (out_index) *out_index = 0u;
    if (!spec || !spec->names || !name) return 0;
    for (i = 0u; i < spec->count; ++i) {
        if (!spec->names[i] || strcmp(spec->names[i], name) != 0) continue;
        if (out_index) *out_index = i;
        return 1;
    }
    return 0;
}

static int cxpr_runtime_call_ast_is_source_like(const cxpr_ast* ast) {
    if (!ast) return 0;
    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_IDENTIFIER:
        case CXPR_NODE_FUNCTION_CALL:
        case CXPR_NODE_PRODUCER_ACCESS:
        case CXPR_NODE_FIELD_ACCESS:
        case CXPR_NODE_CHAIN_ACCESS:
        case CXPR_NODE_LOOKBACK:
            return 1;
        default:
            return 0;
    }
}

static size_t cxpr_runtime_call_positional_count(const cxpr_ast* ast) {
    size_t argc;
    size_t i;

    if (!ast) return 0u;
    argc = cxpr_runtime_call_argc(ast);
    for (i = 0u; i < argc; ++i) {
        if (cxpr_runtime_call_arg_name(ast, i)) break;
    }
    return i;
}

static size_t cxpr_runtime_call_omitted_leading_sources(
    const cxpr_ast* ast,
    const cxpr_expr_param_spec* spec,
    size_t positional_count) {
    size_t spec_index;
    size_t raw_index = 0u;
    size_t omitted = 0u;

    if (!ast || !spec || !spec->kinds) return 0u;
    for (spec_index = 0u; spec_index < spec->count; ++spec_index) {
        const cxpr_ast* raw;
        if (spec->kinds[spec_index] != CXPR_EXPR_ARG_SCALAR_SOURCE) break;
        raw = raw_index < positional_count
                  ? cxpr_runtime_call_arg_raw(ast, raw_index)
                  : NULL;
        if (!cxpr_runtime_call_ast_is_source_like(raw)) {
            ++omitted;
            continue;
        }
        ++raw_index;
    }
    return omitted;
}

static const cxpr_provider_scope_spec* cxpr_runtime_call_scope_spec(
    const cxpr_provider* provider,
    const cxpr_ast* ast) {
    const char* name;
    const cxpr_provider_fn_spec* fn_spec;
    const cxpr_provider_source_spec* source_spec;

    if (!provider || !ast) return NULL;
    name = cxpr_runtime_call_name(ast);
    if (!name) return NULL;

    fn_spec = cxpr_provider_fn_spec_find(provider, name);
    if (fn_spec && fn_spec->scope && fn_spec->scope->param_name &&
        fn_spec->scope->param_name[0] != '\0') {
        return fn_spec->scope;
    }

    source_spec = cxpr_provider_source_spec_find(provider, name);
    if (source_spec && source_spec->scope && source_spec->scope->param_name &&
        source_spec->scope->param_name[0] != '\0') {
        return source_spec->scope;
    }

    return NULL;
}

static size_t cxpr_runtime_call_numeric_value_arg_count(
    const cxpr_provider* provider,
    const cxpr_ast* ast,
    size_t argc,
    size_t raw_value_arg_count) {
    cxpr_expr_param_spec spec;
    size_t count = 0u;
    size_t positional_count;
    size_t omitted_sources;
    size_t i;

    if (!provider || !ast) return raw_value_arg_count;
    if (!cxpr_provider_expr_param_spec_for(
            provider,
            cxpr_runtime_call_name(ast),
            &spec)) {
        return raw_value_arg_count;
    }

    positional_count = cxpr_runtime_call_positional_count(ast);
    omitted_sources = cxpr_runtime_call_omitted_leading_sources(
        ast,
        &spec,
        positional_count);

    for (i = 0u; i < argc; ++i) {
        const char* arg_name = cxpr_runtime_call_arg_name(ast, i);
        size_t spec_index = i + omitted_sources;
        if (i >= raw_value_arg_count) break;
        if (arg_name &&
            cxpr_runtime_call_spec_index_for_name(&spec, arg_name, &spec_index) == 0) {
            continue;
        }
        if (spec.kinds && spec_index < spec.count &&
            spec.kinds[spec_index] == CXPR_EXPR_ARG_SCALAR_SOURCE) {
            continue;
        }
        ++count;
    }
    return count <= raw_value_arg_count ? count : raw_value_arg_count;
}

const cxpr_ast* cxpr_provider_runtime_call_arg(const cxpr_provider* provider,
                                               const cxpr_ast* ast,
                                               size_t index) {
    cxpr_expr_param_spec spec;
    size_t argc;
    size_t positional_count = 0u;
    size_t omitted_sources = 0u;
    size_t raw_index;
    const char* name;
    size_t i;

    if (!ast) return NULL;
    argc = cxpr_runtime_call_argc(ast);
    name = cxpr_runtime_call_name(ast);
    if (!name) return NULL;
    if (!cxpr_provider_expr_param_spec_for(provider, name, &spec)) {
        if (index >= argc) return NULL;
        return cxpr_runtime_call_arg_raw(ast, index);
    }
    if (index >= spec.count) {
        if (index >= argc) return NULL;
        return cxpr_runtime_call_arg_raw(ast, index);
    }

    for (i = 0u; i < argc; ++i) {
        const char* arg_name = cxpr_runtime_call_arg_name(ast, i);
        if (arg_name) break;
        ++positional_count;
    }
    omitted_sources = cxpr_runtime_call_omitted_leading_sources(
        ast,
        &spec,
        positional_count);

    if (index >= omitted_sources) {
        raw_index = index - omitted_sources;
        if (raw_index < positional_count) {
            return cxpr_runtime_call_arg_raw(ast, raw_index);
        }
    }

    for (i = positional_count; i < argc; ++i) {
        const char* arg_name = cxpr_runtime_call_arg_name(ast, i);
        if (arg_name && strcmp(arg_name, spec.names[index]) == 0) {
            return cxpr_runtime_call_arg_raw(ast, i);
        }
    }

    return NULL;
}

int cxpr_parse_runtime_call(const cxpr_ast* ast,
                            cxpr_runtime_call* out) {
    return cxpr_parse_runtime_call_provider(NULL, ast, out);
}

int cxpr_parse_runtime_call_provider(
    const cxpr_provider* provider,
    const cxpr_ast* ast,
    cxpr_runtime_call* out) {
    size_t argc = 0u;
    const char* scope_value = NULL;
    const cxpr_provider_scope_spec* scope_spec = NULL;

    if (!ast || !out) return 0;
    memset(out, 0, sizeof(*out));

    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_FUNCTION_CALL:
            out->kind = CXPR_RUNTIME_CALL_FUNCTION;
            out->name = cxpr_ast_function_name(ast);
            argc = cxpr_ast_function_argc(ast);
            break;
        case CXPR_NODE_PRODUCER_ACCESS:
            out->kind = CXPR_RUNTIME_CALL_PRODUCER;
            out->name = cxpr_ast_producer_name(ast);
            out->field_name = cxpr_ast_producer_field(ast);
            argc = cxpr_ast_producer_argc(ast);
            break;
        default:
            return 0;
    }

    out->arg_count = argc;
    out->value_arg_count = argc;
    if (argc == 0u) return out->name != NULL;

    scope_spec = cxpr_runtime_call_scope_spec(provider, ast);
    if (scope_spec && scope_spec->param_name && scope_spec->param_name[0] != '\0') {
        const cxpr_ast* scope_arg = cxpr_runtime_call_find_named_arg_raw(
            ast,
            scope_spec->param_name);
        scope_value = cxpr_ast_string_value(scope_arg);
        if (scope_value && scope_value[0] != '\0') {
            out->scope_value = scope_value;
            out->timeframe = scope_value;
            if (out->value_arg_count > 0u) out->value_arg_count -= 1u;
            out->value_arg_count = cxpr_runtime_call_numeric_value_arg_count(
                provider,
                ast,
                argc,
                out->value_arg_count);
            return out->name != NULL;
        }
    }

    {
        const cxpr_ast* last = cxpr_runtime_call_arg_raw(ast, argc - 1u);
        scope_value = cxpr_ast_string_value(last);
        if (scope_value && scope_value[0] != '\0') {
            out->timeframe = scope_value;
            out->scope_value = scope_value;
            out->value_arg_count = argc - 1u;
        }
    }
    out->value_arg_count = cxpr_runtime_call_numeric_value_arg_count(
        provider,
        ast,
        argc,
        out->value_arg_count);
    return out->name != NULL;
}

int cxpr_resolve_expression_scope(
    const cxpr_provider* provider,
    const cxpr_ast* root,
    cxpr_resolved_scope* out) {
    cxpr_runtime_call call;
    const cxpr_provider_scope_spec* scope_spec;
    cxpr_resolved_scope nested;
    size_t i;

    if (out) memset(out, 0, sizeof(*out));
    if (!provider || !root || !out) return 0;

    scope_spec = cxpr_runtime_call_scope_spec(provider, root);
    if (scope_spec &&
        cxpr_parse_runtime_call_provider(provider, root, &call) &&
        call.scope_value && call.scope_value[0] != '\0') {
        out->scope_name = scope_spec->param_name;
        out->scope_value = call.scope_value;
        out->origin = root;
        return 1;
    }

    switch (cxpr_ast_type(root)) {
        case CXPR_NODE_FUNCTION_CALL:
            for (i = 0u; i < cxpr_ast_function_argc(root); ++i) {
                if (cxpr_resolve_expression_scope(
                        provider,
                        cxpr_ast_function_arg(root, i),
                        &nested)) {
                    *out = nested;
                    return 1;
                }
            }
            break;
        case CXPR_NODE_PRODUCER_ACCESS:
            for (i = 0u; i < cxpr_ast_producer_argc(root); ++i) {
                if (cxpr_resolve_expression_scope(
                        provider,
                        cxpr_ast_producer_arg(root, i),
                        &nested)) {
                    *out = nested;
                    return 1;
                }
            }
            break;
        case CXPR_NODE_BINARY_OP:
            if (cxpr_resolve_expression_scope(provider, cxpr_ast_left(root), &nested)) {
                *out = nested;
                return 1;
            }
            if (cxpr_resolve_expression_scope(provider, cxpr_ast_right(root), &nested)) {
                *out = nested;
                return 1;
            }
            break;
        case CXPR_NODE_UNARY_OP:
            if (cxpr_resolve_expression_scope(provider, cxpr_ast_operand(root), &nested)) {
                *out = nested;
                return 1;
            }
            break;
        case CXPR_NODE_LOOKBACK:
            if (cxpr_resolve_expression_scope(
                    provider,
                    cxpr_ast_lookback_target(root),
                    &nested)) {
                *out = nested;
                return 1;
            }
            if (cxpr_resolve_expression_scope(
                    provider,
                    cxpr_ast_lookback_index(root),
                    &nested)) {
                *out = nested;
                return 1;
            }
            break;
        case CXPR_NODE_TERNARY:
            if (cxpr_resolve_expression_scope(
                    provider,
                    cxpr_ast_ternary_condition(root),
                    &nested)) {
                *out = nested;
                return 1;
            }
            if (cxpr_resolve_expression_scope(
                    provider,
                    cxpr_ast_ternary_true_branch(root),
                    &nested)) {
                *out = nested;
                return 1;
            }
            if (cxpr_resolve_expression_scope(
                    provider,
                    cxpr_ast_ternary_false_branch(root),
                    &nested)) {
                *out = nested;
                return 1;
            }
            break;
        default:
            break;
    }

    return 0;
}

int cxpr_provider_eval_runtime_call_number_args(const cxpr_provider* provider,
                                                const cxpr_ast* ast,
                                                size_t count,
                                                const cxpr_context* ctx,
                                                const cxpr_registry* reg,
                                                double* out_values,
                                                size_t out_capacity,
                                                cxpr_error* err) {
    cxpr_expr_param_spec spec;
    size_t index;
    size_t out_index = 0u;

    if (!out_values && count > 0u) return 0;
    if (count > out_capacity) return 0;

    if (provider &&
        ast &&
        cxpr_provider_expr_param_spec_for(
            provider,
            cxpr_runtime_call_name(ast),
            &spec)) {
        for (index = 0u; index < spec.count && out_index < count; ++index) {
            const cxpr_ast* arg;
            if (spec.kinds && spec.kinds[index] == CXPR_EXPR_ARG_SCALAR_SOURCE) {
                continue;
            }
            arg = cxpr_provider_runtime_call_arg(provider, ast, index);
            if (!arg || out_index >= count) return 0;
            if (!cxpr_eval_ast_number(arg, ctx, reg, &out_values[out_index], err)) {
                return 0;
            }
            ++out_index;
        }
        return out_index == count;
    }

    for (index = 0u; index < count; ++index) {
        const cxpr_ast* arg = cxpr_provider_runtime_call_arg(provider, ast, index);
        if (!arg) return 0;
        if (!cxpr_eval_ast_number(arg, ctx, reg, &out_values[index], err)) {
            return 0;
        }
    }
    return 1;
}
