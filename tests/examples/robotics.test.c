#include <assert.h>
#include <math.h>
#include <stdio.h>

#include <cxpr/cxpr.h>
#include "../cxpr_test_internal.h"

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

    cxpr_ast* stop_expr = cxpr_parse(
        parser,
        "distance_front < $stop_distance ? 0.0 : (battery > 20 ? max_speed : 0.0)",
        &err
    );
    cxpr_ast* slip_expr = cxpr_parse(
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

    cxpr_ast* ast2 = cxpr_parse(parser, "planar_goal_range(goal2, pose2) < $capture_radius", &err);
    cxpr_ast* ast3 = cxpr_parse(
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
        cxpr_ast* cmd_expr = cxpr_parse(
            parser,
            "distance_front < $stop_distance ? 0.0 : (battery > 20 ? max_speed : 0.0)",
            &err
        );
        cxpr_ast* guard_expr = cxpr_parse(
            parser,
            "slip_ratio > $max_slip or abs(heading_error) > $max_heading_error",
            &err
        );
        cxpr_ast* reached_expr = cxpr_parse(parser, "planar_goal_range(goal, pose) < $capture_radius", &err);
        assert(cmd_expr);
        assert(guard_expr);
        assert(reached_expr);

        for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); ++i) {
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

        cxpr_ast_free(reached_expr);
        cxpr_ast_free(guard_expr);
        cxpr_ast_free(cmd_expr);
    }

    cxpr_ast_free(ast3);
    cxpr_ast_free(ast2);
    cxpr_ast_free(slip_expr);
    cxpr_ast_free(stop_expr);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_parser_free(parser);

    printf("  \342\234\223 robotics example\n");
    return 0;
}
