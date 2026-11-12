#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>

#include <cxpr/generated.h>
#include <cxpr/bulk.h>
#include <cxpr/engine.h>

#define ACCESS_COUNT 1000000u
#define RUNS 7u
#define MAX_STATIC_ELEMENTS 1048576u

static double static_generated_values[MAX_STATIC_ELEMENTS];

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static double static_generated_load(size_t index) {
    return static_generated_values[index];
}

#ifdef CXPR_BENCH_WRAP_ALLOC
void* __real_malloc(size_t size);
void* __real_calloc(size_t count, size_t size);
void* __real_realloc(void* pointer, size_t size);
static size_t allocation_count;
static int count_allocations;
void* __wrap_malloc(size_t size) {
    if (count_allocations) ++allocation_count;
    return __real_malloc(size);
}
void* __wrap_calloc(size_t count, size_t size) {
    if (count_allocations) ++allocation_count;
    return __real_calloc(count, size);
}
void* __wrap_realloc(void* pointer, size_t size) {
    if (count_allocations) ++allocation_count;
    return __real_realloc(pointer, size);
}
#endif

const cxpr_generated_model_descriptor* cxpr_generated_model_v1(void);

typedef double (*view_callback)(const double*, size_t, size_t);

typedef struct engine_view_env {
    const double* values;
    const size_t* indices;
    size_t value_count;
    size_t index_count;
} engine_view_env;

static double now_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static double callback_load(const double* values, size_t count, size_t index) {
    return index < count ? values[index] : 0.0;
}

static int64_t engine_map_index(int64_t cursor, void* userdata) {
    const engine_view_env* env = userdata;
    return cursor >= 0 && (size_t)cursor < env->index_count
        ? (int64_t)env->indices[cursor] : -1;
}

static bool engine_view_load(int64_t index, const char* name,
                             const double* args, size_t argc,
                             double* out_value, void* userdata) {
    const engine_view_env* env = userdata;
    (void)name;
    (void)args;
    (void)argc;
    if (index < 0 || (size_t)index >= env->value_count) return false;
    *out_value = env->values[index];
    return true;
}

static int compare_double(const void* left, const void* right) {
    const double a = *(const double*)left;
    const double b = *(const double*)right;
    return (a > b) - (a < b);
}

static double percentile(const double values[RUNS], size_t index) {
    double sorted[RUNS];
    for (size_t i = 0u; i < RUNS; ++i) sorted[i] = values[i];
    qsort(sorted, RUNS, sizeof(*sorted), compare_double);
    return sorted[index];
}

static size_t generated_source_bytes(void) {
#ifdef CXPR_BENCH_VIEW_TABLE_GENERATED_PATH
    struct stat info;
    return stat(CXPR_BENCH_VIEW_TABLE_GENERATED_PATH, &info) == 0
        ? (size_t)info.st_size : 0u;
#else
    return 0u;
#endif
}

static void fill_indices(size_t* indices, size_t count, size_t size,
                         int random_access) {
    uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
    for (size_t i = 0u; i < count; ++i) {
        if (random_access) {
            state = state * UINT64_C(6364136223846793005) + 1u;
            indices[i] = (size_t)((state >> 16u) % size);
        } else {
            indices[i] = i % size;
        }
    }
}

static void run_case(size_t size, int random_access) {
    const cxpr_generated_model_descriptor* descriptor =
        cxpr_generated_model_v1();
    double* values = malloc(size * sizeof(*values));
    cxpr_value* copied_values = NULL;
    size_t* indices = malloc(ACCESS_COUNT * sizeof(*indices));
    cxpr_view binding = {0};
    cxpr_view_resolved resolved = {0};
    cxpr_view_table table = {0};
    cxpr_value inputs[2] = {cxpr_int64(0), cxpr_int64(0)};
    cxpr_value output = cxpr_num(0.0);
    cxpr_value* bulk_rows = malloc(ACCESS_COUNT * sizeof(*bulk_rows));
    cxpr_value* bulk_columns = malloc(ACCESS_COUNT * sizeof(*bulk_columns));
    cxpr_value* bulk_outputs = malloc(ACCESS_COUNT * sizeof(*bulk_outputs));
    void* bulk_states = NULL;
    const size_t columns = size == 1024u ? 32u
        : size == 65536u ? 256u : 1024u;
    double direct_runs[RUNS];
    double callback_runs[RUNS];
    double copied_runs[RUNS];
    double static_runs[RUNS];
    double checked_runs[RUNS];
    double generated_runs[RUNS];
    double bulk_runs[RUNS];
    double engine_runs[RUNS];
    double resolve_runs[RUNS];
    size_t resolve_allocations = 0u;
    size_t generated_allocations = 0u;
    size_t bulk_allocations = 0u;
    size_t engine_allocations = 0u;
    size_t copied_hot_allocations = 0u;
    size_t copied_setup_allocations = 0u;
    double copied_setup_ns;
    double static_setup_ns;
    volatile double checksum = 0.0;
    view_callback callback = callback_load;
    cxpr_bulk_const_column bulk_input_columns[2];
    cxpr_bulk_column bulk_output_column;
    cxpr_bulk_view bulk = {0};
    engine_view_env engine_env = {values, indices, size, ACCESS_COUNT};
    cxpr_expression_def engine_expression = {"value", "samples"};
    cxpr_engine_view_source_def engine_source = {
        "samples", engine_view_load, &engine_env, engine_map_index};
    cxpr_engine_config engine_config = {0};
    cxpr_error engine_error = {0};
    cxpr_engine_program* engine_program;
    cxpr_engine_session* engine_session;

    if (!values || !indices || !bulk_rows || !bulk_columns || !bulk_outputs ||
        !descriptor || !descriptor->view_tick) abort();
    for (size_t i = 0u; i < size; ++i)
        values[i] = (double)(i % 997u) * 0.125;
    {
#ifdef CXPR_BENCH_WRAP_ALLOC
        const size_t before = allocation_count;
        count_allocations = 1;
#endif
        const double start = now_ns();
        copied_values = malloc(size * sizeof(*copied_values));
        if (!copied_values) abort();
        for (size_t i = 0u; i < size; ++i)
            copied_values[i] = cxpr_num(values[i]);
        copied_setup_ns = now_ns() - start;
#ifdef CXPR_BENCH_WRAP_ALLOC
        count_allocations = 0;
        copied_setup_allocations = allocation_count - before;
#endif
    }
    {
        const double start = now_ns();
        for (size_t i = 0u; i < size; ++i)
            static_generated_values[i] = values[i];
        static_setup_ns = now_ns() - start;
    }
    fill_indices(indices, ACCESS_COUNT, size, random_access);
    for (size_t i = 0u; i < ACCESS_COUNT; ++i) {
        bulk_rows[i] = cxpr_int64((int64_t)(indices[i] / columns));
        bulk_columns[i] = cxpr_int64((int64_t)(indices[i] % columns));
    }

    binding.data = values;
    binding.element_type = CXPR_VALUE_NUMBER;
    binding.rank = 2u;
    binding.shape[0] = size / columns;
    binding.shape[1] = columns;
    binding.strides[0] = (ptrdiff_t)(columns * sizeof(double));
    binding.strides[1] = (ptrdiff_t)sizeof(double);
    binding.capacity_bytes = size * sizeof(double);
    binding.flags = CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS;

    for (size_t run = 0u; run < RUNS; ++run) {
#ifdef CXPR_BENCH_WRAP_ALLOC
        const size_t before = allocation_count;
        count_allocations = 1;
#endif
        double start = now_ns();
        if (cxpr_views_resolve(
                descriptor->views, &binding, descriptor->view_count,
                &resolved, NULL) != CXPR_VIEW_OK) abort();
        resolve_runs[run] = now_ns() - start;
#ifdef CXPR_BENCH_WRAP_ALLOC
        count_allocations = 0;
        resolve_allocations += allocation_count - before;
#endif
    }
    table.views = &resolved;
    table.count = 1u;
    table.generation = 1u;

    bulk_input_columns[0] = (cxpr_bulk_const_column){bulk_rows, 1u};
    bulk_input_columns[1] = (cxpr_bulk_const_column){bulk_columns, 1u};
    bulk_output_column = (cxpr_bulk_column){bulk_outputs, 1u};
    bulk.inputs = bulk_input_columns;
    bulk.input_count = 2u;
    bulk.outputs = &bulk_output_column;
    bulk.output_count = 1u;
    bulk.element_count = ACCESS_COUNT;
    bulk.state_stride = descriptor->state_size();
    if (bulk.state_stride > 0u) {
        bulk_states = calloc(ACCESS_COUNT, bulk.state_stride);
        if (!bulk_states) abort();
        bulk.states = bulk_states;
    }
    if (cxpr_bulk_validate_views(descriptor, &bulk, &table) != CXPR_BULK_OK)
        abort();

    engine_config.expressions = &engine_expression;
    engine_config.expression_count = 1u;
    engine_config.view_sources = &engine_source;
    engine_config.view_source_count = 1u;
    engine_program = cxpr_engine_program_new(&engine_config, &engine_error);
    engine_session = engine_program ? cxpr_engine_session_new(engine_program) : NULL;
    if (!engine_session) abort();

    for (size_t run = 0u; run < RUNS; ++run) {
        double start = now_ns();
        for (size_t i = 0u; i < ACCESS_COUNT; ++i)
            checksum += values[indices[i]];
        direct_runs[run] = (now_ns() - start) / ACCESS_COUNT;

        start = now_ns();
        for (size_t i = 0u; i < ACCESS_COUNT; ++i)
            checksum += callback(values, size, indices[i]);
        callback_runs[run] = (now_ns() - start) / ACCESS_COUNT;

        start = now_ns();
#ifdef CXPR_BENCH_WRAP_ALLOC
        size_t copied_before = allocation_count;
        count_allocations = 1;
#endif
        for (size_t i = 0u; i < ACCESS_COUNT; ++i)
            checksum += copied_values[indices[i]].d;
        copied_runs[run] = (now_ns() - start) / ACCESS_COUNT;
#ifdef CXPR_BENCH_WRAP_ALLOC
        count_allocations = 0;
        copied_hot_allocations += allocation_count - copied_before;
#endif

        start = now_ns();
        for (size_t i = 0u; i < ACCESS_COUNT; ++i)
            checksum += static_generated_load(indices[i]);
        static_runs[run] = (now_ns() - start) / ACCESS_COUNT;

        start = now_ns();
#ifdef CXPR_BENCH_WRAP_ALLOC
        size_t before = allocation_count;
        count_allocations = 1;
#endif
        for (size_t i = 0u; i < ACCESS_COUNT; ++i) {
            const double view_indices[2] = {
                (double)(indices[i] / columns),
                (double)(indices[i] % columns)};
            if (cxpr_view_load_checked(&table, 0u, view_indices, 2u, &output) !=
                CXPR_VIEW_OK) abort();
            checksum += output.d;
        }
        checked_runs[run] = (now_ns() - start) / ACCESS_COUNT;

        start = now_ns();
        for (size_t i = 0u; i < ACCESS_COUNT; ++i) {
            inputs[0].i64 = (int64_t)(indices[i] / columns);
            inputs[1].i64 = (int64_t)(indices[i] % columns);
            descriptor->view_tick(NULL, inputs, NULL, &output, &table);
            checksum += output.d;
        }
        generated_runs[run] = (now_ns() - start) / ACCESS_COUNT;
#ifdef CXPR_BENCH_WRAP_ALLOC
        count_allocations = 0;
        generated_allocations += allocation_count - before;
#endif

        start = now_ns();
#ifdef CXPR_BENCH_WRAP_ALLOC
        before = allocation_count;
        count_allocations = 1;
#endif
        if (cxpr_bulk_run_views(descriptor, &bulk, &table) != CXPR_BULK_OK)
            abort();
        bulk_runs[run] = (now_ns() - start) / ACCESS_COUNT;
#ifdef CXPR_BENCH_WRAP_ALLOC
        count_allocations = 0;
        bulk_allocations += allocation_count - before;
#endif
        checksum += bulk_outputs[ACCESS_COUNT - 1u].d;

        cxpr_engine_session_reset(engine_session);
        start = now_ns();
#ifdef CXPR_BENCH_WRAP_ALLOC
        before = allocation_count;
        count_allocations = 1;
#endif
        for (size_t i = 0u; i < ACCESS_COUNT; ++i) {
            bool found = false;
            if (!cxpr_engine_tick(engine_session, NULL, NULL, &engine_error))
                abort();
            checksum += cxpr_engine_get_double(engine_session, "value", &found);
            if (!found) abort();
        }
        engine_runs[run] = (now_ns() - start) / ACCESS_COUNT;
#ifdef CXPR_BENCH_WRAP_ALLOC
        count_allocations = 0;
        engine_allocations += allocation_count - before;
#endif
    }

    printf(
        "size=%zu pattern=%s resolve_ns=%.2f direct_ns=%.3f "
        "callback_ns=%.3f copied_value_ns=%.3f static_array_ns=%.3f "
        "checked_table_ns=%.3f generated_view_tick_ns=%.3f "
        "bulk_shared_ns=%.3f engine_view_source_ns=%.3f "
        "generated_p95_ns=%.3f bulk_p95_ns=%.3f engine_p95_ns=%.3f "
        "resolve_allocs=%zu generated_allocs=%zu bulk_allocs=%zu engine_allocs=%zu "
        "copied_setup_ns=%.0f copied_setup_allocs=%zu copied_hot_allocs=%zu "
        "copied_bytes=%zu static_setup_ns=%.0f static_bytes=%zu "
        "generated_source_bytes=%zu state_bytes=%zu "
        "checksum=%.3f\n",
        size, random_access ? "random" : "sequential",
        percentile(resolve_runs, RUNS / 2u),
        percentile(direct_runs, RUNS / 2u),
        percentile(callback_runs, RUNS / 2u),
        percentile(copied_runs, RUNS / 2u),
        percentile(static_runs, RUNS / 2u),
        percentile(checked_runs, RUNS / 2u),
        percentile(generated_runs, RUNS / 2u),
        percentile(bulk_runs, RUNS / 2u),
        percentile(engine_runs, RUNS / 2u),
        percentile(generated_runs, RUNS - 1u),
        percentile(bulk_runs, RUNS - 1u),
        percentile(engine_runs, RUNS - 1u),
        resolve_allocations, generated_allocations, bulk_allocations,
        engine_allocations, copied_setup_ns, copied_setup_allocations,
        copied_hot_allocations, size * sizeof(*copied_values), static_setup_ns,
        size * sizeof(*static_generated_values),
        generated_source_bytes(), descriptor->state_size(),
        (double)checksum);

    cxpr_engine_session_free(engine_session);
    cxpr_engine_program_free(engine_program);
    free(bulk_states);
    free(bulk_outputs);
    free(bulk_columns);
    free(bulk_rows);
    free(indices);
    free(copied_values);
    free(values);
}

int main(void) {
    static const size_t sizes[] = {1024u, 65536u, 1048576u};
    for (size_t i = 0u; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        run_case(sizes[i], 0);
        run_case(sizes[i], 1);
    }
    return 0;
}
