/** End-to-end generated-C bulk parity for a host-owned one-dimensional grid. */

#include <cxpr/bulk.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "klein_gordon_cell.gen.c"

enum { CELL_COUNT = 257 };

static void assert_close(double actual, double expected) {
    const double scale = fmax(1.0, fmax(fabs(actual), fabs(expected)));
    assert(isfinite(actual));
    assert(isfinite(expected));
    assert(fabs(actual - expected) <= 1e-12 * scale);
}

static size_t named_index(const char* const* names, size_t count, const char* name) {
    size_t i;
    for (i = 0u; i < count; ++i)
        if (strcmp(names[i], name) == 0) return i;
    assert(!"generated descriptor is missing an expected name");
    return 0u;
}

int main(void) {
    const cxpr_generated_model_descriptor* descriptor =
        &klein_gordon_cell_tick_descriptor;
    double phi[CELL_COUNT];
    double momentum[CELL_COUNT];
    double phi_left[CELL_COUNT];
    double phi_right[CELL_COUNT];
    double source[CELL_COUNT];
    double next_phi[CELL_COUNT];
    double next_momentum[CELL_COUNT];
    double acceleration[CELL_COUNT];
    double energy_density[CELL_COUNT];
    cxpr_value input_values[5][CELL_COUNT];
    cxpr_value output_values[4][CELL_COUNT] = {{{0}}};
    cxpr_bulk_const_column inputs[5];
    cxpr_bulk_column outputs[4];
    cxpr_value params[CXPR_GENERATED_MODEL_MAX_PARAMS] = {{0}};
    void* states = NULL;
    size_t state_size;
    size_t i;

    assert(cxpr_generated_model_descriptor_abi_valid(descriptor));
    assert(descriptor->input_count == 5u);
    assert(descriptor->output_count == 4u);
    assert(descriptor->param_count == 5u);

    for (i = 0u; i < CELL_COUNT; ++i) {
        const double x = (double)i / (double)(CELL_COUNT - 1u);
        phi[i] = sin(6.0 * x) + 0.2 * cos(17.0 * x);
        momentum[i] = 0.3 * cos(4.0 * x);
        source[i] = 0.05 * sin(3.0 * x);
    }
    for (i = 0u; i < CELL_COUNT; ++i) {
        /* Periodic topology is a host concern, not part of the CXPR model. */
        phi_left[i] = phi[(i + CELL_COUNT - 1u) % CELL_COUNT];
        phi_right[i] = phi[(i + 1u) % CELL_COUNT];
        input_values[0][i] = cxpr_num(phi[i]);
        input_values[1][i] = cxpr_num(momentum[i]);
        input_values[2][i] = cxpr_num(phi_left[i]);
        input_values[3][i] = cxpr_num(phi_right[i]);
        input_values[4][i] = cxpr_num(source[i]);
    }

    inputs[named_index(descriptor->input_names, descriptor->input_count, "phi")] =
        (cxpr_bulk_const_column){input_values[0], 1u};
    inputs[named_index(descriptor->input_names, descriptor->input_count, "momentum")] =
        (cxpr_bulk_const_column){input_values[1], 1u};
    inputs[named_index(descriptor->input_names, descriptor->input_count, "phi_left")] =
        (cxpr_bulk_const_column){input_values[2], 1u};
    inputs[named_index(descriptor->input_names, descriptor->input_count, "phi_right")] =
        (cxpr_bulk_const_column){input_values[3], 1u};
    inputs[named_index(descriptor->input_names, descriptor->input_count, "source")] =
        (cxpr_bulk_const_column){input_values[4], 1u};
    outputs[named_index(descriptor->output_names, descriptor->output_count, "next_phi")] =
        (cxpr_bulk_column){output_values[0], 1u};
    outputs[named_index(descriptor->output_names, descriptor->output_count, "next_momentum")] =
        (cxpr_bulk_column){output_values[1], 1u};
    outputs[named_index(descriptor->output_names, descriptor->output_count, "acceleration")] =
        (cxpr_bulk_column){output_values[2], 1u};
    outputs[named_index(descriptor->output_names, descriptor->output_count, "energy_density")] =
        (cxpr_bulk_column){output_values[3], 1u};

    for (i = 0u; i < descriptor->param_count; ++i) {
        assert(descriptor->param_has_default[i]);
        params[i] = descriptor->param_defaults[i];
    }
    state_size = descriptor->state_size();
    if (state_size > 0u) {
        states = calloc(CELL_COUNT, state_size);
        assert(states);
    }

    {
        const cxpr_bulk_view view = {
            .inputs = inputs,
            .input_count = descriptor->input_count,
            .params = params,
            .param_count = descriptor->param_count,
            .outputs = outputs,
            .output_count = descriptor->output_count,
            .states = states,
            .state_stride = state_size,
            .element_count = CELL_COUNT,
        };
        assert(cxpr_bulk_run(descriptor, &view) == CXPR_BULK_OK);
    }

    {
        const double dt = params[named_index(descriptor->param_names, descriptor->param_count, "dt")].d;
        const double dx = params[named_index(descriptor->param_names, descriptor->param_count, "dx")].d;
        const double mass = params[named_index(descriptor->param_names, descriptor->param_count, "mass")].d;
        const double lambda = params[named_index(descriptor->param_names, descriptor->param_count, "lambda")].d;
        const double damping = params[named_index(descriptor->param_names, descriptor->param_count, "damping")].d;
        for (i = 0u; i < CELL_COUNT; ++i) {
            const double laplacian =
                (phi_left[i] - 2.0 * phi[i] + phi_right[i]) / (dx * dx);
            const double expected_acceleration = laplacian - mass * mass * phi[i]
                - lambda * phi[i] * phi[i] * phi[i]
                - damping * momentum[i] + source[i];
            const double expected_momentum = momentum[i] + dt * expected_acceleration;
            const double expected_phi = phi[i] + dt * expected_momentum;
            const double gradient = (phi_right[i] - phi_left[i]) / (2.0 * dx);
            const double expected_energy = 0.5 * momentum[i] * momentum[i]
                + 0.5 * gradient * gradient + 0.5 * mass * mass * phi[i] * phi[i]
                + 0.25 * lambda * phi[i] * phi[i] * phi[i] * phi[i];
            assert_close(output_values[named_index(descriptor->output_names, descriptor->output_count, "next_phi")][i].d, expected_phi);
            assert_close(output_values[named_index(descriptor->output_names, descriptor->output_count, "next_momentum")][i].d, expected_momentum);
            assert_close(output_values[named_index(descriptor->output_names, descriptor->output_count, "acceleration")][i].d, expected_acceleration);
            assert_close(output_values[named_index(descriptor->output_names, descriptor->output_count, "energy_density")][i].d, expected_energy);
        }
    }

    free(states);
    puts("bulk generated-C E2E: 257 Klein-Gordon cells match host reference");
    return 0;
}
