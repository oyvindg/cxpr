#include <cxpr/cxpr.h>
#include <cxpr/engine.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct demo_host_context {
    const char* scenario_name;
} demo_host_context;

typedef struct demo_scenario {
    const char* name;
    const cxpr_expression_def* expressions;
    size_t expression_count;
    const cxpr_engine_column_source_def* columns;
    size_t column_count;
    size_t tick;
    void (*set_context)(cxpr_context* ctx);
} demo_scenario;

static int demo_json_string(FILE* out, const char* text) {
    const unsigned char* p = (const unsigned char*)(text ? text : "");

    if (fputc('"', out) == EOF) return 0;
    while (*p) {
        switch (*p) {
            case '"':
                if (fputs("\\\"", out) == EOF) return 0;
                break;
            case '\\':
                if (fputs("\\\\", out) == EOF) return 0;
                break;
            case '\n':
                if (fputs("\\n", out) == EOF) return 0;
                break;
            case '\r':
                if (fputs("\\r", out) == EOF) return 0;
                break;
            case '\t':
                if (fputs("\\t", out) == EOF) return 0;
                break;
            default:
                if (*p < 0x20u) {
                    if (fprintf(out, "\\u%04x", (unsigned int)*p) < 0) return 0;
                } else if (fputc((int)*p, out) == EOF) {
                    return 0;
                }
                break;
        }
        ++p;
    }
    return fputc('"', out) != EOF;
}

static const char* demo_expression_role(const char* name) {
    if (!name) return "expression";
    if (strcmp(name, "entry") == 0 || strcmp(name, "experiment_ok") == 0 ||
        strcmp(name, "reaction_ok") == 0 || strcmp(name, "drive_allowed") == 0 ||
        strcmp(name, "experiment_accept") == 0 || strcmp(name, "observation_safe") == 0 ||
        strcmp(name, "reactor_stable") == 0 || strcmp(name, "policy_green") == 0 ||
        strcmp(name, "decision_code") == 0 || strcmp(name, "stable_orbit") == 0 ||
        strcmp(name, "touchdown_ok") == 0) {
        return "decision";
    }
    if (strcmp(name, "exit") == 0 || strcmp(name, "safety_stop") == 0 ||
        strcmp(name, "blocked_entry") == 0 || strcmp(name, "risk_block") == 0 ||
        strstr(name, "horizon") || strcmp(name, "scram_required") == 0 ||
        strcmp(name, "coolant_alert") == 0 || strstr(name, "decline") ||
        strstr(name, "review")) {
        return "risk";
    }
    if (strstr(name, "ok") || strstr(name, "allowed")) return "filter";
    if (strstr(name, "score") || strstr(name, "ratio") || strstr(name, "ltv")) return "score";
    return "derived";
}

static const char* demo_output_label(const char* scenario, const char* name) {
    if (!scenario || !name) return NULL;
    if (strcmp(scenario, "trading") == 0) {
        if (strcmp(name, "entry") == 0) return "Entry";
        if (strcmp(name, "exit") == 0) return "Exit";
    } else if (strcmp(scenario, "physics") == 0) {
        if (strcmp(name, "experiment_ok") == 0) return "Experiment OK";
        if (strcmp(name, "force") == 0) return "Force";
        if (strcmp(name, "energy_score") == 0) return "Energy Score";
    } else if (strcmp(scenario, "orbital") == 0) {
        if (strcmp(name, "kepler_law_ok") == 0) return "Kepler Law OK";
        if (strcmp(name, "newton_law_ok") == 0) return "Newton Law OK";
        if (strcmp(name, "stable_orbit") == 0) return "Stable Orbit";
        if (strcmp(name, "kepler_ratio") == 0) return "Kepler Ratio";
    } else if (strcmp(scenario, "apollo11") == 0) {
        if (strcmp(name, "descent_profile_ok") == 0) return "Descent Profile OK";
        if (strcmp(name, "landing_energy_score") == 0) return "Landing Energy Score";
        if (strcmp(name, "fuel_margin_ok") == 0) return "Fuel Margin OK";
        if (strcmp(name, "touchdown_ok") == 0) return "Touchdown OK";
    } else if (strcmp(scenario, "chemistry") == 0) {
        if (strcmp(name, "reaction_ok") == 0) return "Reaction OK";
        if (strcmp(name, "yield_score") == 0) return "Yield Score";
    } else if (strcmp(scenario, "robotics") == 0) {
        if (strcmp(name, "cmd_vel") == 0) return "Command Velocity";
        if (strcmp(name, "drive_allowed") == 0) return "Drive Allowed";
        if (strcmp(name, "safety_stop") == 0) return "Safety Stop";
    } else if (strcmp(scenario, "quantum") == 0) {
        if (strcmp(name, "experiment_accept") == 0) return "Experiment Accept";
        if (strcmp(name, "gate_allowed") == 0) return "Gate Allowed";
        if (strcmp(name, "collapse_candidate") == 0) return "Collapse Candidate";
    } else if (strcmp(scenario, "blackhole") == 0) {
        if (strcmp(name, "inside_event_horizon") == 0) return "Inside Event Horizon";
        if (strcmp(name, "event_horizon_alert") == 0) return "Event Horizon Alert";
        if (strcmp(name, "observation_safe") == 0) return "Observation Safe";
        if (strcmp(name, "redshift_factor") == 0) return "Redshift Factor";
    } else if (strcmp(scenario, "reactor") == 0) {
        if (strcmp(name, "reactor_stable") == 0) return "Reactor Stable";
        if (strcmp(name, "scram_required") == 0) return "SCRAM Required";
        if (strcmp(name, "coolant_alert") == 0) return "Coolant Alert";
        if (strcmp(name, "thermal_margin") == 0) return "Thermal Margin";
    } else if (strcmp(scenario, "credit_policy") == 0) {
        if (strcmp(name, "policy_green") == 0) return "Policy Green";
        if (strcmp(name, "manual_review_required") == 0) return "Manual Review";
        if (strcmp(name, "decline_required") == 0) return "Decline Required";
        if (strcmp(name, "max_loan_amount") == 0) return "Max Loan Amount";
        if (strcmp(name, "approved_amount") == 0) return "Approved Amount";
        if (strcmp(name, "decision_code") == 0) return "Decision Code";
    }
    return NULL;
}

static bool demo_write_flow_node_host_json(FILE* out,
                                           const cxpr_eval_snapshot_flow* flow,
                                           size_t node_index,
                                           void* userdata) {
    const demo_host_context* host = (const demo_host_context*)userdata;
    const cxpr_eval_snapshot_flow_node* node;
    const char* scenario = host ? host->scenario_name : "";
    const char* output_label;

    if (!flow || node_index >= flow->node_count) return false;
    node = &flow->nodes[node_index];
    if (fputs("{ \"scenario\": ", out) == EOF) return false;
    if (!demo_json_string(out, scenario)) return false;
    if (fputs(", \"role\": ", out) == EOF) return false;
    if (!demo_json_string(out, strcmp(node->kind ? node->kind : "", "expression") == 0 ?
                              demo_expression_role(node->name) :
                              node->kind)) {
        return false;
    }
    if (fputs(", \"stable_id\": ", out) == EOF) return false;
    if (!demo_json_string(out, node->name)) return false;
    output_label = demo_output_label(scenario, node->name);
    if (output_label) {
        if (fputs(", \"output\": true, \"output_label\": ", out) == EOF) return false;
        if (!demo_json_string(out, output_label)) return false;
    }
    return fputs(" }", out) != EOF;
}

static bool demo_write_ast_node_host_json(FILE* out,
                                          const cxpr_eval_snapshot* snapshot,
                                          size_t node_index,
                                          void* userdata) {
    const demo_host_context* host = (const demo_host_context*)userdata;
    const cxpr_snapshot_node* node;

    if (!snapshot || node_index >= snapshot->node_count) return false;
    node = &snapshot->nodes[node_index];
    if (fputs("{ \"scenario\": ", out) == EOF) return false;
    if (!demo_json_string(out, host ? host->scenario_name : "")) return false;
    if (fputs(", \"ast_role\": ", out) == EOF) return false;
    if (!demo_json_string(out, node->role)) return false;
    if (fputs(", \"ast_kind\": ", out) == EOF) return false;
    if (!demo_json_string(out, node->kind)) return false;
    return fputs(" }", out) != EOF;
}

static double demo_rsi(const double* args, size_t argc, void* userdata) {
    (void)args;
    (void)argc;
    (void)userdata;
    return 42.0;
}

static const char* const demo_rsi_params[] = { "period" };
static const char* const demo_macd_fields[] = { "line", "signal", "histogram" };
static const char* const demo_macd_params[] = { "fast", "slow", "signal" };

typedef struct {
    const double* close;
    size_t count;
    size_t index;
} demo_macd_context;

static int demo_macd_period_arg(const double* args, size_t argc, size_t index, int fallback) {
    double raw;
    int period;

    if (!args || index >= argc) return fallback;
    raw = args[index];
    period = (int)(raw >= 0.0 ? raw + 0.5 : raw - 0.5);
    return period < 1 ? 1 : period;
}

static double demo_macd_ema_step(double prev, double x, int period) {
    double alpha = 2.0 / ((double)period + 1.0);
    return alpha * x + (1.0 - alpha) * prev;
}

static void demo_macd(const double* args,
                      size_t argc,
                      cxpr_value* out,
                      size_t field_count,
                      void* userdata) {
    const demo_macd_context* ctx = (const demo_macd_context*)userdata;
    int fast = demo_macd_period_arg(args, argc, 0u, 12);
    int slow = demo_macd_period_arg(args, argc, 1u, 26);
    int signal_period = demo_macd_period_arg(args, argc, 2u, 9);
    int swap_tmp;
    double ema_fast;
    double ema_slow;
    double signal = 0.0;
    double line = 0.0;
    double histogram = 0.0;
    size_t end;

    if (!out || field_count < 3u) return;
    if (fast > slow) {
        swap_tmp = fast;
        fast = slow;
        slow = swap_tmp;
    }
    if (!ctx || !ctx->close || ctx->count == 0u) {
        out[0] = cxpr_num(0.0);
        out[1] = cxpr_num(0.0);
        out[2] = cxpr_num(0.0);
        return;
    }

    end = ctx->index < ctx->count ? ctx->index : ctx->count - 1u;
    ema_fast = ctx->close[0];
    ema_slow = ctx->close[0];
    for (size_t i = 1u; i <= end; ++i) {
        ema_fast = demo_macd_ema_step(ema_fast, ctx->close[i], fast);
        ema_slow = demo_macd_ema_step(ema_slow, ctx->close[i], slow);
        line = ema_fast - ema_slow;
        signal = demo_macd_ema_step(signal, line, signal_period);
        histogram = line - signal;
    }

    out[0] = cxpr_num(line);
    out[1] = cxpr_num(signal);
    out[2] = cxpr_num(histogram);
}

static const double trading_close[] = {100.0, 101.2, 100.8, 103.4, 104.1};
static const double trading_volume[] = {900.0, 1200.0, 1600.0, 1300.0, 1800.0};
static const double trading_account_balance[] = {42000.0, 43600.0, 45100.0, 46800.0, 48250.0};
static const demo_macd_context trading_macd_context = {
    trading_close,
    sizeof(trading_close) / sizeof(trading_close[0]),
    4u,
};
static const cxpr_expression_def trading_expressions[] = {
        { "trend", "ema_fast > ema_slow" },
        { "breakout", "close > close[1]" },
        { "momentum_ok", "close - close[3] > 2" },
        { "macd_line", "macd(fast=12, slow=26, signal=9).line" },
        { "macd_signal", "macd(fast=12, slow=26, signal=9).signal" },
        { "macd_histogram", "macd(fast=12, slow=26, signal=9).histogram" },
        { "macd_bullish", "macd_line > macd_signal and macd_histogram > 0" },
        { "volume_ok", "volume > volume[1] and volume > $min_volume" },
        { "range_floor", "min(close, close[1], close[3])" },
        { "range_ceiling", "max(close, close[1], close[3])" },
        { "range_ok", "range_ceiling - range_floor > $min_range" },
        { "pullback", "falling(close, 2)" },
        { "regime_allowed", "contains(2, [1, 2, 3])" },
        { "field_access_demo", "account.balance > 45000" },
        { "chain_access_demo", "risk.model.score > 0.75" },
        { "array_literal_demo", "contains(3, [1, 2, 3])" },
        { "top_level_array_demo", "[1, [2, 3], volume_ok]" },
        { "complex_lookback_demo", "macd(fast=12, slow=26, signal=9).line[1]" },
        { "ternary_demo", "range_ok ? range_ceiling : range_floor" },
        { "pipe_score", "(close - close[3]) |> abs |> clamp(0, 3)" },
        { "pipe_ok", "pipe_score > $min_range" },
        { "entry", "trend and breakout and momentum_ok and macd_bullish and volume_ok and range_ok and regime_allowed and pipe_ok" },
        { "risk_block", "rsi(14) > $rsi_exit" },
        { "blocked_entry", "risk_block and volume_ok" },
        { "exit", "close < close[1] or rsi(14) > 70" },
};
static const cxpr_engine_column_source_def trading_columns[] = {
    { "close", &trading_close[0], sizeof(trading_close[0]), sizeof(trading_close) / sizeof(trading_close[0]) },
    { "volume", &trading_volume[0], sizeof(trading_volume[0]), sizeof(trading_volume) / sizeof(trading_volume[0]) },
    { "account.balance", &trading_account_balance[0], sizeof(trading_account_balance[0]), sizeof(trading_account_balance) / sizeof(trading_account_balance[0]) },
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

static const double orbital_semi_major_axis_au[] = {1.00, 1.52, 2.77, 5.20, 9.58};
static const double orbital_period_years[] = {1.00, 1.88, 4.61, 11.86, 29.46};
static const double orbital_radius_au[] = {1.00, 1.52, 2.77, 5.20, 9.58};
static const double orbital_speed_au_year[] = {6.28, 5.08, 3.77, 2.75, 2.04};
static const double orbital_central_mass_solar[] = {1.0, 1.0, 1.0, 1.0, 1.0};
static const cxpr_expression_def orbital_expressions[] = {
    { "kepler_period_sq", "square(orbital_period_years)" },
    { "kepler_axis_cubed", "semi_major_axis_au * semi_major_axis_au * semi_major_axis_au" },
    { "kepler_ratio", "kepler_period_sq / kepler_axis_cubed" },
    { "kepler_law_ok", "abs(kepler_ratio - 1) < $kepler_tolerance" },
    { "orbital_speed_expected", "$two_pi * semi_major_axis_au / orbital_period_years" },
    { "speed_match", "abs(orbital_speed_au_year - orbital_speed_expected) < $speed_tolerance" },
    { "newton_gravity", "$gravity_au3_solar_year2 * central_mass_solar / square(radius_au)" },
    { "centripetal_accel", "square(orbital_speed_au_year) / radius_au" },
    { "newton_law_ok", "abs(centripetal_accel - newton_gravity) < $accel_tolerance" },
    { "stable_orbit", "kepler_law_ok and speed_match and newton_law_ok" },
};
static const cxpr_engine_column_source_def orbital_columns[] = {
    { "semi_major_axis_au", &orbital_semi_major_axis_au[0], sizeof(orbital_semi_major_axis_au[0]), sizeof(orbital_semi_major_axis_au) / sizeof(orbital_semi_major_axis_au[0]) },
    { "orbital_period_years", &orbital_period_years[0], sizeof(orbital_period_years[0]), sizeof(orbital_period_years) / sizeof(orbital_period_years[0]) },
    { "radius_au", &orbital_radius_au[0], sizeof(orbital_radius_au[0]), sizeof(orbital_radius_au) / sizeof(orbital_radius_au[0]) },
    { "orbital_speed_au_year", &orbital_speed_au_year[0], sizeof(orbital_speed_au_year[0]), sizeof(orbital_speed_au_year) / sizeof(orbital_speed_au_year[0]) },
    { "central_mass_solar", &orbital_central_mass_solar[0], sizeof(orbital_central_mass_solar[0]), sizeof(orbital_central_mass_solar) / sizeof(orbital_central_mass_solar[0]) },
};

static const double apollo11_altitude_km[] = {110.0, 15.0, 2.5, 0.15, 0.003};
static const double apollo11_vertical_speed_m_s[] = {0.0, 45.0, 25.0, 5.0, 0.7};
static const double apollo11_horizontal_speed_m_s[] = {1600.0, 450.0, 75.0, 8.0, 0.2};
static const double apollo11_fuel_pct[] = {100.0, 62.0, 28.0, 9.0, 5.6};
static const double apollo11_guidance_error_m[] = {1200.0, 250.0, 80.0, 18.0, 1.5};
static const cxpr_expression_def apollo11_expressions[] = {
    { "speed_total_m_s", "sqrt(square(vertical_speed_m_s) + square(horizontal_speed_m_s))" },
    { "descent_profile_ok", "falling(altitude_km, 3) and altitude_km < altitude_km[1]" },
    { "vertical_rate_ok", "vertical_speed_m_s < $max_touchdown_vertical_speed_m_s" },
    { "horizontal_rate_ok", "horizontal_speed_m_s < $max_touchdown_horizontal_speed_m_s" },
    { "fuel_margin_ok", "fuel_pct > $min_touchdown_fuel_pct" },
    { "guidance_ok", "guidance_error_m < $max_touchdown_guidance_error_m" },
    { "near_surface", "altitude_km < $touchdown_altitude_km" },
    { "landing_energy_score", "(0.5 * $lunar_module_mass_kg * square(speed_total_m_s) / $energy_scale) |> clamp(0, 100)" },
    { "soft_landing_energy_ok", "landing_energy_score < $max_landing_energy_score" },
    { "touchdown_ok", "near_surface and vertical_rate_ok and horizontal_rate_ok and fuel_margin_ok and guidance_ok and soft_landing_energy_ok" },
};
static const cxpr_engine_column_source_def apollo11_columns[] = {
    { "altitude_km", &apollo11_altitude_km[0], sizeof(apollo11_altitude_km[0]), sizeof(apollo11_altitude_km) / sizeof(apollo11_altitude_km[0]) },
    { "vertical_speed_m_s", &apollo11_vertical_speed_m_s[0], sizeof(apollo11_vertical_speed_m_s[0]), sizeof(apollo11_vertical_speed_m_s) / sizeof(apollo11_vertical_speed_m_s[0]) },
    { "horizontal_speed_m_s", &apollo11_horizontal_speed_m_s[0], sizeof(apollo11_horizontal_speed_m_s[0]), sizeof(apollo11_horizontal_speed_m_s) / sizeof(apollo11_horizontal_speed_m_s[0]) },
    { "fuel_pct", &apollo11_fuel_pct[0], sizeof(apollo11_fuel_pct[0]), sizeof(apollo11_fuel_pct) / sizeof(apollo11_fuel_pct[0]) },
    { "guidance_error_m", &apollo11_guidance_error_m[0], sizeof(apollo11_guidance_error_m[0]), sizeof(apollo11_guidance_error_m) / sizeof(apollo11_guidance_error_m[0]) },
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

static const double blackhole_mass_solar[] = {8.2, 8.2, 8.2, 8.2, 8.2};
static const double blackhole_radial_distance_km[] = {54.0, 43.0, 35.5, 27.8, 22.4};
static const double blackhole_orbital_velocity_c[] = {0.18, 0.24, 0.31, 0.43, 0.58};
static const double blackhole_disk_temp_mk[] = {3.1, 4.8, 7.2, 10.5, 14.4};
static const double blackhole_lensing_angle_deg[] = {12.0, 21.0, 38.0, 64.0, 91.0};
static const cxpr_expression_def blackhole_expressions[] = {
    { "schwarzschild_radius_km", "$schwarzschild_per_solar_mass_km * mass_solar" },
    { "photon_sphere_radius_km", "1.5 * schwarzschild_radius_km" },
    { "horizon_margin_km", "radial_distance_km - schwarzschild_radius_km" },
    { "inside_event_horizon", "radial_distance_km <= schwarzschild_radius_km" },
    { "near_event_horizon", "radial_distance_km < photon_sphere_radius_km" },
    { "redshift_factor", "sqrt(abs(1 - schwarzschild_radius_km / radial_distance_km))" },
    { "strong_lensing", "lensing_angle_deg > $strong_lensing_deg" },
    { "relativistic_orbit", "orbital_velocity_c > $relativistic_velocity_c" },
    { "accretion_hot", "disk_temp_mk > $min_disk_temp_mk" },
    { "infall_accelerating", "falling(radial_distance_km, 3)" },
    { "tidal_stress_score", "(mass_solar / (radial_distance_km * radial_distance_km * radial_distance_km) * $tidal_scale) |> clamp(0, 100)" },
    { "event_horizon_alert", "near_event_horizon and infall_accelerating and relativistic_orbit" },
    { "observation_safe", "not inside_event_horizon and redshift_factor > $min_redshift_factor and not event_horizon_alert" },
};
static const cxpr_engine_column_source_def blackhole_columns[] = {
    { "mass_solar", &blackhole_mass_solar[0], sizeof(blackhole_mass_solar[0]), sizeof(blackhole_mass_solar) / sizeof(blackhole_mass_solar[0]) },
    { "radial_distance_km", &blackhole_radial_distance_km[0], sizeof(blackhole_radial_distance_km[0]), sizeof(blackhole_radial_distance_km) / sizeof(blackhole_radial_distance_km[0]) },
    { "orbital_velocity_c", &blackhole_orbital_velocity_c[0], sizeof(blackhole_orbital_velocity_c[0]), sizeof(blackhole_orbital_velocity_c) / sizeof(blackhole_orbital_velocity_c[0]) },
    { "disk_temp_mk", &blackhole_disk_temp_mk[0], sizeof(blackhole_disk_temp_mk[0]), sizeof(blackhole_disk_temp_mk) / sizeof(blackhole_disk_temp_mk[0]) },
    { "lensing_angle_deg", &blackhole_lensing_angle_deg[0], sizeof(blackhole_lensing_angle_deg[0]), sizeof(blackhole_lensing_angle_deg) / sizeof(blackhole_lensing_angle_deg[0]) },
};

static const double reactor_neutron_flux_pct[] = {82.0, 88.0, 94.0, 102.0, 111.0};
static const double reactor_core_temp_c[] = {286.0, 291.0, 298.0, 307.0, 318.0};
static const double reactor_coolant_flow_pct[] = {100.0, 98.0, 94.0, 88.0, 81.0};
static const double reactor_control_rod_depth_pct[] = {48.0, 46.0, 43.0, 38.0, 32.0};
static const double reactor_pressure_mpa[] = {15.2, 15.4, 15.8, 16.4, 17.1};
static const double reactor_boron_ppm[] = {920.0, 900.0, 875.0, 840.0, 810.0};
static const cxpr_expression_def reactor_expressions[] = {
    { "flux_delta", "neutron_flux_pct - neutron_flux_pct[1]" },
    { "flux_rising", "rising(neutron_flux_pct, 3)" },
    { "rod_withdrawal", "control_rod_depth_pct[1] - control_rod_depth_pct" },
    { "boron_dilution", "boron_ppm[1] - boron_ppm" },
    { "reactivity_score", "(flux_delta + rod_withdrawal * $rod_gain + boron_dilution * $boron_gain) |> clamp(0, 100)" },
    { "coolant_margin", "coolant_flow_pct - $min_coolant_flow_pct" },
    { "thermal_margin", "$max_core_temp_c - core_temp_c" },
    { "pressure_margin", "$max_pressure_mpa - pressure_mpa" },
    { "coolant_alert", "coolant_flow_pct < $min_coolant_flow_pct or falling(coolant_flow_pct, 3)" },
    { "temperature_alert", "core_temp_c > $max_core_temp_c or rising(core_temp_c, 3)" },
    { "pressure_alert", "pressure_mpa > $max_pressure_mpa" },
    { "reactivity_alert", "reactivity_score > $max_reactivity_score and flux_rising" },
    { "scram_required", "reactivity_alert or temperature_alert or pressure_alert or coolant_alert" },
    { "reactor_stable", "not scram_required and thermal_margin > $min_thermal_margin and pressure_margin > $min_pressure_margin" },
};
static const cxpr_engine_column_source_def reactor_columns[] = {
    { "neutron_flux_pct", &reactor_neutron_flux_pct[0], sizeof(reactor_neutron_flux_pct[0]), sizeof(reactor_neutron_flux_pct) / sizeof(reactor_neutron_flux_pct[0]) },
    { "core_temp_c", &reactor_core_temp_c[0], sizeof(reactor_core_temp_c[0]), sizeof(reactor_core_temp_c) / sizeof(reactor_core_temp_c[0]) },
    { "coolant_flow_pct", &reactor_coolant_flow_pct[0], sizeof(reactor_coolant_flow_pct[0]), sizeof(reactor_coolant_flow_pct) / sizeof(reactor_coolant_flow_pct[0]) },
    { "control_rod_depth_pct", &reactor_control_rod_depth_pct[0], sizeof(reactor_control_rod_depth_pct[0]), sizeof(reactor_control_rod_depth_pct) / sizeof(reactor_control_rod_depth_pct[0]) },
    { "pressure_mpa", &reactor_pressure_mpa[0], sizeof(reactor_pressure_mpa[0]), sizeof(reactor_pressure_mpa) / sizeof(reactor_pressure_mpa[0]) },
    { "boron_ppm", &reactor_boron_ppm[0], sizeof(reactor_boron_ppm[0]), sizeof(reactor_boron_ppm) / sizeof(reactor_boron_ppm[0]) },
};

static const double credit_requested_loan[] = {3600000.0, 3650000.0, 3700000.0, 3750000.0, 3800000.0};
static const double credit_estimate_value[] = {3950000.0, 3980000.0, 4010000.0, 4030000.0, 4050000.0};
static const double credit_customer_value[] = {4000000.0, 4020000.0, 4050000.0, 4070000.0, 4100000.0};
static const double credit_common_debt[] = {69675.0, 69675.0, 69675.0, 69675.0, 69675.0};
static const double credit_other_debt[] = {470000.0, 465000.0, 460000.0, 455000.0, 450000.0};
static const double credit_creditcard_limit[] = {85000.0, 80000.0, 75000.0, 70000.0, 65000.0};
static const double credit_creditcard_debt[] = {70000.0, 66000.0, 62000.0, 58000.0, 55000.0};
static const double credit_consumer_loan_debt[] = {120000.0, 112000.0, 104000.0, 94000.0, 85000.0};
static const double credit_income_main[] = {650000.0, 670000.0, 690000.0, 710000.0, 730000.0};
static const double credit_income_co[] = {510000.0, 518000.0, 525000.0, 530000.0, 535000.0};
static const double credit_verified_income_main[] = {640000.0, 660000.0, 680000.0, 700000.0, 710000.0};
static const double credit_verified_income_co[] = {505000.0, 512000.0, 520000.0, 525000.0, 525000.0};
static const double credit_monthly_tax[] = {27500.0, 28000.0, 28500.0, 29000.0, 29500.0};
static const double credit_tax_free_income[] = {0.0, 0.0, 0.0, 0.0, 0.0};
static const double credit_household_cost[] = {21000.0, 21200.0, 21400.0, 21600.0, 21800.0};
static const double credit_existing_monthly_debt[] = {6200.0, 6100.0, 6000.0, 5900.0, 5800.0};
static const double credit_existing_monthly_debt_buffered[] = {7600.0, 7500.0, 7400.0, 7300.0, 7200.0};
static const double credit_requested_monthly_debt[] = {12800.0, 13000.0, 13200.0, 13400.0, 13600.0};
static const double credit_requested_monthly_debt_buffered[] = {23600.0, 23900.0, 24200.0, 24500.0, 24800.0};
static const double credit_age_main[] = {35.0, 35.0, 35.0, 35.0, 35.0};
static const double credit_age_co[] = {33.0, 33.0, 33.0, 33.0, 33.0};
static const double credit_residency_code[] = {1.0, 1.0, 1.0, 1.0, 1.0};
static const double credit_citizenship_code[] = {100.0, 100.0, 100.0, 100.0, 100.0};
static const double credit_payment_remarks_count[] = {0.0, 0.0, 0.0, 0.0, 0.0};
static const double credit_area_market_score[] = {18.0, 18.0, 18.0, 18.0, 18.0};
static const double credit_municipality_score[] = {20.0, 20.0, 20.0, 20.0, 20.0};
static const cxpr_expression_def credit_policy_expressions[] = {
    { "markedsverdi", "estimate_value" },
    { "fellesgjeld", "common_debt" },
    { "verdi", "markedsverdi - fellesgjeld" },
    { "restlaan", "requested_loan" },
    { "ltv", "restlaan / verdi" },
    { "ltv_prosent", "round(ltv * 100)" },
    { "max_ltv", "min($max_ltv, $avvik_ltv)" },
    { "max_ltv_prosent", "max_ltv * 100" },
    { "ltv_max_laanebelop", "round(verdi * max_ltv)" },
    { "inntekt_soknad", "income_main + income_co" },
    { "inntekt_beregnet", "verified_income_main + verified_income_co" },
    { "inntekt_diff", "inntekt_soknad - inntekt_beregnet" },
    { "inntekt_avvik", "abs(inntekt_diff) / inntekt_soknad" },
    { "inntekt_avvik_prosent", "round(inntekt_avvik * 100)" },
    { "inntekt_diff_yellow", "inntekt_avvik_prosent > $max_inntekt_avvik_prosent" },
    { "totalgjeld", "requested_loan + other_debt + creditcard_debt + consumer_loan_debt" },
    { "totalgjeld_med_kredittkortramme", "requested_loan + other_debt + creditcard_limit + consumer_loan_debt" },
    { "sum_annen_gjeld", "totalgjeld - requested_loan" },
    { "egenkapital", "max(0, markedsverdi - requested_loan - other_debt)" },
    { "gjeldsgrad", "totalgjeld_med_kredittkortramme / inntekt_beregnet" },
    { "gjeldsgrad_yellow", "within(gjeldsgrad, $min_gjeldsgrad_avvik, $max_gjeldsgrad_avvik) and ltv < $max_ltv_avvik" },
    { "gjeldsgrad_red", "gjeldsgrad > $max_gjeldsgrad_avvik" },
    { "gjeldsgrad_green", "gjeldsgrad < $min_gjeldsgrad_avvik" },
    { "egenkapital_yellow", "egenkapital <= 0 and totalgjeld >= 0" },
    { "usikret_kreditt", "creditcard_debt + consumer_loan_debt" },
    { "usikret_prosent", "usikret_kreditt / inntekt_beregnet * 100" },
    { "usikret_green", "usikret_prosent < $max_usikret_kreditt * 100 and inntekt_beregnet > 0" },
    { "usikret_red", "inntekt_beregnet == 0 or usikret_prosent >= $max_usikret_kreditt * 100" },
    { "nettoinntekt", "inntekt_beregnet / 12 - monthly_tax" },
    { "disponibelt", "nettoinntekt + tax_free_income" },
    { "utgifter_maanedlig", "household_cost + existing_monthly_debt + requested_monthly_debt" },
    { "utgifter_med_rentepaslag", "household_cost + existing_monthly_debt_buffered + requested_monthly_debt_buffered" },
    { "utgifter_andre_laan_med_rentepaslag", "household_cost + existing_monthly_debt_buffered" },
    { "likviditet", "round(disponibelt - utgifter_maanedlig)" },
    { "likviditet_med_rentepaslag", "round(disponibelt - utgifter_med_rentepaslag)" },
    { "likviditet_med_rentepaslag_andre_laan", "round(disponibelt - utgifter_andre_laan_med_rentepaslag)" },
    { "likviditet_green", "likviditet_med_rentepaslag > 0" },
    { "likviditet_yellow", "likviditet > 0 and likviditet_med_rentepaslag < 0" },
    { "likviditet_red", "likviditet_med_rentepaslag < 0 and likviditet < 0" },
    { "ltv_green_liquidity_exception", "ltv > max_ltv and likviditet_med_rentepaslag >= $min_ltv_likviditet_avvik" },
    { "ltv_green", "ltv_green_liquidity_exception or ltv <= max_ltv" },
    { "ltv_yellow", "not ltv_green and within(ltv, 0.85, 1)" },
    { "ltv_red", "ltv > 1" },
    { "alder_green", "within(age_main, $min_age, $max_age) and within(age_co, $min_age, $max_age)" },
    { "alder_yellow", "age_main > $max_age or age_co > $max_age" },
    { "alder_red", "age_main < $min_age or age_co < $min_age" },
    { "bosatt_green", "residency_code == 1" },
    { "bosatt_red", "not bosatt_green" },
    { "statsborgerskap_green", "contains(citizenship_code, [0, 100, 101, 102, 103, 104, 105, 106, 111, 112, 113, 114, 115, 117, 118, 119, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 136, 137, 138, 139, 141, 144, 146, 148, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 196, 198, 199])" },
    { "statsborgerskap_yellow", "not statsborgerskap_green" },
    { "betalingsanmerkninger", "payment_remarks_count" },
    { "betalingsanmerkninger_green", "betalingsanmerkninger == 0" },
    { "betalingsanmerkninger_red", "betalingsanmerkninger > 0" },
    { "max_loan_inntekt", "inntekt_beregnet * $max_ganger_inntekt - sum_annen_gjeld" },
    { "max_loan_ltv", "round(verdi * max_ltv)" },
    { "max_loan_likviditet", "max(round(likviditet_med_rentepaslag_andre_laan / (($rente_nytt_boliglaan + $rentebuffer + $avdragsprosent) / 100 / $terminer)), 0)" },
    { "max_loan_min", "min(max_loan_inntekt, max_loan_ltv, max_loan_likviditet)" },
    { "max_loan_amount", "floor(max(0, max_loan_min) / 10000) * 10000" },
    { "max_loan_yellow", "max_loan_amount > 0 and max_loan_amount < requested_loan" },
    { "avvik_gjeldsgrad_yellow", "gjeldsgrad_yellow and inntekt_beregnet < 3000000 and likviditet_med_rentepaslag > 0 and ltv_prosent < 60 and age_main < 60" },
    { "avvik_ltv_yellow", "markedsverdi < 10000000 and area_market_score >= 17 and inntekt_beregnet < 3000000 and likviditet_med_rentepaslag > 10000 and ltv_prosent > 85 and age_main < 50" },
    { "manual_review_required", "ltv_yellow or gjeldsgrad_yellow or egenkapital_yellow or likviditet_yellow or inntekt_diff_yellow or max_loan_yellow or statsborgerskap_yellow or avvik_gjeldsgrad_yellow or avvik_ltv_yellow or alder_yellow" },
    { "decline_required", "ltv_red or gjeldsgrad_red or usikret_red or likviditet_red or alder_red or bosatt_red or betalingsanmerkninger_red or max_loan_amount < $min_loan_amount" },
    { "policy_green", "not decline_required and not manual_review_required and ltv_green and gjeldsgrad_green and usikret_green and likviditet_green and alder_green and bosatt_green and statsborgerskap_green and betalingsanmerkninger_green" },
    { "approved_amount", "policy_green ? requested_loan : max_loan_amount" },
    { "decision_code", "decline_required ? 0 : (manual_review_required ? 1 : 2)" },
};
static const cxpr_engine_column_source_def credit_policy_columns[] = {
    { "requested_loan", &credit_requested_loan[0], sizeof(credit_requested_loan[0]), sizeof(credit_requested_loan) / sizeof(credit_requested_loan[0]) },
    { "estimate_value", &credit_estimate_value[0], sizeof(credit_estimate_value[0]), sizeof(credit_estimate_value) / sizeof(credit_estimate_value[0]) },
    { "customer_value", &credit_customer_value[0], sizeof(credit_customer_value[0]), sizeof(credit_customer_value) / sizeof(credit_customer_value[0]) },
    { "common_debt", &credit_common_debt[0], sizeof(credit_common_debt[0]), sizeof(credit_common_debt) / sizeof(credit_common_debt[0]) },
    { "other_debt", &credit_other_debt[0], sizeof(credit_other_debt[0]), sizeof(credit_other_debt) / sizeof(credit_other_debt[0]) },
    { "creditcard_limit", &credit_creditcard_limit[0], sizeof(credit_creditcard_limit[0]), sizeof(credit_creditcard_limit) / sizeof(credit_creditcard_limit[0]) },
    { "creditcard_debt", &credit_creditcard_debt[0], sizeof(credit_creditcard_debt[0]), sizeof(credit_creditcard_debt) / sizeof(credit_creditcard_debt[0]) },
    { "consumer_loan_debt", &credit_consumer_loan_debt[0], sizeof(credit_consumer_loan_debt[0]), sizeof(credit_consumer_loan_debt) / sizeof(credit_consumer_loan_debt[0]) },
    { "income_main", &credit_income_main[0], sizeof(credit_income_main[0]), sizeof(credit_income_main) / sizeof(credit_income_main[0]) },
    { "income_co", &credit_income_co[0], sizeof(credit_income_co[0]), sizeof(credit_income_co) / sizeof(credit_income_co[0]) },
    { "verified_income_main", &credit_verified_income_main[0], sizeof(credit_verified_income_main[0]), sizeof(credit_verified_income_main) / sizeof(credit_verified_income_main[0]) },
    { "verified_income_co", &credit_verified_income_co[0], sizeof(credit_verified_income_co[0]), sizeof(credit_verified_income_co) / sizeof(credit_verified_income_co[0]) },
    { "monthly_tax", &credit_monthly_tax[0], sizeof(credit_monthly_tax[0]), sizeof(credit_monthly_tax) / sizeof(credit_monthly_tax[0]) },
    { "tax_free_income", &credit_tax_free_income[0], sizeof(credit_tax_free_income[0]), sizeof(credit_tax_free_income) / sizeof(credit_tax_free_income[0]) },
    { "household_cost", &credit_household_cost[0], sizeof(credit_household_cost[0]), sizeof(credit_household_cost) / sizeof(credit_household_cost[0]) },
    { "existing_monthly_debt", &credit_existing_monthly_debt[0], sizeof(credit_existing_monthly_debt[0]), sizeof(credit_existing_monthly_debt) / sizeof(credit_existing_monthly_debt[0]) },
    { "existing_monthly_debt_buffered", &credit_existing_monthly_debt_buffered[0], sizeof(credit_existing_monthly_debt_buffered[0]), sizeof(credit_existing_monthly_debt_buffered) / sizeof(credit_existing_monthly_debt_buffered[0]) },
    { "requested_monthly_debt", &credit_requested_monthly_debt[0], sizeof(credit_requested_monthly_debt[0]), sizeof(credit_requested_monthly_debt) / sizeof(credit_requested_monthly_debt[0]) },
    { "requested_monthly_debt_buffered", &credit_requested_monthly_debt_buffered[0], sizeof(credit_requested_monthly_debt_buffered[0]), sizeof(credit_requested_monthly_debt_buffered) / sizeof(credit_requested_monthly_debt_buffered[0]) },
    { "age_main", &credit_age_main[0], sizeof(credit_age_main[0]), sizeof(credit_age_main) / sizeof(credit_age_main[0]) },
    { "age_co", &credit_age_co[0], sizeof(credit_age_co[0]), sizeof(credit_age_co) / sizeof(credit_age_co[0]) },
    { "residency_code", &credit_residency_code[0], sizeof(credit_residency_code[0]), sizeof(credit_residency_code) / sizeof(credit_residency_code[0]) },
    { "citizenship_code", &credit_citizenship_code[0], sizeof(credit_citizenship_code[0]), sizeof(credit_citizenship_code) / sizeof(credit_citizenship_code[0]) },
    { "payment_remarks_count", &credit_payment_remarks_count[0], sizeof(credit_payment_remarks_count[0]), sizeof(credit_payment_remarks_count) / sizeof(credit_payment_remarks_count[0]) },
    { "area_market_score", &credit_area_market_score[0], sizeof(credit_area_market_score[0]), sizeof(credit_area_market_score) / sizeof(credit_area_market_score[0]) },
    { "municipality_score", &credit_municipality_score[0], sizeof(credit_municipality_score[0]), sizeof(credit_municipality_score) / sizeof(credit_municipality_score[0]) },
};

static void set_trading_context(cxpr_context* ctx) {
    const char* model_fields[] = { "score" };
    const cxpr_value model_values[] = { cxpr_num(0.82) };
    const char* risk_fields[] = { "model" };
    cxpr_struct_value* model;
    cxpr_value risk_values[1];
    cxpr_struct_value* risk;

    cxpr_context_set(ctx, "ema_fast", 104.5);
    cxpr_context_set(ctx, "ema_slow", 102.0);
    cxpr_context_set(ctx, "account.balance", 48250.0);
    cxpr_context_set_param(ctx, "min_volume", 1500.0);
    cxpr_context_set_param(ctx, "min_range", 2.0);
    cxpr_context_set_param(ctx, "rsi_exit", 70.0);

    model = cxpr_struct_value_new(model_fields, model_values, 1u);
    if (!model) return;
    risk_values[0] = cxpr_struct(model);
    risk = cxpr_struct_value_new(risk_fields, risk_values, 1u);
    if (risk) {
        cxpr_context_set_struct(ctx, "risk", risk);
        cxpr_struct_value_free(risk);
    }
    cxpr_struct_value_free(model);
}

static void set_physics_context(cxpr_context* ctx) {
    cxpr_context_set_param(ctx, "dt", 0.5);
    cxpr_context_set_param(ctx, "mass", 12.0);
    cxpr_context_set_param(ctx, "previous_velocity", 6.2);
    cxpr_context_set_param(ctx, "max_temperature", 30.0);
    cxpr_context_set_param(ctx, "max_force", 100.0);
    cxpr_context_set_param(ctx, "energy_cap", 500.0);
}

static void set_orbital_context(cxpr_context* ctx) {
    cxpr_context_set_param(ctx, "two_pi", 6.283185307179586);
    cxpr_context_set_param(ctx, "gravity_au3_solar_year2", 39.47841760435743);
    cxpr_context_set_param(ctx, "kepler_tolerance", 0.02);
    cxpr_context_set_param(ctx, "speed_tolerance", 0.08);
    cxpr_context_set_param(ctx, "accel_tolerance", 0.05);
}

static void set_apollo11_context(cxpr_context* ctx) {
    cxpr_context_set_param(ctx, "max_touchdown_vertical_speed_m_s", 1.0);
    cxpr_context_set_param(ctx, "max_touchdown_horizontal_speed_m_s", 1.0);
    cxpr_context_set_param(ctx, "min_touchdown_fuel_pct", 4.0);
    cxpr_context_set_param(ctx, "max_touchdown_guidance_error_m", 3.0);
    cxpr_context_set_param(ctx, "touchdown_altitude_km", 0.01);
    cxpr_context_set_param(ctx, "lunar_module_mass_kg", 5200.0);
    cxpr_context_set_param(ctx, "energy_scale", 1000000.0);
    cxpr_context_set_param(ctx, "max_landing_energy_score", 2.0);
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

static void set_blackhole_context(cxpr_context* ctx) {
    cxpr_context_set_param(ctx, "schwarzschild_per_solar_mass_km", 2.95);
    cxpr_context_set_param(ctx, "strong_lensing_deg", 45.0);
    cxpr_context_set_param(ctx, "relativistic_velocity_c", 0.35);
    cxpr_context_set_param(ctx, "min_disk_temp_mk", 8.0);
    cxpr_context_set_param(ctx, "tidal_scale", 100000.0);
    cxpr_context_set_param(ctx, "min_redshift_factor", 0.20);
}

static void set_reactor_context(cxpr_context* ctx) {
    cxpr_context_set_param(ctx, "rod_gain", 1.7);
    cxpr_context_set_param(ctx, "boron_gain", 0.08);
    cxpr_context_set_param(ctx, "min_coolant_flow_pct", 85.0);
    cxpr_context_set_param(ctx, "max_core_temp_c", 315.0);
    cxpr_context_set_param(ctx, "max_pressure_mpa", 16.8);
    cxpr_context_set_param(ctx, "max_reactivity_score", 18.0);
    cxpr_context_set_param(ctx, "min_thermal_margin", 8.0);
    cxpr_context_set_param(ctx, "min_pressure_margin", 0.2);
}

static void set_credit_policy_context(cxpr_context* ctx) {
    cxpr_context_set_param(ctx, "min_age", 18.0);
    cxpr_context_set_param(ctx, "max_age", 75.0);
    cxpr_context_set_param(ctx, "max_inntekt_avvik_prosent", 5.0);
    cxpr_context_set_param(ctx, "max_ltv", 0.85);
    cxpr_context_set_param(ctx, "avvik_ltv", 0.85);
    cxpr_context_set_param(ctx, "max_ltv_avvik", 0.60);
    cxpr_context_set_param(ctx, "max_ltv_flex", 0.60);
    cxpr_context_set_param(ctx, "min_ltv_likviditet_avvik", 10000.0);
    cxpr_context_set_param(ctx, "min_gjeldsgrad_avvik", 5.0);
    cxpr_context_set_param(ctx, "max_gjeldsgrad_avvik", 8.0);
    cxpr_context_set_param(ctx, "max_usikret_kreditt", 0.30);
    cxpr_context_set_param(ctx, "max_ganger_inntekt", 5.0);
    cxpr_context_set_param(ctx, "rente_nytt_boliglaan", 1.8);
    cxpr_context_set_param(ctx, "rentebuffer", 5.0);
    cxpr_context_set_param(ctx, "avdragsprosent", 2.78);
    cxpr_context_set_param(ctx, "terminer", 12.0);
    cxpr_context_set_param(ctx, "min_loan_amount", 50000.0);
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
        "orbital",
        orbital_expressions,
        sizeof(orbital_expressions) / sizeof(orbital_expressions[0]),
        orbital_columns,
        sizeof(orbital_columns) / sizeof(orbital_columns[0]),
        4,
        set_orbital_context,
    },
    {
        "apollo11",
        apollo11_expressions,
        sizeof(apollo11_expressions) / sizeof(apollo11_expressions[0]),
        apollo11_columns,
        sizeof(apollo11_columns) / sizeof(apollo11_columns[0]),
        4,
        set_apollo11_context,
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
    {
        "blackhole",
        blackhole_expressions,
        sizeof(blackhole_expressions) / sizeof(blackhole_expressions[0]),
        blackhole_columns,
        sizeof(blackhole_columns) / sizeof(blackhole_columns[0]),
        4,
        set_blackhole_context,
    },
    {
        "reactor",
        reactor_expressions,
        sizeof(reactor_expressions) / sizeof(reactor_expressions[0]),
        reactor_columns,
        sizeof(reactor_columns) / sizeof(reactor_columns[0]),
        4,
        set_reactor_context,
    },
    {
        "credit_policy",
        credit_policy_expressions,
        sizeof(credit_policy_expressions) / sizeof(credit_policy_expressions[0]),
        credit_policy_columns,
        sizeof(credit_policy_columns) / sizeof(credit_policy_columns[0]),
        4,
        set_credit_policy_context,
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
    int host_demo = 0;
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
        if (strcmp(argv[argi], "--host-demo") == 0) {
            host_demo = 1;
        } else if (is_scenario_name(argv[argi])) {
            scenario_name = argv[argi];
        } else if (!out_path) {
            out_path = argv[argi];
        } else {
            fprintf(stderr,
                    "usage: %s [--host-demo] [output.json] [trading|physics|orbital|apollo11|chemistry|robotics|quantum|blackhole|reactor|credit_policy]\n",
                    argv[0]);
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
    if (!cxpr_registry_set_param_names(reg, "rsi", demo_rsi_params, 1u)) {
        fprintf(stderr, "failed to set rsi param names\n");
        goto done;
    }
    cxpr_registry_add_struct(reg, "macd", demo_macd, 3, 3,
                             demo_macd_fields, 3u, (void*)&trading_macd_context, NULL);
    if (!cxpr_registry_set_param_names(reg, "macd", demo_macd_params, 3u)) {
        fprintf(stderr, "failed to set macd param names\n");
        goto done;
    }
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

    if (host_demo) {
        demo_host_context host_ctx = { scenario_name };
        cxpr_snapshot_json_hooks hooks = {
            "cxpr-example",
            "cxpr.example.host_snapshot.v1",
            demo_write_flow_node_host_json,
            demo_write_ast_node_host_json,
            &host_ctx,
        };
        if (!cxpr_eval_snapshot_flow_write_json_ex(&flow, &hooks, out)) {
            fprintf(stderr, "failed to write host snapshot JSON\n");
            goto cleanup_snapshot;
        }
    } else if (!cxpr_eval_snapshot_flow_write_json(&flow, out)) {
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
