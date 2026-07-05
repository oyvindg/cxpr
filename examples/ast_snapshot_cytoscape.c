#include <cxpr/cxpr.h>
#include <cxpr/engine.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct demo_scenario {
    const char* name;
    const cxpr_expression_def* expressions;
    size_t expression_count;
    const cxpr_engine_column_source_def* columns;
    size_t column_count;
    size_t tick;
    void (*set_context)(cxpr_context* ctx);
} demo_scenario;

static double demo_rsi(const double* args, size_t argc, void* userdata) {
    (void)args;
    (void)argc;
    (void)userdata;
    return 42.0;
}

static const double trading_close[] = {100.0, 101.2, 100.8, 103.4, 104.1};
static const double trading_volume[] = {900.0, 1200.0, 1600.0, 1300.0, 1800.0};
static const cxpr_expression_def trading_expressions[] = {
        { "trend", "ema_fast > ema_slow" },
        { "breakout", "close > close[1]" },
        { "momentum_ok", "close - close[3] > 2" },
        { "volume_ok", "volume > volume[1] and volume > $min_volume" },
        { "range_floor", "min(close, close[1], close[3])" },
        { "range_ceiling", "max(close, close[1], close[3])" },
        { "range_ok", "range_ceiling - range_floor > $min_range" },
        { "pullback", "falling(close, 2)" },
        { "regime_allowed", "contains(2, [1, 2, 3])" },
        { "pipe_score", "(close - close[3]) |> abs |> clamp(0, 3)" },
        { "pipe_ok", "pipe_score > $min_range" },
        { "entry", "trend and breakout and momentum_ok and volume_ok and range_ok and regime_allowed and pipe_ok" },
        { "risk_block", "rsi(14) > $rsi_exit" },
        { "blocked_entry", "risk_block and volume_ok" },
        { "exit", "close < close[1] or rsi(14) > 70" },
};
static const cxpr_engine_column_source_def trading_columns[] = {
    { "close", &trading_close[0], sizeof(trading_close[0]), sizeof(trading_close) / sizeof(trading_close[0]) },
    { "volume", &trading_volume[0], sizeof(trading_volume[0]), sizeof(trading_volume) / sizeof(trading_volume[0]) },
};

static const double physics_position[] = {0.0, 2.1, 4.7, 7.8, 11.6};
static const double physics_temperature[] = {21.0, 22.2, 23.1, 24.0, 24.7};
static const cxpr_expression_def physics_expressions[] = {
    { "displacement", "position - position[1]" },
    { "velocity", "displacement / $dt" },
    { "acceleration", "(velocity - $previous_velocity) / $dt" },
    { "force", "$mass * acceleration" },
    { "kinetic_energy", "0.5 * $mass * velocity * velocity" },
    { "normalized_energy", "energy_norm(velocity, $mass, $energy_cap)" },
    { "motion_rising", "rising(position, 3)" },
    { "thermal_ok", "temperature < $max_temperature" },
    { "force_ok", "abs(force) < $max_force" },
    { "energy_score", "kinetic_energy |> clamp(0, 500)" },
    { "experiment_ok", "motion_rising and thermal_ok and force_ok" },
};
static const cxpr_engine_column_source_def physics_columns[] = {
    { "position", &physics_position[0], sizeof(physics_position[0]), sizeof(physics_position) / sizeof(physics_position[0]) },
    { "temperature", &physics_temperature[0], sizeof(physics_temperature[0]), sizeof(physics_temperature) / sizeof(physics_temperature[0]) },
};

static const double chemistry_concentration[] = {0.42, 0.47, 0.55, 0.61, 0.66};
static const double chemistry_temperature[] = {78.0, 76.0, 74.5, 73.0, 71.5};
static const double chemistry_ph[] = {6.8, 6.9, 7.0, 7.1, 7.2};
static const cxpr_expression_def chemistry_expressions[] = {
    { "concentration_delta", "concentration - concentration[1]" },
    { "reaction_rate", "abs(concentration_delta) / $dt" },
    { "ph_ok", "ph > $min_ph and ph < $max_ph" },
    { "cooling", "falling(temperature, 2)" },
    { "catalyst_ok", "contains(catalyst_id, [2, 4, 7])" },
    { "yield_score", "(reaction_rate * yield_pct) |> clamp(0, 100)" },
    { "thermal_safe", "temperature < $max_temperature" },
    { "reaction_ok", "ph_ok and cooling and catalyst_ok and thermal_safe" },
};
static const cxpr_engine_column_source_def chemistry_columns[] = {
    { "concentration", &chemistry_concentration[0], sizeof(chemistry_concentration[0]), sizeof(chemistry_concentration) / sizeof(chemistry_concentration[0]) },
    { "temperature", &chemistry_temperature[0], sizeof(chemistry_temperature[0]), sizeof(chemistry_temperature) / sizeof(chemistry_temperature[0]) },
    { "ph", &chemistry_ph[0], sizeof(chemistry_ph[0]), sizeof(chemistry_ph) / sizeof(chemistry_ph[0]) },
};

static const double robotics_distance_front[] = {1.20, 0.92, 0.58, 0.36, 0.42};
static const double robotics_battery[] = {82.0, 80.0, 78.0, 76.0, 74.0};
static const double robotics_slip_ratio[] = {0.02, 0.04, 0.08, 0.11, 0.06};
static const double robotics_heading_error[] = {2.0, 3.5, 6.0, 8.5, 4.0};
static const double robotics_pose_x[] = {0.0, 0.8, 1.7, 2.6, 3.4};
static const double robotics_pose_y[] = {0.0, 0.2, 0.4, 0.5, 0.6};
static const double robotics_motor_temp[] = {41.0, 44.0, 49.0, 53.0, 55.0};
static const cxpr_expression_def robotics_expressions[] = {
    { "obstacle_clear", "distance_front > $stop_distance" },
    { "battery_ok", "battery > $min_battery" },
    { "heading_ok", "abs(heading_error) < $max_heading_error" },
    { "slip_alert", "slip_ratio > $max_slip" },
    { "goal_distance", "range2($goal_x - pose_x, $goal_y - pose_y)" },
    { "near_goal", "goal_distance < $capture_radius" },
    { "obstacle_closing", "falling(distance_front, 2)" },
    { "thermal_ok", "motor_temp < $max_motor_temp" },
    { "steering_score", "abs(heading_error) |> clamp(0, 12)" },
    { "cmd_vel", "obstacle_clear ? (battery_ok ? $max_speed : $crawl_speed) : 0" },
    { "mission_mode_ok", "contains(mission_mode, [1, 2])" },
    { "drive_allowed", "obstacle_clear and battery_ok and heading_ok and thermal_ok and mission_mode_ok" },
    { "safety_stop", "slip_alert or near_goal or not drive_allowed" },
};
static const cxpr_engine_column_source_def robotics_columns[] = {
    { "distance_front", &robotics_distance_front[0], sizeof(robotics_distance_front[0]), sizeof(robotics_distance_front) / sizeof(robotics_distance_front[0]) },
    { "battery", &robotics_battery[0], sizeof(robotics_battery[0]), sizeof(robotics_battery) / sizeof(robotics_battery[0]) },
    { "slip_ratio", &robotics_slip_ratio[0], sizeof(robotics_slip_ratio[0]), sizeof(robotics_slip_ratio) / sizeof(robotics_slip_ratio[0]) },
    { "heading_error", &robotics_heading_error[0], sizeof(robotics_heading_error[0]), sizeof(robotics_heading_error) / sizeof(robotics_heading_error[0]) },
    { "pose_x", &robotics_pose_x[0], sizeof(robotics_pose_x[0]), sizeof(robotics_pose_x) / sizeof(robotics_pose_x[0]) },
    { "pose_y", &robotics_pose_y[0], sizeof(robotics_pose_y[0]), sizeof(robotics_pose_y) / sizeof(robotics_pose_y[0]) },
    { "motor_temp", &robotics_motor_temp[0], sizeof(robotics_motor_temp[0]), sizeof(robotics_motor_temp) / sizeof(robotics_motor_temp[0]) },
};

static const double quantum_alpha_re[] = {0.92, 0.88, 0.81, 0.72, 0.64};
static const double quantum_alpha_im[] = {0.00, 0.06, 0.10, 0.14, 0.18};
static const double quantum_beta_re[] = {0.18, 0.28, 0.39, 0.51, 0.63};
static const double quantum_beta_im[] = {0.34, 0.36, 0.40, 0.44, 0.43};
static const double quantum_phase[] = {0.10, 0.35, 0.72, 1.08, 1.42};
static const double quantum_decoherence[] = {0.02, 0.04, 0.07, 0.11, 0.16};
static const double quantum_measurement[] = {0.12, 0.24, 0.37, 0.59, 0.71};
static const cxpr_expression_def quantum_expressions[] = {
    { "p_zero_raw", "probability(alpha_re, alpha_im)" },
    { "p_one_raw", "probability(beta_re, beta_im)" },
    { "normalization", "p_zero_raw + p_one_raw" },
    { "normalized_p_zero", "p_zero_raw / normalization" },
    { "normalized_p_one", "p_one_raw / normalization" },
    { "coherence", "1 - decoherence" },
    { "phase_delta", "abs(phase - phase[1])" },
    { "phase_stable", "phase_delta < $max_phase_step" },
    { "decoherence_rising", "rising(decoherence, 3)" },
    { "state_bias", "normalized_p_one - normalized_p_zero" },
    { "measurement_window", "measurement > $gate_low and measurement < $gate_high" },
    { "basis_allowed", "contains(basis_id, [0, 1, 3])" },
    { "probability_floor", "min(normalized_p_zero, normalized_p_one)" },
    { "probability_ceiling", "max(normalized_p_zero, normalized_p_one)" },
    { "collapse_candidate", "normalized_p_one > $collapse_threshold and measurement_window" },
    { "gate_score", "(state_bias * coherence) |> abs |> clamp(0, 1)" },
    { "adaptive_threshold", "decoherence_rising ? $strict_gate_score : $min_gate_score" },
    { "gate_allowed", "gate_score > adaptive_threshold and phase_stable and basis_allowed and not decoherence_rising" },
    { "experiment_accept", "gate_allowed or collapse_candidate" },
};
static const cxpr_engine_column_source_def quantum_columns[] = {
    { "alpha_re", &quantum_alpha_re[0], sizeof(quantum_alpha_re[0]), sizeof(quantum_alpha_re) / sizeof(quantum_alpha_re[0]) },
    { "alpha_im", &quantum_alpha_im[0], sizeof(quantum_alpha_im[0]), sizeof(quantum_alpha_im) / sizeof(quantum_alpha_im[0]) },
    { "beta_re", &quantum_beta_re[0], sizeof(quantum_beta_re[0]), sizeof(quantum_beta_re) / sizeof(quantum_beta_re[0]) },
    { "beta_im", &quantum_beta_im[0], sizeof(quantum_beta_im[0]), sizeof(quantum_beta_im) / sizeof(quantum_beta_im[0]) },
    { "phase", &quantum_phase[0], sizeof(quantum_phase[0]), sizeof(quantum_phase) / sizeof(quantum_phase[0]) },
    { "decoherence", &quantum_decoherence[0], sizeof(quantum_decoherence[0]), sizeof(quantum_decoherence) / sizeof(quantum_decoherence[0]) },
    { "measurement", &quantum_measurement[0], sizeof(quantum_measurement[0]), sizeof(quantum_measurement) / sizeof(quantum_measurement[0]) },
};

static void set_trading_context(cxpr_context* ctx) {
    cxpr_context_set(ctx, "ema_fast", 104.5);
    cxpr_context_set(ctx, "ema_slow", 102.0);
    cxpr_context_set_param(ctx, "min_volume", 1500.0);
    cxpr_context_set_param(ctx, "min_range", 2.0);
    cxpr_context_set_param(ctx, "rsi_exit", 70.0);
}

static void set_physics_context(cxpr_context* ctx) {
    cxpr_context_set_param(ctx, "dt", 0.5);
    cxpr_context_set_param(ctx, "mass", 12.0);
    cxpr_context_set_param(ctx, "previous_velocity", 6.2);
    cxpr_context_set_param(ctx, "max_temperature", 30.0);
    cxpr_context_set_param(ctx, "max_force", 100.0);
    cxpr_context_set_param(ctx, "energy_cap", 500.0);
}

static void set_chemistry_context(cxpr_context* ctx) {
    cxpr_context_set(ctx, "catalyst_id", 4.0);
    cxpr_context_set(ctx, "yield_pct", 82.0);
    cxpr_context_set_param(ctx, "dt", 2.0);
    cxpr_context_set_param(ctx, "min_ph", 6.5);
    cxpr_context_set_param(ctx, "max_ph", 7.5);
    cxpr_context_set_param(ctx, "max_temperature", 75.0);
}

static void set_robotics_context(cxpr_context* ctx) {
    cxpr_context_set(ctx, "mission_mode", 2.0);
    cxpr_context_set_param(ctx, "stop_distance", 0.35);
    cxpr_context_set_param(ctx, "min_battery", 20.0);
    cxpr_context_set_param(ctx, "max_heading_error", 12.0);
    cxpr_context_set_param(ctx, "max_slip", 0.10);
    cxpr_context_set_param(ctx, "goal_x", 4.0);
    cxpr_context_set_param(ctx, "goal_y", 0.8);
    cxpr_context_set_param(ctx, "capture_radius", 0.8);
    cxpr_context_set_param(ctx, "max_motor_temp", 60.0);
    cxpr_context_set_param(ctx, "max_speed", 2.0);
    cxpr_context_set_param(ctx, "crawl_speed", 0.35);
}

static void set_quantum_context(cxpr_context* ctx) {
    cxpr_context_set(ctx, "basis_id", 1.0);
    cxpr_context_set_param(ctx, "max_phase_step", 0.45);
    cxpr_context_set_param(ctx, "gate_low", 0.20);
    cxpr_context_set_param(ctx, "gate_high", 0.80);
    cxpr_context_set_param(ctx, "collapse_threshold", 0.55);
    cxpr_context_set_param(ctx, "min_gate_score", 0.08);
    cxpr_context_set_param(ctx, "strict_gate_score", 0.18);
}

static const demo_scenario scenarios[] = {
    {
        "trading",
        trading_expressions,
        sizeof(trading_expressions) / sizeof(trading_expressions[0]),
        trading_columns,
        sizeof(trading_columns) / sizeof(trading_columns[0]),
        4,
        set_trading_context,
    },
    {
        "physics",
        physics_expressions,
        sizeof(physics_expressions) / sizeof(physics_expressions[0]),
        physics_columns,
        sizeof(physics_columns) / sizeof(physics_columns[0]),
        4,
        set_physics_context,
    },
    {
        "chemistry",
        chemistry_expressions,
        sizeof(chemistry_expressions) / sizeof(chemistry_expressions[0]),
        chemistry_columns,
        sizeof(chemistry_columns) / sizeof(chemistry_columns[0]),
        4,
        set_chemistry_context,
    },
    {
        "robotics",
        robotics_expressions,
        sizeof(robotics_expressions) / sizeof(robotics_expressions[0]),
        robotics_columns,
        sizeof(robotics_columns) / sizeof(robotics_columns[0]),
        4,
        set_robotics_context,
    },
    {
        "quantum",
        quantum_expressions,
        sizeof(quantum_expressions) / sizeof(quantum_expressions[0]),
        quantum_columns,
        sizeof(quantum_columns) / sizeof(quantum_columns[0]),
        4,
        set_quantum_context,
    },
};

static const demo_scenario* find_scenario(const char* name) {
    size_t i;
    for (i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i) {
        if (strcmp(name, scenarios[i].name) == 0) return &scenarios[i];
    }
    return NULL;
}

static int is_scenario_name(const char* value) {
    return find_scenario(value) != NULL;
}

int main(int argc, char** argv) {
    const char* out_path = NULL;
    const char* scenario_name = "trading";
    const demo_scenario* scenario = NULL;
    cxpr_engine_config cfg = {0};
    cxpr_registry* reg = NULL;
    cxpr_engine_session* session = NULL;
    cxpr_context* ctx;
    cxpr_error err = {0};
    cxpr_eval_snapshot_flow flow;
    FILE* out = stdout;
    int rc = 1;
    int argi;

    for (argi = 1; argi < argc; ++argi) {
        if (is_scenario_name(argv[argi])) {
            scenario_name = argv[argi];
        } else if (!out_path) {
            out_path = argv[argi];
        } else {
            fprintf(stderr, "usage: %s [output.json] [trading|physics|chemistry|robotics|quantum]\n", argv[0]);
            return 2;
        }
    }

    scenario = find_scenario(scenario_name);
    if (!scenario) {
        fprintf(stderr, "unknown scenario: %s\n", scenario_name);
        return 2;
    }

    cfg.expressions = scenario->expressions;
    cfg.expression_count = scenario->expression_count;
    cfg.column_sources = scenario->columns;
    cfg.column_source_count = scenario->column_count;
    reg = cxpr_registry_new();
    if (!reg) {
        fprintf(stderr, "failed to create registry\n");
        goto done;
    }
    cxpr_register_defaults(reg);
    cxpr_registry_add(reg, "rsi", demo_rsi, 1, 1, NULL, NULL);
    err = cxpr_registry_define_fn(reg, "square(x) => x * x");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "define_fn square error: %s\n", err.message ? err.message : "unknown");
        goto done;
    }
    err = cxpr_registry_define_fn(reg,
                                  "energy_norm(v, mass, cap) => clamp(0.5 * mass * square(v), 0, cap)");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "define_fn energy_norm error: %s\n", err.message ? err.message : "unknown");
        goto done;
    }
    err = cxpr_registry_define_fn(reg, "range2(dx, dy) => sqrt(square(dx) + square(dy))");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "define_fn range2 error: %s\n", err.message ? err.message : "unknown");
        goto done;
    }
    err = cxpr_registry_define_fn(reg, "probability(re, im) => square(re) + square(im)");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "define_fn probability error: %s\n", err.message ? err.message : "unknown");
        goto done;
    }
    cfg.registry = reg;
    session = cxpr_engine_session_create(&cfg, &err);
    if (!session) {
        fprintf(stderr, "engine create error: %s\n", err.message ? err.message : "unknown");
        goto done;
    }

    ctx = cxpr_engine_session_context(session);
    scenario->set_context(ctx);

    if (!cxpr_engine_tick_at(session, scenario->tick, NULL, NULL, &err)) {
        fprintf(stderr, "engine tick error: %s\n", err.message ? err.message : "unknown");
        goto done;
    }

    if (!cxpr_engine_snapshot_flow(session, &flow, &err)) {
        fprintf(stderr, "snapshot error: %s\n", err.message ? err.message : "unknown");
        goto done;
    }

    if (out_path) {
        out = fopen(out_path, "wb");
        if (!out) {
            fprintf(stderr, "failed to open %s\n", out_path);
            cxpr_eval_snapshot_flow_free(&flow);
            goto done;
        }
    }

    if (!cxpr_eval_snapshot_flow_write_json(&flow, out)) {
        fprintf(stderr, "failed to write snapshot JSON\n");
        goto cleanup_snapshot;
    }
    rc = 0;

cleanup_snapshot:
    if (out_path && out) fclose(out);
    cxpr_eval_snapshot_flow_free(&flow);

done:
    cxpr_engine_session_free(session);
    cxpr_registry_free(reg);
    return rc;
}
