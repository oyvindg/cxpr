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
    double next_phi[CELL_COUNT];
    double next_momentum[CELL_COUNT];
    double energy_density[CELL_COUNT];
    cxpr_bulk_const_column inputs[4];
    cxpr_bulk_column outputs[3];
    double params[CXPR_GENERATED_MODEL_MAX_PARAMS] = {0};
    void* states = NULL;
    size_t state_size;
    size_t i;

    assert(cxpr_generated_model_descriptor_abi_valid(descriptor));
    assert(descriptor->input_count == 4u);
    assert(descriptor->output_count == 3u);
    assert(descriptor->param_count == 3u);

    for (i = 0u; i < CELL_COUNT; ++i) {
        const double x = (double)i / (double)(CELL_COUNT - 1u);
        phi[i] = sin(6.0 * x) + 0.2 * cos(17.0 * x);
        momentum[i] = 0.3 * cos(4.0 * x);
    }
    for (i = 0u; i < CELL_COUNT; ++i) {
        /* Periodic topology is a host concern, not part of the CXPR model. */
        phi_left[i] = phi[(i + CELL_COUNT - 1u) % CELL_COUNT];
        phi_right[i] = phi[(i + 1u) % CELL_COUNT];
    }

    inputs[named_index(descriptor->input_names, descriptor->input_count, "phi")] =
        (cxpr_bulk_const_column){phi, 1u};
    inputs[named_index(descriptor->input_names, descriptor->input_count, "momentum")] =
        (cxpr_bulk_const_column){momentum, 1u};
    inputs[named_index(descriptor->input_names, descriptor->input_count, "phi_left")] =
        (cxpr_bulk_const_column){phi_left, 1u};
    inputs[named_index(descriptor->input_names, descriptor->input_count, "phi_right")] =
        (cxpr_bulk_const_column){phi_right, 1u};
    outputs[named_index(descriptor->output_names, descriptor->output_count, "next_phi")] =
        (cxpr_bulk_column){next_phi, 1u};
    outputs[named_index(descriptor->output_names, descriptor->output_count, "next_momentum")] =
        (cxpr_bulk_column){next_momentum, 1u};
    outputs[named_index(descriptor->output_names, descriptor->output_count, "energy_density")] =
        (cxpr_bulk_column){energy_density, 1u};

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
        const double dt = params[named_index(descriptor->param_names, descriptor->param_count, "dt")];
        const double dx = params[named_index(descriptor->param_names, descriptor->param_count, "dx")];
        const double mass = params[named_index(descriptor->param_names, descriptor->param_count, "mass")];
        for (i = 0u; i < CELL_COUNT; ++i) {
            const double laplacian =
                (phi_left[i] - 2.0 * phi[i] + phi_right[i]) / (dx * dx);
            const double expected_momentum =
                momentum[i] + dt * (laplacian - mass * mass * phi[i]);
            const double expected_phi = phi[i] + dt * expected_momentum;
            const double expected_energy =
                0.5 * expected_momentum * expected_momentum +
                0.5 * mass * mass * expected_phi * expected_phi;
            assert_close(next_phi[i], expected_phi);
            assert_close(next_momentum[i], expected_momentum);
            assert_close(energy_density[i], expected_energy);
        }
    }

    free(states);
    puts("bulk generated-C E2E: 257 Klein-Gordon cells match host reference");
    return 0;
}
