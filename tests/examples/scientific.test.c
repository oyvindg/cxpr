/**
 * @file scientific.test.c
 * @brief Verifies the cross-domain scientific computing example.
 *
 * Demonstrates the named-expression evaluator resolving an interdependent set
 * of physics/chemistry formulas in automatic topological order, spanning
 * general relativity, special relativity, quantum mechanics, and chemical
 * kinetics in a single evaluation pass.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cxpr/cxpr.h>

#define APPROX(a, b) (fabs((a) - (b)) <= 1e-9 * fabs(b) + 1e-12)

int main(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    /* Physical constants (SI). */
    const double G = 6.67430e-11;       /* gravitational constant */
    const double c = 299792458.0;       /* speed of light */
    const double M = 8.54e36;           /* mass of Sagittarius A* (kg) */
    const double v = 0.6 * c;           /* test particle speed */
    const double hbar = 1.054571817e-34;
    const double omega = 1.0e15;        /* oscillator angular frequency */
    const double n = 2.0;               /* quantum number */
    const double A = 1.0e13;            /* Arrhenius pre-exponential factor */
    const double Ea = 75000.0;          /* activation energy (J/mol) */
    const double R = 8.314462618;       /* gas constant */
    const double T = 298.15;            /* temperature (K) */

    cxpr_register_defaults(reg);

    cxpr_context_set_array(ctx, (cxpr_context_entry[]) {
        {"G", G}, {"c", c}, {"M", M}, {"v", v},
        {"hbar", hbar}, {"omega", omega}, {"n", n},
        {"A", A}, {"Ea", Ea}, {"R", R}, {"T", T},
        {NULL, 0.0}
    });
    cxpr_context_set_param(ctx, "r_over_rs", 4.0);

    /* Listed out of dependency order on purpose: time_dilation needs
     * orbit_radius, which needs schwarzschild_radius. The evaluator sorts it. */
    const cxpr_expression_def defs[] = {
        { "time_dilation",        "sqrt(1 - schwarzschild_radius / orbit_radius)" },
        { "orbit_radius",         "$r_over_rs * schwarzschild_radius" },
        { "schwarzschild_radius", "2 * G * M / c^2" },
        { "outside_horizon",      "orbit_radius > schwarzschild_radius" },
        { "lorentz_factor",       "1 / sqrt(1 - (v / c)^2)" },
        { "oscillator_energy",    "hbar * omega * (n + 0.5)" },
        { "reaction_rate",        "A * exp(-Ea / (R * T))" },
    };

    assert(cxpr_expressions_add(evaluator, defs, 7, &err));
    assert(cxpr_evaluator_compile(evaluator, &err));
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);

    /* General relativity: Schwarzschild radius and gravitational time dilation. */
    const double rs = 2.0 * G * M / (c * c);
    assert(APPROX(cxpr_expression_get_double(evaluator, "schwarzschild_radius", NULL), rs));
    assert(APPROX(cxpr_expression_get_double(evaluator, "orbit_radius", NULL), 4.0 * rs));
    /* r = 4 r_s  ->  sqrt(1 - 1/4) = sqrt(0.75), independent of the mass. */
    assert(APPROX(cxpr_expression_get_double(evaluator, "time_dilation", NULL), sqrt(0.75)));
    assert(cxpr_expression_get_bool(evaluator, "outside_horizon", NULL) == true);

    /* Special relativity: Lorentz factor at v = 0.6c is exactly 1.25. */
    assert(APPROX(cxpr_expression_get_double(evaluator, "lorentz_factor", NULL), 1.25));

    /* Quantum mechanics: harmonic-oscillator energy level E_n. */
    assert(APPROX(cxpr_expression_get_double(evaluator, "oscillator_energy", NULL),
                  hbar * omega * (n + 0.5)));

    /* Chemistry: Arrhenius reaction rate. */
    assert(APPROX(cxpr_expression_get_double(evaluator, "reaction_rate", NULL),
                  A * exp(-Ea / (R * T))));

    /* The dependency chain was resolved before its dependents. */
    const char* order[8];
    size_t count = cxpr_expression_eval_order(evaluator, order, 8);
    assert(count == 7);
    size_t i_rs = count, i_orbit = count, i_dil = count;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(order[i], "schwarzschild_radius") == 0) i_rs = i;
        else if (strcmp(order[i], "orbit_radius") == 0) i_orbit = i;
        else if (strcmp(order[i], "time_dilation") == 0) i_dil = i;
    }
    assert(i_rs < i_orbit && i_orbit < i_dil);

    cxpr_context_free(ctx);
    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);

    printf("  \342\234\223 scientific example\n");
    return 0;
}
