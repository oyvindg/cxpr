/*
 * R7 -- sentinel/inf convention: runs today (no gate).
 *
 * R7 needs no new builtin: saturation is the plain cxpr function
 *   fn sadd(a, b) = min(a + b, $INF)
 * which lowers to generated-C/CUDA. So this test is ungated and asserts:
 *   - sadd identity: sadd(INF, cost) == INF, sadd(x, cost) == x + cost, min(INF,x)==x
 *   - a wall cell never propagates a finite distance to neighbours over N iterations,
 *   - no NaN appears in any output.
 *
 * The full flow-field descriptor (grid_relax_4nabo) additionally needs R6 (argmin);
 * this test uses the distance-only fixture (grid_dist_4nabo) so it runs on
 * release/3.2.0 today.
 *
 * See plans/field_pathfinding_requirements.md (R7).
 */
#include <cxpr/cxpr.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CXPR_TEST_SOURCE_DIR
#define CXPR_TEST_SOURCE_DIR "."
#endif

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

static cxpr_model_compiled* compile_fixture(const char* name, cxpr_model** out_model) {
    cxpr_error err = {0};
    char* source = read_fixture(name);
    cxpr_model* model = cxpr_model_parse(source, &err);
    cxpr_model_compiled* program;
    free(source);
    assert(model);
    program = cxpr_model_compile(model, NULL, &err);
    assert(program);
    *out_model = model;
    return program;
}

/* sadd identity via the sentinel fixture. */
static void test_sadd_identity(void) {
    cxpr_error err = {0};
    cxpr_model* model;
    cxpr_model_compiled* program = compile_fixture("sentinel.cxpr", &model);
    cxpr_context* ctx = cxpr_context_new();
    bool found = false;

    assert(cxpr_model_compiled_seed_defaults(program, ctx, NULL, &err)); /* $INF */

    /* blocked neighbour stays INF */
    cxpr_context_set(ctx, "neighbour", INF_SENTINEL);
    cxpr_context_set(ctx, "cost", 1.0);
    assert(cxpr_model_compiled_eval(program, ctx, NULL, &err));
    assert(cxpr_context_get(ctx, "relaxed", &found) == INF_SENTINEL);
    assert(cxpr_context_get(ctx, "best", &found) == INF_SENTINEL);

    /* reachable neighbour accumulates cost */
    cxpr_context_set(ctx, "neighbour", 5.0);
    cxpr_context_set(ctx, "cost", 2.0);
    assert(cxpr_model_compiled_eval(program, ctx, NULL, &err));
    assert(cxpr_context_get(ctx, "relaxed", &found) == 7.0);

    cxpr_context_free(ctx);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
}

/* One relaxation sweep over a GRID_N x GRID_N field using the compiled cell model.
 * Cell (2,2) is a wall; (0,0) is the goal. Neighbours off-grid read as INF. */
static void relax_once(cxpr_model_compiled* program, const double* dist,
                       const int* wall, const int* goal, double* out) {
    cxpr_error err = {0};
    cxpr_context* ctx = cxpr_context_new();
    assert(cxpr_model_compiled_seed_defaults(program, ctx, NULL, &err)); /* $INF */
    for (int y = 0; y < GRID_N; ++y) {
        for (int x = 0; x < GRID_N; ++x) {
            int i = y * GRID_N + x;
            double dn = (y > 0)          ? dist[i - GRID_N] : INF_SENTINEL;
            double ds = (y < GRID_N - 1) ? dist[i + GRID_N] : INF_SENTINEL;
            double de = (x < GRID_N - 1) ? dist[i + 1]      : INF_SENTINEL;
            double dw = (x > 0)          ? dist[i - 1]      : INF_SENTINEL;
            double next;
            bool found = false;

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
    cxpr_model* model;
    cxpr_model_compiled* program = compile_fixture("grid_dist_4nabo.cxpr", &model);
    double a[GRID_N * GRID_N];
    double b[GRID_N * GRID_N];
    int wall[GRID_N * GRID_N] = {0};
    int goal[GRID_N * GRID_N] = {0};
    double* cur = a;
    double* nxt = b;

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
    /* The goal is reachable, so a non-wall cell next to the goal is finite. */
    assert(cur[1] < INF_SENTINEL);

    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
}

int main(void) {
    test_sadd_identity();
    test_wall_never_propagates_finite();
    printf("pathfinding_sentinel: OK\n");
    return 0;
}
