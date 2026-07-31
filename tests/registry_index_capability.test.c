#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool resolve_index(const cxpr_expr_ast* target, int64_t index,
                          const cxpr_context* context,
                          const cxpr_registry* registry, void* userdata,
                          cxpr_value* out, cxpr_error* error) {
    (void)target;
    (void)context;
    (void)registry;
    (void)userdata;
    (void)error;
    *out = cxpr_num((double)index * 2.0);
    return true;
}

static cxpr_value evaluate(const char* source, cxpr_registry* registry,
                           const cxpr_context* context, bool compiled,
                           cxpr_error* error) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, source, error);
    cxpr_value result = cxpr_null();
    assert(parser && ast);
    if (compiled) {
        cxpr_expr_compiled* program = cxpr_expr_compile(ast, registry, error);
        assert(program);
        assert(cxpr_expr_compiled_eval(program, context, registry, &result, error));
        cxpr_expr_compiled_free(program);
    } else {
        assert(cxpr_eval_ast(ast, context, registry, &result, error));
    }
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
    return result;
}

static bool compound_owner(const cxpr_expr_ast* target,
                           const cxpr_expr_ast* index,
                           const cxpr_context* context,
                           const cxpr_registry* registry, void* userdata,
                           cxpr_value* out, cxpr_error* error) {
    size_t* calls = (size_t*)userdata;
    (void)index;
    (void)context;
    (void)registry;
    (void)error;
    if (cxpr_expr_ast_kind_of(target) == CXPR_NODE_IDENTIFIER) return false;
    ++*calls;
    *out = cxpr_num(77.0);
    return true;
}

static void count_free(void* userdata) {
    size_t* frees = (size_t*)userdata;
    ++*frees;
}

static cxpr_value next_index(const cxpr_value* args, size_t argc,
                             void* userdata) {
    size_t* calls = (size_t*)userdata;
    (void)args;
    assert(argc == 0u);
    ++*calls;
    return cxpr_num(5.0);
}

int main(void) {
    size_t frees = 0u;
    cxpr_registry* registry = cxpr_registry_new();
    assert(registry);
    assert(cxpr_registry_add_index_capability(
        registry, "path", "ray", CXPR_VALUE_STRUCT, resolve_index,
        &frees, count_free));
    assert(cxpr_registry_add_index_capability(
        registry, "history", "sensor", CXPR_VALUE_NUMBER, resolve_index,
        NULL, NULL));
    assert(cxpr_registry_add_index_capability(
        registry, "path", "ray_y", CXPR_VALUE_NUMBER, resolve_index,
        &frees, NULL));
    assert(cxpr_registry_index_capability_count(registry) == 3u);
    {
        const char* target = NULL;
        const char* name = NULL;
        cxpr_value_type type = CXPR_VALUE_NULL;
        assert(cxpr_registry_index_capability_at(
            registry, 0u, &target, &name, &type));
        assert(strcmp(target, "ray") == 0);
        assert(strcmp(name, "path") == 0);
        assert(type == CXPR_VALUE_STRUCT);
        assert(!cxpr_registry_index_capability_at(
            registry, 3u, &target, &name, &type));
    }
    {
        const char* capability_name = NULL;
        cxpr_value_type result_type = CXPR_VALUE_NULL;
        assert(cxpr_registry_index_capability_info(
            registry, "ray", &capability_name, &result_type));
        assert(strcmp(capability_name, "path") == 0);
        assert(result_type == CXPR_VALUE_STRUCT);
        assert(!cxpr_registry_index_capability_info(
            registry, "missing", &capability_name, &result_type));
        assert(capability_name == NULL);
    }
    {
        cxpr_error error = {0};
        size_t calls = 0u;
        cxpr_value result = evaluate("ray[3]", registry, NULL, false, &error);
        assert(error.code == CXPR_OK && result.type == CXPR_VALUE_NUMBER);
        assert(result.d == 6.0);
        result = evaluate("ray[4]", registry, NULL, true, &error);
        assert(error.code == CXPR_OK && result.type == CXPR_VALUE_NUMBER);
        assert(result.d == 8.0);
        cxpr_registry_add_typed(registry, "next_index", next_index, 0u, 0u,
                                NULL, CXPR_VALUE_NUMBER, &calls, NULL);
        result = evaluate("ray[next_index()]", registry, NULL, true, &error);
        assert(error.code == CXPR_OK && result.d == 10.0 && calls == 1u);
        result = evaluate("(ray + ray_y)[2]", registry, NULL, true, &error);
        assert(error.code == CXPR_OK && result.d == 4.0);
        result = evaluate("[1, 2][1] + ray[3]", registry, NULL, false, &error);
        assert(error.code == CXPR_OK && result.d == 8.0);
        result = evaluate("[1, 2][1] + ray[3]", registry, NULL, true, &error);
        assert(error.code == CXPR_OK && result.d == 8.0);

        error = (cxpr_error){0};
        {
            cxpr_expr_parser* parser = cxpr_expr_parser_new();
            cxpr_expr_ast* ast =
                cxpr_expr_ast_parse(parser, "(ray + sensor)[1]", &error);
            assert(ast);
            assert(!cxpr_eval_ast(ast, NULL, registry, &result, &error));
            assert(error.code == CXPR_ERR_TYPE_MISMATCH);
            assert(strcmp(error.message,
                          "Index target references multiple capabilities") == 0);
            cxpr_expr_ast_free(ast);
            cxpr_expr_parser_free(parser);
        }

        error = (cxpr_error){0};
        {
            cxpr_expr_parser* parser = cxpr_expr_parser_new();
            cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, "ray[-1]", &error);
            assert(ast);
            assert(!cxpr_eval_ast(ast, NULL, registry, &result, &error));
            assert(error.code == CXPR_ERR_INVALID_INDEX);
            assert(error.message != NULL);
            cxpr_expr_ast_free(ast);
            cxpr_expr_parser_free(parser);
        }
    }

    {
        cxpr_error error = {0};
        cxpr_expr_parser* parser = cxpr_expr_parser_new();
        cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, "(ray + ray_y)", &error);
        const char* name = NULL;
        cxpr_value_type type = CXPR_VALUE_NULL;
        assert(ast);
        assert(cxpr_registry_index_target_info(
            registry, ast, &name, &type, &error));
        assert(strcmp(name, "path") == 0 && type == CXPR_VALUE_STRUCT);
        cxpr_expr_ast_free(ast);
        cxpr_expr_parser_free(parser);
    }
    {
        cxpr_error error = {0};
        cxpr_context* context = cxpr_context_new();
        cxpr_value elements[] = {cxpr_num(11.0), cxpr_num(22.0)};
        cxpr_value array = cxpr_array(cxpr_array_value_new(elements, 2u));
        cxpr_value result;
        assert(context && array.a);
        cxpr_context_set_value(context, "ray", &array);
        cxpr_value_free(&array);
        result = evaluate("ray[1]", registry, context, false, &error);
        assert(error.code == CXPR_OK && result.d == 22.0);
        cxpr_value_free(&result);
        result = evaluate("ray[1]", registry, context, true, &error);
        assert(error.code == CXPR_OK && result.d == 22.0);
        cxpr_value_free(&result);
        cxpr_context_free(context);
    }
    {
        cxpr_error error = {0};
        size_t owner_calls = 0u;
        cxpr_value result;
        cxpr_registry_set_lookback_resolver(
            registry, compound_owner, &owner_calls, NULL);
        result = evaluate("(ray + ray_y)[2]", registry, NULL, false, &error);
        assert(error.code == CXPR_OK && result.d == 77.0 && owner_calls == 1u);
        result = evaluate("(ray + ray_y)[2]", registry, NULL, true, &error);
        assert(error.code == CXPR_OK && result.d == 77.0 && owner_calls == 2u);
        cxpr_registry_set_lookback_resolver(registry, NULL, NULL, NULL);
    }

    /* Exact target ownership rejects ambiguity regardless of registration order. */
    assert(!cxpr_registry_add_index_capability(
        registry, "other", "ray", CXPR_VALUE_NUMBER, resolve_index,
        NULL, NULL));
    assert(cxpr_registry_index_capability_count(registry) == 3u);
    cxpr_registry_free(registry);
    assert(frees == 1u);
    return 0;
}
