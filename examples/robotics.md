# Robotics Example

Related test: [`../tests/examples/robotics.test.c`](../tests/examples/robotics.test.c)

This example shows threshold-based control logic and struct-aware helper functions. The setup below defines the runtime values and parameters used by the expressions.

```c
#include <cxpr/cxpr.h>
#include <stdio.h>

int main(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_register_defaults(reg);

    cxpr_context_set(ctx, "distance_front", 0.42);
    cxpr_context_set(ctx, "battery",        76.0);
    cxpr_context_set(ctx, "slip_ratio",     0.03);
    cxpr_context_set(ctx, "heading_error",   4.0);
    cxpr_context_set(ctx, "max_speed",       2.0);
    cxpr_context_set_param(ctx, "stop_distance", 0.25);
    cxpr_context_set_param(ctx, "max_slip",      0.10);
    cxpr_context_set_param(ctx, "max_heading_error", 12.0);

    cxpr_expr_ast* stop_expr = cxpr_expr_ast_parse(
        parser,
        "distance_front < $stop_distance ? 0.0 : (battery > 20 ? max_speed : 0.0)",
        &err
    );

    printf("cmd_vel=%.2f\n", cxpr_expr_ast_eval_double(stop_expr, ctx, reg, &err));

    cxpr_expr_ast_free(stop_expr);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_parser_free(parser);
    return 0;
}
```

```text
distance_front < $stop_distance ? 0.0 : (battery > 20 ? max_speed : 0.0)
slip_ratio > $max_slip or abs(heading_error) > $max_heading_error
```

Register helpers when domain logic needs to collapse multiple fields into one scalar:

```c
#include <cxpr/cxpr.h>
#include <math.h>
#include <stdio.h>

static double fn_planar_goal_range(const double* args, size_t argc, void* ud) {
    (void)argc;
    (void)ud;
    double dx = args[0] - args[2];
    double dy = args[1] - args[3];
    return sqrt(dx * dx + dy * dy);
}

static double fn_spatial_waypoint_range(const double* args, size_t argc, void* ud) {
    (void)argc;
    (void)ud;
    double dx = args[0] - args[3];
    double dy = args[1] - args[4];
    double dz = args[2] - args[5];
    return sqrt(dx * dx + dy * dy + dz * dz);
}

int main(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    const char* xy[] = {"x", "y"};
    const char* xyz[] = {"x", "y", "z"};
    double goal2_xy[] = {3.0, 0.0};
    double pose2_xy[] = {0.0, 4.0};
    double goal3_xyz[] = {3.0, 0.0, 0.0};
    double pose3_xyz[] = {0.0, 0.0, 0.0};

    cxpr_register_defaults(reg);
    cxpr_registry_add_fn(reg, "planar_goal_range", fn_planar_goal_range, xy, 2, 2, NULL, NULL);
    cxpr_registry_add(reg, "spatial_waypoint_range", fn_spatial_waypoint_range, 6, 6, NULL, NULL);

    cxpr_context_set_fields(ctx, "goal2", xy, goal2_xy, 2);
    cxpr_context_set_fields(ctx, "pose2", xy, pose2_xy, 2);
    cxpr_context_set_fields(ctx, "goal3", xyz, goal3_xyz, 3);
    cxpr_context_set_fields(ctx, "pose3", xyz, pose3_xyz, 3);
    cxpr_context_set_param(ctx, "capture_radius", 5.0);

    cxpr_expr_ast* ast2 = cxpr_expr_ast_parse(parser, "planar_goal_range(goal2, pose2) < $capture_radius", &err);
    cxpr_expr_ast* ast3 = cxpr_expr_ast_parse(
        parser,
        "spatial_waypoint_range(goal3.x, goal3.y, goal3.z, pose3.x, pose3.y, pose3.z) < $capture_radius",
        &err
    );

    printf("near2=%d near3=%d\n",
           cxpr_expr_ast_eval_bool(ast2, ctx, reg, &err),
           cxpr_expr_ast_eval_bool(ast3, ctx, reg, &err));

    cxpr_expr_ast_free(ast3);
    cxpr_expr_ast_free(ast2);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_parser_free(parser);
    return 0;
}
```

```text
planar_goal_range(goal2, pose2) < $capture_radius
spatial_waypoint_range(goal3.x, goal3.y, goal3.z, pose3.x, pose3.y, pose3.z) < $capture_radius
```

## Sensor Loop With Engine

For streaming robotics data, `cxpr_engine` is the higher-level path: declare the
control expressions once, bind sensor slots once, then tick the engine for each
frame. Watches turn guard transitions into events the host can dispatch after
the tick.

```c
#include <cxpr/cxpr.h>
#include <cxpr/engine.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    double distance_front;
    double battery;
    double heading_error;
    double slip_ratio;
    double pose_x;
    double pose_y;
    double goal_x;
    double goal_y;
} sensor_frame_t;

static double as_number(cxpr_value value) {
    return value.type == CXPR_VALUE_NUMBER ? value.d : 0.0;
}

static int as_bool(cxpr_value value) {
    return value.type == CXPR_VALUE_BOOL ? (value.b ? 1 : 0) : 0;
}

static double fn_planar_goal_range(const double* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    double dx = args[0] - args[2];
    double dy = args[1] - args[3];
    return sqrt(dx * dx + dy * dy);
}

int main(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};

    const sensor_frame_t frames[] = {
        {0.42, 76.0,  4.0, 0.03, 0.0, 4.0, 3.0, 0.0},
        {0.18, 76.0,  6.0, 0.03, 1.0, 3.0, 3.0, 0.0},
        {0.35, 52.0, 18.0, 0.14, 2.6, 0.2, 3.0, 0.0}
    };

    cxpr_register_defaults(reg);
    cxpr_registry_add(reg, "planar_goal_range", fn_planar_goal_range, 4, 4, NULL, NULL);

    const cxpr_expression_def exprs[] = {
        {"cmd_vel", "distance_front < $stop_distance ? 0.0 : (battery > 20 ? $max_speed : 0.0)"},
        {"fault_guard", "slip_ratio > $max_slip or abs(heading_error) > $max_heading_error"},
        {"reached_goal", "planar_goal_range(pose_x, pose_y, goal_x, goal_y) < $capture_radius"}
    };
    const cxpr_context_entry params[] = {
        {"stop_distance", 0.25},
        {"max_slip", 0.10},
        {"max_heading_error", 12.0},
        {"capture_radius", 0.5},
        {"max_speed", 2.0}
    };
    const cxpr_engine_watch_def watches[] = {
        {"fault_guard", CXPR_EDGE_RISING},
        {"reached_goal", CXPR_EDGE_RISING}
    };
    const cxpr_engine_config cfg = {
        .registry = reg,
        .expressions = exprs,
        .expression_count = CXPR_ARRAY_COUNT(exprs),
        .params = params,
        .param_count = CXPR_ARRAY_COUNT(params),
        .watches = watches,
        .watch_count = CXPR_ARRAY_COUNT(watches)
    };

    cxpr_engine_session* session = cxpr_engine_session_create(&cfg, &err);
    if (!session) return 1;

    cxpr_context* input_ctx = cxpr_context_new();
    const char* slot_names[] = {
        "distance_front", "battery", "heading_error", "slip_ratio",
        "pose_x", "pose_y", "goal_x", "goal_y"
    };
    cxpr_context_slot slots[CXPR_ARRAY_COUNT(slot_names)];
    for (size_t i = 0; i < CXPR_ARRAY_COUNT(slot_names); ++i) {
        cxpr_context_set(input_ctx, slot_names[i], 0.0);
    }
    if (!cxpr_context_slots_bind(input_ctx, slot_names, slots, CXPR_ARRAY_COUNT(slots))) {
        return 1;
    }

    for (size_t i = 0; i < CXPR_ARRAY_COUNT(frames); ++i) {
        const double values[] = {
            frames[i].distance_front, frames[i].battery,
            frames[i].heading_error, frames[i].slip_ratio,
            frames[i].pose_x, frames[i].pose_y,
            frames[i].goal_x, frames[i].goal_y
        };
        const cxpr_engine_event* events = NULL;
        size_t event_count = 0;
        bool found = false;

        cxpr_context_slots_set(slots, values, CXPR_ARRAY_COUNT(slots));
        if (!cxpr_engine_tick_fallback(session, input_ctx, &events, &event_count, &err)) return 1;

        printf("frame %zu cmd=%.2f guard=%d reached=%d\n",
               i,
               as_number(cxpr_engine_get(session, "cmd_vel", &found)),
               as_bool(cxpr_engine_get(session, "fault_guard", &found)),
               as_bool(cxpr_engine_get(session, "reached_goal", &found)));

        for (size_t e = 0; e < event_count; ++e) {
            printf("  event %s\n", events[e].expr_name);
        }
    }

    cxpr_context_free(input_ctx);
    cxpr_engine_session_free(session);
    cxpr_registry_free(reg);
    return 0;
}
```

When each cycle writes the same set of sensor keys, pre-bound slots avoid repeated
name lookup. Use column or view sources instead when your sensor data is already
stored as indexed arrays and you want engine-owned lookback over the series.

## Run Test

From `libs/cxpr/`:

```bash
cmake --build build --target test_examples_robotics
./build/tests/test_examples_robotics
```
