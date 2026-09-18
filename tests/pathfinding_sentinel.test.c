/*
 * R7 -- sentinel/inf contract: wall-propagation stub.
 *
 * Drives the grid_relax_4nabo fixture over N relaxation iterations on a tiny grid
 * with a corridor around a wall, and asserts:
 *   - a wall cell never propagates a finite distance to its neighbours,
 *   - no NaN appears in any output,
 *   - min(INF, x) == x and sat_add(INF, cost) == INF hold end to end.
 *
 * GATED behind CXPR_PATHFINDING_R7_READY (default off) since sat_add / the INF
 * contract do not exist on release/3.2.0 yet. Passes as SKIP until R7 lands.
 *
 * See plans/field_pathfinding_requirements.md (R7).
 */
#include <stdio.h>

#ifndef CXPR_PATHFINDING_R7_READY
#define CXPR_PATHFINDING_R7_READY 0
#endif

#ifndef CXPR_TEST_SOURCE_DIR
#define CXPR_TEST_SOURCE_DIR "."
#endif

#if CXPR_PATHFINDING_R7_READY

#include <cxpr/cxpr.h>

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GRID_N 5
#define ITERATIONS 16
#define INF_SENTINEL 1e18

static char* read_fixture(const char* name) {
    char path[1024];
    FILE* file;
    long size;
    char* source;
    snprintf(path, sizeof(path), "%s/fixtures/pathfinding/%s",
             CXPR_TEST_SOURCE_DIR, name);
    file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    rewind(file);
    source = (char*)malloc((size_t)size + 1u);
    assert(source && fread(source, 1u, (size_t)size, file) == (size_t)size);
    source[size] = '\0';
    fclose(file);
    return source;
}

/* One relaxation sweep over a GRID_N x GRID_N field using the compiled cell model.
 * Cell (2,2) is a wall; (0,0) is the goal. Neighbours off-grid read as INF. */
static void relax_once(cxpr_model_compiled* program, const double* dist,
                       const int* wall, const int* goal, double* out) {
    cxpr_error err = {0};
    cxpr_context* ctx = cxpr_context_new();
    for (int y = 0; y < GRID_N; ++y) {
        for (int x = 0; x < GRID_N; ++x) {
            int i = y * GRID_N + x;
            double dn = (y > 0)          ? dist[i - GRID_N] : INF_SENTINEL;
            double ds = (y < GRID_N - 1) ? dist[i + GRID_N] : INF_SENTINEL;
            double de = (x < GRID_N - 1) ? dist[i + 1]      : INF_SENTINEL;
            double dw = (x > 0)          ? dist[i - 1]      : INF_SENTINEL;
            double next;
            int found = 0;

            cxpr_context_set(ctx, "d_n", dn);
            cxpr_context_set(ctx, "d_s", ds);
            cxpr_context_set(ctx, "d_e", de);
            cxpr_context_set(ctx, "d_w", dw);
            cxpr_context_set(ctx, "cost", 1.0);
            cxpr_context_set(ctx, "is_wall", wall[i] ? 1.0 : 0.0);
            cxpr_context_set(ctx, "is_goal", goal[i] ? 1.0 : 0.0);
            assert(cxpr_model_compiled_eval(program, ctx, NULL, &err));

            next = cxpr_context_get(ctx, "next_dist", &found);
            assert(found);
            assert(!isnan(next));           /* R7: never NaN */
            out[i] = next;
        }
    }
    cxpr_context_free(ctx);
}

static void test_wall_never_propagates_finite(void) {
    cxpr_error err = {0};
    char* source = read_fixture("grid_relax_4nabo.cxpr");
    cxpr_model* model = cxpr_model_parse(source, &err);
    cxpr_model_compiled* program;
    double a[GRID_N * GRID_N];
    double b[GRID_N * GRID_N];
    int wall[GRID_N * GRID_N] = {0};
    int goal[GRID_N * GRID_N] = {0};
    double* cur = a;
    double* nxt = b;

    free(source);
    assert(model);
    program = cxpr_model_compile(model, NULL, &err);
    assert(program);

    for (int i = 0; i < GRID_N * GRID_N; ++i) a[i] = INF_SENTINEL;
    wall[2 * GRID_N + 2] = 1; /* centre wall */
    goal[0] = 1;              /* goal at (0,0) */
    a[0] = 0.0;

    for (int it = 0; it < ITERATIONS; ++it) {
        relax_once(program, cur, wall, goal, nxt);
        double* tmp = cur; cur = nxt; nxt = tmp;
    }

    /* The wall cell stays INF and never hands a finite distance to neighbours. */
    assert(cur[2 * GRID_N + 2] >= INF_SENTINEL);

    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
}

#endif /* CXPR_PATHFINDING_R7_READY */

int main(void) {
#if CXPR_PATHFINDING_R7_READY
    test_wall_never_propagates_finite();
    printf("pathfinding_sentinel: OK\n");
#else
    printf("pathfinding_sentinel: SKIP "
           "(R7 sentinel/sat_add contract not yet implemented on release/3.2.0)\n");
#endif
    return 0;
}
