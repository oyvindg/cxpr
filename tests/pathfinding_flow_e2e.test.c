/** Full host-driven flow-field loop against an independent Dijkstra reference. */

#include <cxpr/bulk.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pathfinding_grid4.gen.c"
#include "pathfinding_grid8.gen.c"

enum { WIDTH = 9, HEIGHT = 7, CELLS = WIDTH * HEIGHT };
static const double INF_SENTINEL = 1e18;

static size_t named_index(const char* const* names, size_t count, const char* name) {
    size_t i;
    for (i = 0u; i < count; ++i)
        if (strcmp(names[i], name) == 0) return i;
    assert(!"generated descriptor is missing an expected name");
    return 0u;
}

static int inside(int x, int y) {
    return x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT;
}

static double neighbour(const double* dist, const int* wall, int x, int y) {
    const int index = y * WIDTH + x;
    return inside(x, y) && !wall[index] ? dist[index] : INF_SENTINEL;
}

static void dijkstra(const int* wall, int diagonal, double* result) {
    int used[CELLS] = {0};
    size_t step;
    const int goal = WIDTH - 1;
    const int dx[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    const int dy[8] = {-1, 1, 0, 0, -1, -1, 1, 1};
    const double weight[8] = {1, 1, 1, 1, 1.5, 1.5, 1.5, 1.5};

    for (step = 0u; step < CELLS; ++step) result[step] = INF_SENTINEL;
    result[goal] = 0.0;
    for (step = 0u; step < CELLS; ++step) {
        int current = -1;
        int direction;
        size_t i;
        for (i = 0u; i < CELLS; ++i)
            if (!used[i] && !wall[i] &&
                (current < 0 || result[i] < result[current])) current = (int)i;
        if (current < 0 || result[current] >= INF_SENTINEL) break;
        used[current] = 1;
        for (direction = 0; direction < (diagonal ? 8 : 4); ++direction) {
            const int nx = current % WIDTH + dx[direction];
            const int ny = current / WIDTH + dy[direction];
            const int next = ny * WIDTH + nx;
            double candidate;
            if (!inside(nx, ny) || wall[next]) continue;
            candidate = result[current] + weight[direction];
            if (candidate < result[next]) result[next] = candidate;
        }
    }
}

static void set_input(cxpr_value values[CXPR_GENERATED_MODEL_MAX_INPUTS][CELLS],
                      const cxpr_generated_model_descriptor* descriptor,
                      size_t cell, const char* name, double value) {
    values[named_index(descriptor->input_names, descriptor->input_count, name)][cell] =
        cxpr_num(value);
}

static void run_case(const cxpr_generated_model_descriptor* descriptor, int diagonal) {
    int wall[CELLS] = {0};
    double first[CELLS];
    double second[CELLS];
    double reference[CELLS];
    double* current = first;
    double* next = second;
    cxpr_value input_values[CXPR_GENERATED_MODEL_MAX_INPUTS][CELLS] = {{{0}}};
    cxpr_value output_values[CXPR_GENERATED_MODEL_MAX_OUTPUTS][CELLS] = {{{0}}};
    cxpr_bulk_const_column inputs[CXPR_GENERATED_MODEL_MAX_INPUTS] = {{0}};
    cxpr_bulk_column outputs[CXPR_GENERATED_MODEL_MAX_OUTPUTS] = {{0}};
    cxpr_value params[CXPR_GENERATED_MODEL_MAX_PARAMS] = {{0}};
    void* states;
    size_t state_size = descriptor->state_size();
    size_t dist_output = named_index(descriptor->output_names,
                                     descriptor->output_count, "next_dist");
    size_t dir_output = named_index(descriptor->output_names,
                                    descriptor->output_count, "dir");
    size_t i;
    int iteration;

    /* A barrier with two gaps exercises detours and stable direction ties. */
    for (i = 1u; i + 1u < HEIGHT; ++i) wall[i * WIDTH + 4u] = 1;
    wall[2u * WIDTH + 4u] = 0;
    wall[5u * WIDTH + 4u] = 0;
    for (i = 0u; i < CELLS; ++i) first[i] = INF_SENTINEL;
    first[WIDTH - 1] = 0.0;

    for (i = 0u; i < descriptor->input_count; ++i)
        inputs[i] = (cxpr_bulk_const_column){input_values[i], 1u};
    for (i = 0u; i < descriptor->output_count; ++i)
        outputs[i] = (cxpr_bulk_column){output_values[i], 1u};
    for (i = 0u; i < descriptor->param_count; ++i)
        params[i] = descriptor->param_defaults[i];
    states = calloc(CELLS, state_size);
    assert(states);

    for (iteration = 0; iteration < CELLS; ++iteration) {
        double max_delta = 0.0;
        for (i = 0u; i < CELLS; ++i) {
            const int x = (int)(i % WIDTH);
            const int y = (int)(i / WIDTH);
            set_input(input_values, descriptor, i, "d_n", neighbour(current, wall, x, y - 1));
            set_input(input_values, descriptor, i, "d_s", neighbour(current, wall, x, y + 1));
            set_input(input_values, descriptor, i, "d_e", neighbour(current, wall, x + 1, y));
            set_input(input_values, descriptor, i, "d_w", neighbour(current, wall, x - 1, y));
            if (diagonal) {
                set_input(input_values, descriptor, i, "d_ne", neighbour(current, wall, x + 1, y - 1));
                set_input(input_values, descriptor, i, "d_nw", neighbour(current, wall, x - 1, y - 1));
                set_input(input_values, descriptor, i, "d_se", neighbour(current, wall, x + 1, y + 1));
                set_input(input_values, descriptor, i, "d_sw", neighbour(current, wall, x - 1, y + 1));
                set_input(input_values, descriptor, i, "cost_diag", 1.5);
            }
            set_input(input_values, descriptor, i, "cost", 1.0);
            set_input(input_values, descriptor, i, "is_wall", wall[i] ? 1.0 : 0.0);
            set_input(input_values, descriptor, i, "is_goal", i == WIDTH - 1 ? 1.0 : 0.0);
        }
        {
            const cxpr_bulk_view view = {
                .inputs = inputs, .input_count = descriptor->input_count,
                .params = params, .param_count = descriptor->param_count,
                .outputs = outputs, .output_count = descriptor->output_count,
                .states = states, .state_stride = state_size,
                .element_count = CELLS,
            };
            assert(cxpr_bulk_run(descriptor, &view) == CXPR_BULK_OK);
        }
        for (i = 0u; i < CELLS; ++i) {
            next[i] = output_values[dist_output][i].d;
            if (next[i] != current[i]) {
                const double delta = fabs(next[i] - current[i]);
                if (delta > max_delta) max_delta = delta;
            }
        }
        {
            double* swap = current; current = next; next = swap;
        }
        if (max_delta == 0.0) break;
    }
    assert(iteration < CELLS);

    dijkstra(wall, diagonal, reference);
    for (i = 0u; i < CELLS; ++i) {
        if (wall[i]) assert(current[i] == INF_SENTINEL);
        else assert(current[i] == reference[i]);
    }

    /* Re-evaluate once at the fixed point and validate each meaningful dir. */
    for (i = 0u; i < CELLS; ++i) {
        if (!wall[i] && current[i] < INF_SENTINEL && i != WIDTH - 1) {
            const int x = (int)(i % WIDTH);
            const int y = (int)(i / WIDTH);
            double candidates[8] = {
                neighbour(current, wall, x, y - 1) + 1.0,
                neighbour(current, wall, x, y + 1) + 1.0,
                neighbour(current, wall, x + 1, y) + 1.0,
                neighbour(current, wall, x - 1, y) + 1.0,
                neighbour(current, wall, x + 1, y - 1) + 1.5,
                neighbour(current, wall, x - 1, y - 1) + 1.5,
                neighbour(current, wall, x + 1, y + 1) + 1.5,
                neighbour(current, wall, x - 1, y + 1) + 1.5,
            };
            int expected = 0;
            int d;
            for (d = 1; d < (diagonal ? 8 : 4); ++d)
                if (candidates[d] < candidates[expected]) expected = d;
            assert(output_values[dir_output][i].d == (double)expected);
        }
    }

    free(states);
}

int main(void) {
    run_case(&pathfinding_grid4_tick_descriptor, 0);
    run_case(&pathfinding_grid8_tick_descriptor, 1);
    puts("pathfinding generated-C bulk E2E matches Dijkstra (4/8 neighbours)");
    return 0;
}
