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

static void assert_near(double actual, double expected) {
    assert(fabs(actual - expected) < 1e-9);
}

static double get_output_field(cxpr_context* context, const char* object, const char* field) {
    bool found = false;
    cxpr_value value = cxpr_context_get_field(context, object, field, &found);
    assert(found && value.type == CXPR_VALUE_NUMBER);
    return value.d;
}

static double get_output_number(cxpr_model_session* session, const char* name) {
    double value = 0.0;
    assert(cxpr_model_session_get_number(session, name, &value));
    return value;
}

static bool sample_ray(const cxpr_expr_ast* target, int64_t distance,
                       const cxpr_context* context, const cxpr_registry* registry,
                       void* userdata, cxpr_value* out, cxpr_error* error) {
    static const char* fields[] = {"x", "y"};
    cxpr_value values[2];
    bool found = false;
    double origin_x = cxpr_context_get(context, "origin_x", &found);
    double origin_y;
    double direction_x;
    double direction_y;
    double direction_length;
    (void)target;
    (void)registry;
    (void)error;
    if (userdata) ++*(size_t*)userdata;
    if (!found) return false;
    origin_y = cxpr_context_get(context, "origin_y", &found);
    if (!found) return false;
    direction_x = cxpr_context_get(context, "direction_x", &found);
    if (!found) return false;
    direction_y = cxpr_context_get(context, "direction_y", &found);
    if (!found) return false;
    direction_length = sqrt(direction_x * direction_x + direction_y * direction_y);
    values[0] = cxpr_num(origin_x +
                         (direction_length > 0.0 ? direction_x / direction_length : 0.0) *
                             (double)distance);
    values[1] = cxpr_num(origin_y +
                         (direction_length > 0.0 ? direction_y / direction_length : 0.0) *
                             (double)distance);
    *out = cxpr_struct(cxpr_struct_value_new(fields, values, 2u));
    return out->s != NULL;
}

static void test_square_bracket_fixture_parses(void) {
    cxpr_error error = {0};
    char* source = read_fixture("fixtures/syntax/square_brackets.cxpr");
    cxpr_model* model;
    assert(source != NULL);
    model = cxpr_model_parse(source, &error);
    if (!model) fprintf(stderr, "square-bracket fixture: %s\n", error.message);
    assert(model != NULL);
    assert(cxpr_model_output_count(model) == 14u);
    assert(strstr(source, "[10, 20, 30][1]") != NULL);
    assert(strstr(source, "vertices[vertex_index]") != NULL);
    assert(strstr(source, "[\"idle\", \"moving\"]") != NULL);
    assert(strstr(source, "passthrough(sensor)[2]") != NULL);
    assert(strstr(source, "sample(sensor).doubled[3]") != NULL);
    assert(strstr(source, "(ray[sample_distance]).x") != NULL);
    cxpr_model_free(model);
    free(source);
}

static void test_raytrace_predicts_hit_and_miss_paths(void) {
    cxpr_error error = {0};
    char* source = read_fixture("fixtures/robotics/raytrace_path.cxpr");
    cxpr_model* model;
    cxpr_model_compiled* program;
    cxpr_model_session* session;
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* context;
    bool hit = false;
    double distance = 0.0;
    size_t ray_samples = 0u;

    assert(source != NULL && registry != NULL);
    cxpr_register_defaults(registry);
    assert(cxpr_registry_add_index_capability(
        registry, "procedural_path", "ray", CXPR_VALUE_STRUCT,
        sample_ray, &ray_samples, NULL));
    model = cxpr_model_parse(source, &error);
    if (!model) fprintf(stderr, "raytrace fixture parse: %s\n", error.message);
    assert(model != NULL);
    assert(cxpr_model_output_count(model) == 11u);
    assert(strstr(source, "role = \"path_point\"") != NULL);
    assert(strstr(source, "path { id = \"predicted_ray\"") != NULL);
    assert(strstr(source, "state traced_distance = 0") != NULL);
    assert(strstr(source, "simulated_point = ray[round(simulated_distance)]") != NULL);
    program = cxpr_model_compile(model, registry, &error);
    if (!program) fprintf(stderr, "raytrace fixture compile: %s\n", error.message);
    assert(program != NULL);
    assert(cxpr_model_compiled_history_count(program) == 0u);
    session = cxpr_model_session_new(program, registry, &error);
    assert(session != NULL);
    context = cxpr_model_session_context(session);

    cxpr_context_set(context, "origin_x", 1.0);
    cxpr_context_set(context, "origin_y", 2.0);
    cxpr_context_set(context, "direction_x", 3.0);
    cxpr_context_set(context, "direction_y", 4.0);
    cxpr_context_set(context, "obstacle_x", 7.0);
    cxpr_context_set(context, "max_distance", 20.0);
    cxpr_context_set(context, "advance_distance", 2.0);
    cxpr_context_set(context, "reset", 0.0);
    if (!cxpr_model_session_tick(program, session, registry, &error)) {
        fprintf(stderr, "raytrace fixture tick: %s\n",
                error.message ? error.message : "(null)");
        assert(false);
    }
    assert(cxpr_model_session_get_bool(session, "hit", &hit) && hit);
    assert(cxpr_model_session_get_number(session, "travel_distance", &distance));
    assert_near(distance, 10.0);
    assert_near(get_output_field(context, "waypoint_50", "x"), 4.0);
    assert_near(get_output_field(context, "waypoint_50", "y"), 6.0);
    assert_near(get_output_field(context, "endpoint", "x"), 7.0);
    assert_near(get_output_field(context, "endpoint", "y"), 10.0);
    assert_near(get_output_field(context, "simulated_point", "x"), 2.2);
    assert_near(get_output_field(context, "simulated_point", "y"), 3.6);
    assert_near(get_output_number(session, "simulated_distance"), 2.0);
    assert(ray_samples >= 5u);

    cxpr_context_set(context, "advance_distance", 20.0);
    assert(cxpr_model_session_tick(program, session, registry, &error));
    assert_near(get_output_field(context, "simulated_point", "x"), 7.0);
    assert_near(get_output_field(context, "simulated_point", "y"), 10.0);
    assert_near(get_output_number(session, "simulated_distance"), 10.0);
    assert(cxpr_model_session_get_bool(session, "simulation_complete", &hit) && hit);

    cxpr_context_set(context, "origin_x", 1.0);
    cxpr_context_set(context, "origin_y", 2.0);
    cxpr_context_set(context, "direction_x", 0.0);
    cxpr_context_set(context, "direction_y", 2.0);
    cxpr_context_set(context, "obstacle_x", 7.0);
    cxpr_context_set(context, "max_distance", 5.0);
    cxpr_context_set(context, "advance_distance", 1.0);
    cxpr_context_set(context, "reset", 1.0);
    assert(cxpr_model_session_tick(program, session, registry, &error));
    assert(cxpr_model_session_get_bool(session, "hit", &hit) && !hit);
    assert_near(get_output_field(context, "endpoint", "x"), 1.0);
    assert_near(get_output_field(context, "endpoint", "y"), 7.0);

    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
    cxpr_registry_free(registry);
    cxpr_model_free(model);
    free(source);
}

int main(void) {
    test_square_bracket_fixture_parses();
    test_raytrace_predicts_hit_and_miss_paths();
    puts("cxpr square-bracket and raytrace path tests passed.");
    return 0;
}
