#include <cxpr/model/model.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CXPR_TEST_SOURCE_DIR
#define CXPR_TEST_SOURCE_DIR "."
#endif

typedef struct {
    double x;
    double y;
} game_vec2;

typedef struct {
    const game_vec2* values;
    size_t count;
} game_vec2_view;

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

static bool sample_vec2(const cxpr_expr_ast* target, int64_t index,
                        const cxpr_context* context, const cxpr_registry* registry,
                        void* userdata, cxpr_value* out, cxpr_error* error) {
    static const char* const fields[] = {"x", "y"};
    const game_vec2_view* view = (const game_vec2_view*)userdata;
    cxpr_value values[2];
    (void)target;
    (void)context;
    (void)registry;
    if (!view || index < 0 || (uint64_t)index >= view->count) {
        if (error) {
            error->code = CXPR_ERR_INDEX_OUT_OF_RANGE;
            error->message = "Game Vec2 index out of range";
        }
        return false;
    }
    values[0] = cxpr_num(view->values[index].x);
    values[1] = cxpr_num(view->values[index].y);
    *out = cxpr_struct(cxpr_struct_value_new(fields, values, 2u));
    return out->s != NULL;
}

static double output_number(cxpr_model_session* session, const char* name) {
    double value = NAN;
    assert(cxpr_model_session_get_number(session, name, &value));
    return value;
}

static double output_field(cxpr_context* context, const char* object, const char* field) {
    bool found = false;
    cxpr_value value = cxpr_context_get_field(context, object, field, &found);
    assert(found && value.type == CXPR_VALUE_NUMBER);
    return value.d;
}

static void assert_near(double actual, double expected) {
    assert(fabs(actual - expected) < 1e-8);
}

int main(void) {
    game_vec2 motion_values[] = {{0.0, 0.0}, {-1.0, 0.0}};
    game_vec2 target_values[] = {{10.0, 0.0}};
    game_vec2 obstacle_values[] = {{2.0, 0.5}, {50.0, 50.0}, {60.0, -40.0}};
    game_vec2_view motion = {motion_values, 2u};
    game_vec2_view targets = {target_values, 1u};
    game_vec2_view obstacles = {obstacle_values, 3u};
    cxpr_error error = {0};
    char* source = read_fixture("fixtures/games/fictional_projectile_avoidance.cxpr");
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_model* model;
    cxpr_model_compiled* program;
    cxpr_model_session* session;
    cxpr_context* context;
    bool avoiding = false;
    double first_x;
    double first_y;

    assert(source != NULL && registry != NULL);
    assert(strstr(source, "past = motion[1]") != NULL);
    assert(strstr(source, "present = motion[0]") != NULL);
    assert(strstr(source, "obstacle_2 = obstacles[2]") != NULL);
    assert(strstr(source, "future_3 =") != NULL);
    assert(strstr(source, "base_x = present.x") != NULL);
    cxpr_register_defaults(registry);
    assert(cxpr_registry_add_index_capability(
        registry, "game_motion_vec2", "motion", CXPR_VALUE_STRUCT,
        sample_vec2, &motion, NULL));
    assert(cxpr_registry_add_index_capability(
        registry, "game_target_vec2", "targets", CXPR_VALUE_STRUCT,
        sample_vec2, &targets, NULL));
    assert(cxpr_registry_add_index_capability(
        registry, "game_obstacle_vec2", "obstacles", CXPR_VALUE_STRUCT,
        sample_vec2, &obstacles, NULL));

    model = cxpr_model_parse(source, &error);
    if (!model) fprintf(stderr, "projectile fixture parse: %s\n", error.message);
    assert(model != NULL);
    assert(cxpr_model_output_count(model) == 10u);
    program = cxpr_model_compile(model, registry, &error);
    if (!program) fprintf(stderr, "projectile fixture compile: %s\n", error.message);
    assert(program != NULL);
    session = cxpr_model_session_new(program, registry, &error);
    assert(session != NULL);
    context = cxpr_model_session_context(session);

    cxpr_context_set(context, "obstacle_count", 3.0);
    cxpr_context_set(context, "dt", 1.0);
    cxpr_context_set(context, "speed", 1.0);
    cxpr_context_set(context, "avoid_radius", 4.0);
    cxpr_context_set(context, "avoid_strength", 0.3);
    cxpr_context_set(context, "reset", 1.0);
    if (!cxpr_model_session_tick(program, session, registry, &error)) {
        fprintf(stderr, "projectile fixture tick: %s\n",
                error.message ? error.message : "(null)");
        assert(false);
    }

    assert_near(output_field(context, "past_position", "x"), -1.0);
    assert_near(output_field(context, "present_position", "x"), 0.0);
    assert(cxpr_model_session_get_bool(session, "avoiding", &avoiding) && avoiding);
    first_x = output_field(context, "future_1", "x");
    first_y = output_field(context, "future_1", "y");
    assert(first_x > 0.0);
    assert(first_y < 0.0); /* The nearby upper obstacle bends the path downward. */
    assert_near(output_field(context, "simulated_position", "x"), first_x);
    assert_near(output_field(context, "simulated_position", "y"), first_y);
    assert(output_field(context, "future_2", "x") > first_x);
    assert(output_field(context, "future_3", "x") >
           output_field(context, "future_2", "x"));
    assert(output_number(session, "target_distance") > 9.9);

    /* Move the host-owned obstacle between ticks. The next prediction must be
     * recalculated without rebuilding the model or copying the obstacle array. */
    obstacle_values[0].x = 50.0;
    obstacle_values[0].y = 50.0;
    motion_values[1] = motion_values[0];
    motion_values[0].x = first_x;
    motion_values[0].y = first_y;
    cxpr_context_set(context, "reset", 0.0);
    assert(cxpr_model_session_tick(program, session, registry, &error));
    assert(cxpr_model_session_get_bool(session, "avoiding", &avoiding) && !avoiding);
    assert(output_number(session, "command_vx") > 0.99);
    assert(fabs(output_number(session, "command_vy")) < 0.1);

    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
    cxpr_registry_free(registry);
    free(source);
    puts("cxpr fictional projectile avoidance fixture tests passed.");
    return 0;
}
