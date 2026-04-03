/**
 * @file physics_simulation.test.c
 * @brief End-to-end physics simulation tests for cxpr.
 *
 * Simulates a damped driven spring-mass system over many timesteps using the
 * public FormulaEngine API. The formulas are compiled once and then evaluated
 * repeatedly as the simulation state is updated, which exercises parsing,
 * dependency resolution, compiled IR execution, built-in math functions, and
 * context updates together.
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define EPSILON 1e-10
#define ASSERT_APPROX(a, b, eps) assert(fabs((a) - (b)) < (eps))

typedef struct {
    double x;
    double v;
    double t;
} OscillatorState;

typedef struct {
    double restoring_force;
    double damping_force;
    double drive_force;
    double acceleration;
    double next_v;
    double next_x;
    double energy;
} OscillatorStep;

static OscillatorStep reference_step(const OscillatorState* state,
                                     double mass,
                                     double stiffness,
                                     double damping,
                                     double drive_amplitude,
                                     double drive_frequency,
                                     double dt) {
    OscillatorStep step;

    step.restoring_force = -stiffness * state->x;
    step.damping_force = -damping * state->v;
    step.drive_force = drive_amplitude * sin(drive_frequency * state->t);
    step.acceleration =
        (step.restoring_force + step.damping_force + step.drive_force) / mass;
    step.next_v = state->v + step.acceleration * dt;
    step.next_x = state->x + step.next_v * dt;
    step.energy =
        0.5 * mass * step.next_v * step.next_v + 0.5 * stiffness * step.next_x * step.next_x;

    return step;
}

static void test_formula_engine_damped_oscillator(void) {
    const size_t steps = 1500;
    const double mass = 2.0;
    const double stiffness = 9.0;
    const double damping = 0.8;
    const double drive_amplitude = 1.25;
    const double drive_frequency = 1.1;
    const double dt = 0.01;
    const double max_abs_x = 2.0;
    const double max_abs_v = 5.0;

    cxpr_registry* reg = cxpr_registry_new();
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    OscillatorState state = {0.75, -0.2, 0.0};
    OscillatorStep expected = {0};

    assert(reg);
    assert(engine);
    assert(ctx);

    cxpr_register_builtins(reg);

    cxpr_context_set_param(ctx, "mass", mass);
    cxpr_context_set_param(ctx, "stiffness", stiffness);
    cxpr_context_set_param(ctx, "damping", damping);
    cxpr_context_set_param(ctx, "drive_amplitude", drive_amplitude);
    cxpr_context_set_param(ctx, "drive_frequency", drive_frequency);
    cxpr_context_set_param(ctx, "dt", dt);
    cxpr_context_set_param(ctx, "max_abs_x", max_abs_x);
    cxpr_context_set_param(ctx, "max_abs_v", max_abs_v);

    assert(cxpr_formula_add(engine, "restoring_force", "-$stiffness * x", &err));
    assert(cxpr_formula_add(engine, "damping_force", "-$damping * v", &err));
    assert(cxpr_formula_add(engine, "drive_force",
                            "$drive_amplitude * sin($drive_frequency * t)", &err));
    assert(cxpr_formula_add(engine, "acceleration",
                            "(restoring_force + damping_force + drive_force) / $mass", &err));
    assert(cxpr_formula_add(engine, "next_v", "v + acceleration * $dt", &err));
    assert(cxpr_formula_add(engine, "next_x", "x + next_v * $dt", &err));
    assert(cxpr_formula_add(engine, "energy",
                            "0.5 * $mass * next_v * next_v + 0.5 * $stiffness * next_x * next_x",
                            &err));
    assert(cxpr_formula_add(engine, "stable",
                            "abs(next_x) <= $max_abs_x and abs(next_v) <= $max_abs_v", &err));

    {
        const bool ok = cxpr_formula_compile(engine, &err);
        if (!ok) {
            fprintf(stderr, "formula compile failed: code=%d message=%s\n",
                    (int)err.code, err.message ? err.message : "(null)");
        }
        assert(ok);
        assert(err.code == CXPR_OK);
    }

    for (size_t i = 0; i < steps; ++i) {
        bool found = false;
        const double expected_time = (double)i * dt;

        cxpr_context_set(ctx, "x", state.x);
        cxpr_context_set(ctx, "v", state.v);
        cxpr_context_set(ctx, "t", state.t);

        cxpr_formula_eval_all(engine, ctx, &err);
        if (err.code != CXPR_OK) {
            fprintf(stderr, "formula eval failed at step %zu: code=%d message=%s\n",
                    i, (int)err.code, err.message ? err.message : "(null)");
        }
        assert(err.code == CXPR_OK);

        expected = reference_step(&state, mass, stiffness, damping,
                                  drive_amplitude, drive_frequency, dt);

        ASSERT_APPROX(state.t, expected_time, 1e-12);
        ASSERT_APPROX(cxpr_formula_get_double(engine, "restoring_force", &found),
                      expected.restoring_force, 1e-12);
        assert(found);
        ASSERT_APPROX(cxpr_formula_get_double(engine, "damping_force", &found),
                      expected.damping_force, 1e-12);
        assert(found);
        ASSERT_APPROX(cxpr_formula_get_double(engine, "drive_force", &found),
                      expected.drive_force, 1e-12);
        assert(found);
        ASSERT_APPROX(cxpr_formula_get_double(engine, "acceleration", &found),
                      expected.acceleration, 1e-12);
        assert(found);
        ASSERT_APPROX(cxpr_formula_get_double(engine, "next_v", &found), expected.next_v, EPSILON);
        assert(found);
        ASSERT_APPROX(cxpr_formula_get_double(engine, "next_x", &found), expected.next_x, EPSILON);
        assert(found);
        ASSERT_APPROX(cxpr_formula_get_double(engine, "energy", &found), expected.energy, EPSILON);
        assert(found);
        assert(cxpr_formula_get_bool(engine, "stable", &found) == true);
        assert(found);

        assert(isfinite(expected.energy));
        assert(fabs(expected.next_x) <= max_abs_x);
        assert(fabs(expected.next_v) <= max_abs_v);

        state.x = expected.next_x;
        state.v = expected.next_v;
        state.t += dt;
    }

    ASSERT_APPROX(state.x, -0.080347329774910, 1e-12);
    ASSERT_APPROX(state.v, -0.206717044720150, 1e-12);
    ASSERT_APPROX(state.t, (double)steps * dt, 1e-12);

    cxpr_formula_engine_free(engine);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_engine_damped_oscillator\n");
}

int main(void) {
    printf("Running physics simulation tests...\n");
    test_formula_engine_damped_oscillator();
    printf("All physics simulation tests passed!\n");
    return 0;
}
