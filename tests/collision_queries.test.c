#include <cxpr/model/model.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CXPR_TEST_SOURCE_DIR
#define CXPR_TEST_SOURCE_DIR "."
#endif

static char* read_fixture(const char* relative_path) {
    char path[1024];
    FILE* file;
    long size;
    char* source;
    (void)snprintf(path, sizeof(path), "%s/%s", CXPR_TEST_SOURCE_DIR, relative_path);
    file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) return NULL;
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    source = (char*)malloc((size_t)size + 1u);
    if (!source || fread(source, 1u, (size_t)size, file) != (size_t)size) {
        free(source);
        fclose(file);
        return NULL;
    }
    source[size] = '\0';
    fclose(file);
    return source;
}

static cxpr_value point(double x, double y) {
    static const char* const names[] = {"x", "y"};
    cxpr_value values[] = {cxpr_num(x), cxpr_num(y)};
    return (cxpr_value){
        .type = CXPR_VALUE_STRUCT,
        .s = cxpr_struct_value_new(names, values, CXPR_ARRAY_COUNT(values))
    };
}

static cxpr_value hit(double x, double y, double nx, double ny,
                      const char* entity) {
    static const char* const names[] = {"point", "normal", "entity"};
    cxpr_value values[] = {point(x, y), point(nx, ny), cxpr_string(entity)};
    cxpr_value result = {
        .type = CXPR_VALUE_STRUCT,
        .s = cxpr_struct_value_new(names, values, CXPR_ARRAY_COUNT(values))
    };
    cxpr_value_free(&values[0]);
    cxpr_value_free(&values[1]);
    return result;
}

static void assert_near(double actual, double expected) {
    assert(fabs(actual - expected) < 1e-9);
}

static cxpr_value evaluate(const char* source, const cxpr_context* context) {
    cxpr_error error = {0};
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, source, &error);
    cxpr_value result = cxpr_null();
    assert(parser != NULL && ast != NULL);
    assert(cxpr_eval_ast(ast, context, NULL, &result, &error));
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
    return result;
}

int main(void) {
    cxpr_error error = {0};
    char* source = read_fixture("fixtures/games/collision_queries.cxpr");
    cxpr_model* model;
    cxpr_context* context;
    cxpr_value hit_value;
    cxpr_value hits_value;
    cxpr_value result;

    assert(source != NULL);
    assert(strstr(source, "first_hit = hits[0]") != NULL);
    assert(strstr(source, "hit_point = (hits[0]).point") != NULL);
    assert(strstr(source, "hit_normal = (hits[0]).normal") != NULL);
    assert(strstr(source, "hit_entity = (hits[0]).entity") != NULL);
    assert(strstr(source, "state position_x = 0") != NULL);
    model = cxpr_model_parse(source, &error);
    if (!model) fprintf(stderr, "collision fixture parse: %s\n", error.message);
    assert(model != NULL);
    assert(cxpr_model_output_count(model) == 8u);
    /* Model/session execution awaits Phase 6 typed external-array planning.
     * Exercise the implemented array/struct semantics directly in the fixture
     * syntax above and the tree-expression runtime below. */
    context = cxpr_context_new();
    assert(context != NULL);

    hit_value = hit(3.0, 4.0, -1.0, 0.0, "wall-7");
    hits_value = cxpr_array(cxpr_array_value_new(&hit_value, 1u));
    assert(hit_value.s != NULL && hits_value.a != NULL);
    cxpr_context_set_value(context, "hits", &hits_value);
    cxpr_value_free(&hits_value);
    cxpr_value_free(&hit_value);
    result = evaluate("((hits[0]).point).x", context);
    assert(result.type == CXPR_VALUE_NUMBER);
    assert_near(result.d, 3.0);
    result = evaluate("hits[0]", context);
    assert(result.type == CXPR_VALUE_STRUCT && result.s != NULL);
    assert(result.s->field_count == 3u);
    cxpr_value_free(&result);
    result = evaluate("(hits[0]).entity", context);
    assert(result.type == CXPR_VALUE_STRING);
    assert(strcmp(result.str, "wall-7") == 0);
    cxpr_value_free(&result);

    cxpr_context_free(context);
    cxpr_model_free(model);
    free(source);
    puts("cxpr collision query fixture tests passed.");
    return 0;
}
