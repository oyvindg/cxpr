#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cxpr/cxpr.h>
#include <cxpr/engine.h>

#define EPSILON 1e-12

static double fn_planar_goal_range(const double* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    double dx = args[0] - args[2];
    double dy = args[1] - args[3];
    return sqrt(dx * dx + dy * dy);
}

static double fn_spatial_waypoint_range(const double* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    double dx = args[0] - args[3];
    double dy = args[1] - args[4];
    double dz = args[2] - args[5];
    return sqrt(dx * dx + dy * dy + dz * dz);
}

static double as_number(cxpr_value value) {
    assert(value.type == CXPR_VALUE_NUMBER);
    return value.d;
}

static bool as_bool(cxpr_value value) {
    assert(value.type == CXPR_VALUE_BOOL);
    return value.b;
}

int main(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_register_defaults(reg);

    cxpr_context_set(ctx, "distance_front", 0.42);
    cxpr_context_set(ctx, "battery", 76.0);
    cxpr_context_set(ctx, "slip_ratio", 0.03);
    cxpr_context_set(ctx, "heading_error", 4.0);
    cxpr_context_set(ctx, "max_speed", 2.0);
    cxpr_context_set_param(ctx, "stop_distance", 0.25);
    cxpr_context_set_param(ctx, "max_slip", 0.10);
    cxpr_context_set_param(ctx, "max_heading_error", 12.0);

    cxpr_expr_ast* stop_expr = cxpr_expr_ast_parse(
        parser,
        "distance_front < $stop_distance ? 0.0 : (battery > 20 ? max_speed : 0.0)",
        &err
    );
    cxpr_expr_ast* slip_expr = cxpr_expr_ast_parse(
        parser,
        "slip_ratio > $max_slip or abs(heading_error) > $max_heading_error",
        &err
    );
    assert(stop_expr);
    assert(slip_expr);
    assert(fabs(cxpr_test_eval_ast_number(stop_expr, ctx, reg, &err) - 2.0) < EPSILON);
    assert(cxpr_test_eval_ast_bool(slip_expr, ctx, reg, &err) == false);

    const char* xy[] = {"x", "y"};
    const char* xyz[] = {"x", "y", "z"};
    double goal2_xy[] = {3.0, 0.0};
    double pose2_xy[] = {0.0, 4.0};
    double goal3_xyz[] = {3.0, 0.0, 0.0};
    double pose3_xyz[] = {0.0, 0.0, 0.0};

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
    assert(ast2);
    assert(ast3);
    assert(cxpr_test_eval_ast_bool(ast2, ctx, reg, &err) == false);
    assert(cxpr_test_eval_ast_bool(ast3, ctx, reg, &err) == true);

    {
        typedef struct {
            double distance_front;
            double battery;
            double heading_error;
            double slip_ratio;
            double pose_x;
            double pose_y;
            double goal_x;
            double goal_y;
            double expected_cmd;
            bool expected_guard;
            bool expected_reached;
        } sensor_frame_t;

        const sensor_frame_t frames[] = {
            {0.42, 76.0, 4.0, 0.03, 0.0, 4.0, 3.0, 0.0, 2.0, false, false},
            {0.18, 76.0, 6.0, 0.03, 1.0, 3.0, 3.0, 0.0, 0.0, false, false},
            {0.35, 52.0, 18.0, 0.14, 2.6, 0.2, 3.0, 0.0, 2.0, true, true}
        };

        cxpr_context_set_param(ctx, "capture_radius", 0.5);
        cxpr_expr_ast* cmd_expr = cxpr_expr_ast_parse(
            parser,
            "distance_front < $stop_distance ? 0.0 : (battery > 20 ? max_speed : 0.0)",
            &err
        );
        cxpr_expr_ast* guard_expr = cxpr_expr_ast_parse(
            parser,
            "slip_ratio > $max_slip or abs(heading_error) > $max_heading_error",
            &err
        );
        cxpr_expr_ast* reached_expr = cxpr_expr_ast_parse(parser, "planar_goal_range(goal, pose) < $capture_radius", &err);
        assert(cmd_expr);
        assert(guard_expr);
        assert(reached_expr);

        for (size_t i = 0; i < CXPR_ARRAY_COUNT(frames); ++i) {
            double pose_xy[] = {frames[i].pose_x, frames[i].pose_y};
            double goal_xy[] = {frames[i].goal_x, frames[i].goal_y};

            cxpr_context_set(ctx, "distance_front", frames[i].distance_front);
            cxpr_context_set(ctx, "battery", frames[i].battery);
            cxpr_context_set(ctx, "heading_error", frames[i].heading_error);
            cxpr_context_set(ctx, "slip_ratio", frames[i].slip_ratio);
            cxpr_context_set_fields(ctx, "pose", xy, pose_xy, 2);
            cxpr_context_set_fields(ctx, "goal", xy, goal_xy, 2);

            assert(fabs(cxpr_test_eval_ast_number(cmd_expr, ctx, reg, &err) - frames[i].expected_cmd) < EPSILON);
            assert(cxpr_test_eval_ast_bool(guard_expr, ctx, reg, &err) == frames[i].expected_guard);
            assert(cxpr_test_eval_ast_bool(reached_expr, ctx, reg, &err) == frames[i].expected_reached);
        }

        cxpr_expr_ast_free(reached_expr);
        cxpr_expr_ast_free(guard_expr);
        cxpr_expr_ast_free(cmd_expr);
    }

    {
        typedef struct {
            double distance_front;
            double battery;
            double heading_error;
            double slip_ratio;
            double pose_x;
            double pose_y;
            double goal_x;
            double goal_y;
            double expected_cmd;
            bool expected_guard;
            bool expected_reached;
        } sensor_frame_t;

        const sensor_frame_t frames[] = {
            {0.42, 76.0, 4.0, 0.03, 0.0, 4.0, 3.0, 0.0, 2.0, false, false},
            {0.18, 76.0, 6.0, 0.03, 1.0, 3.0, 3.0, 0.0, 0.0, false, false},
            {0.35, 52.0, 18.0, 0.14, 2.6, 0.2, 3.0, 0.0, 2.0, true, true}
        };
        const cxpr_expression_def exprs[] = {
            {"cmd_vel", "distance_front < $stop_distance ? 0.0 : (battery > 20 ? $max_speed : 0.0)"},
            {"fault_guard", "slip_ratio > $max_slip or abs(heading_error) > $max_heading_error"},
            {"reached_goal", "planar_distance(pose_x, pose_y, goal_x, goal_y) < $capture_radius"}
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
        const char* slot_names[] = {
            "distance_front", "battery", "heading_error", "slip_ratio",
            "pose_x", "pose_y", "goal_x", "goal_y"
        };
        cxpr_context_slot slots[CXPR_ARRAY_COUNT(slot_names)];
        cxpr_engine_config cfg = {0};
        cxpr_error engine_err = {0};
        cxpr_engine_session* session;
        cxpr_context* input_ctx;
        int guard_events = 0;
        int reached_events = 0;

        cxpr_registry_add(reg, "planar_distance", fn_planar_goal_range, 4, 4, NULL, NULL);

        cfg.registry = reg;
        cfg.expressions = exprs;
        cfg.expression_count = CXPR_ARRAY_COUNT(exprs);
        cfg.params = params;
        cfg.param_count = CXPR_ARRAY_COUNT(params);
        cfg.watches = watches;
        cfg.watch_count = CXPR_ARRAY_COUNT(watches);

        session = cxpr_engine_session_create(&cfg, &engine_err);
        assert(session);
        input_ctx = cxpr_context_new();
        assert(input_ctx);
        for (size_t i = 0; i < CXPR_ARRAY_COUNT(slot_names); ++i) {
            cxpr_context_set(input_ctx, slot_names[i], 0.0);
        }
        assert(cxpr_context_slots_bind(input_ctx, slot_names, slots, CXPR_ARRAY_COUNT(slots)));

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
            if (!cxpr_engine_tick_fallback(session, input_ctx, &events, &event_count, &engine_err)) {
                fprintf(stderr, "engine tick failed: %s\n",
                        engine_err.message ? engine_err.message : "?");
                assert(false);
            }

            assert(fabs(as_number(cxpr_engine_get(session, "cmd_vel", &found)) -
                        frames[i].expected_cmd) < EPSILON && found);
            assert(as_bool(cxpr_engine_get(session, "fault_guard", &found)) ==
                   frames[i].expected_guard && found);
            assert(as_bool(cxpr_engine_get(session, "reached_goal", &found)) ==
                   frames[i].expected_reached && found);

            for (size_t e = 0; e < event_count; ++e) {
                if (strcmp(events[e].expr_name, "fault_guard") == 0) guard_events++;
                if (strcmp(events[e].expr_name, "reached_goal") == 0) reached_events++;
            }
        }

        assert(guard_events == 1);
        assert(reached_events == 1);
        cxpr_context_free(input_ctx);
        cxpr_engine_session_free(session);
    }

    cxpr_expr_ast_free(ast3);
    cxpr_expr_ast_free(ast2);
    cxpr_expr_ast_free(slip_expr);
    cxpr_expr_ast_free(stop_expr);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_parser_free(parser);

    printf("  \342\234\223 robotics example\n");
    return 0;
}
