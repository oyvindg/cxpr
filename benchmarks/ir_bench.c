#include <cxpr/cxpr.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef CXPR_BENCH_IR_SIMPLE_ARITH_INLINE
#include CXPR_BENCH_IR_SIMPLE_ARITH_INLINE
#endif
#ifdef CXPR_BENCH_IR_NESTED_EXPR_INLINE
#include CXPR_BENCH_IR_NESTED_EXPR_INLINE
#endif
#ifdef CXPR_BENCH_IR_FUNCTION_CALL_INLINE
#include CXPR_BENCH_IR_FUNCTION_CALL_INLINE
#endif
#ifdef CXPR_BENCH_IR_DEFINED_FN_INLINE
#include CXPR_BENCH_IR_DEFINED_FN_INLINE
#endif
#ifdef CXPR_BENCH_IR_DEFINED_CHAIN_INLINE
#include CXPR_BENCH_IR_DEFINED_CHAIN_INLINE
#endif
#ifdef CXPR_BENCH_IR_DEEP_DEFINED_INLINE
#include CXPR_BENCH_IR_DEEP_DEFINED_INLINE
#endif
#ifdef CXPR_BENCH_IR_COMPLEX_SIGNAL_INLINE
#include CXPR_BENCH_IR_COMPLEX_SIGNAL_INLINE
#endif
#ifdef CXPR_BENCH_IR_LARGE_ARITH_INLINE
#include CXPR_BENCH_IR_LARGE_ARITH_INLINE
#endif
#ifdef CXPR_BENCH_IR_LARGE_BRANCH_INLINE
#include CXPR_BENCH_IR_LARGE_BRANCH_INLINE
#endif
#ifdef CXPR_BENCH_IR_LARGE_MATH_INLINE
#include CXPR_BENCH_IR_LARGE_MATH_INLINE
#endif
#ifdef CXPR_BENCH_IR_MIXED_EXPR_INLINE
#include CXPR_BENCH_IR_MIXED_EXPR_INLINE
#endif
#ifdef CXPR_BENCH_IR_MIXED_PIPE_INLINE
#include CXPR_BENCH_IR_MIXED_PIPE_INLINE
#endif
#ifdef CXPR_BENCH_IR_CONTEXT_CHURN_INLINE
#include CXPR_BENCH_IR_CONTEXT_CHURN_INLINE
#endif
#ifdef CXPR_BENCH_IR_STRUCT_SCALAR_MUL_INLINE
#include CXPR_BENCH_IR_STRUCT_SCALAR_MUL_INLINE
#endif
#ifdef CXPR_BENCH_IR_SCALAR_STRUCT_MUL_INLINE
#include CXPR_BENCH_IR_SCALAR_STRUCT_MUL_INLINE
#endif
#ifdef CXPR_BENCH_IR_STRUCT_STRUCT_MUL_INLINE
#include CXPR_BENCH_IR_STRUCT_STRUCT_MUL_INLINE
#endif
#ifdef CXPR_BENCH_IR_STRUCT_STRUCT_ADD_INLINE
#include CXPR_BENCH_IR_STRUCT_STRUCT_ADD_INLINE
#endif
#ifdef CXPR_BENCH_IR_STRUCT_SCALAR_MUL_ALL_FIELDS_INLINE
#include CXPR_BENCH_IR_STRUCT_SCALAR_MUL_ALL_FIELDS_INLINE
#endif
#ifdef CXPR_BENCH_IR_SCALAR_STRUCT_MUL_ALL_FIELDS_INLINE
#include CXPR_BENCH_IR_SCALAR_STRUCT_MUL_ALL_FIELDS_INLINE
#endif
#ifdef CXPR_BENCH_IR_STRUCT_STRUCT_MUL_ALL_FIELDS_INLINE
#include CXPR_BENCH_IR_STRUCT_STRUCT_MUL_ALL_FIELDS_INLINE
#endif
#ifdef CXPR_BENCH_IR_STRUCT_STRUCT_ADD_ALL_FIELDS_INLINE
#include CXPR_BENCH_IR_STRUCT_STRUCT_ADD_ALL_FIELDS_INLINE
#endif
#ifdef CXPR_BENCH_IR_LOOKBACK_LEAF_INLINE
#include CXPR_BENCH_IR_LOOKBACK_LEAF_INLINE
#endif
#ifdef CXPR_BENCH_IR_LOOKBACK_MIXED_INLINE
#include CXPR_BENCH_IR_LOOKBACK_MIXED_INLINE
#endif

typedef enum {
    BENCH_C_NONE = 0,
    BENCH_C_SIMPLE_ARITH,
    BENCH_C_NESTED_EXPR,
    BENCH_C_FUNCTION_CALL,
    BENCH_C_DEFINED_FN,
    BENCH_C_DEFINED_CHAIN,
    BENCH_C_DEEP_DEFINED,
    BENCH_C_COMPLEX_SIGNAL,
    BENCH_C_LARGE_ARITH,
    BENCH_C_LARGE_BRANCH,
    BENCH_C_LARGE_MATH,
    BENCH_C_MIXED_EXPR,
    BENCH_C_MIXED_PIPE,
    BENCH_C_CONTEXT_CHURN,
    BENCH_C_STRUCT_SCALAR_MUL,
    BENCH_C_SCALAR_STRUCT_MUL,
    BENCH_C_STRUCT_STRUCT_MUL,
    BENCH_C_STRUCT_STRUCT_ADD,
    BENCH_C_STRUCT_SCALAR_MUL_ALL_FIELDS,
    BENCH_C_SCALAR_STRUCT_MUL_ALL_FIELDS,
    BENCH_C_STRUCT_STRUCT_MUL_ALL_FIELDS,
    BENCH_C_STRUCT_STRUCT_ADD_ALL_FIELDS,
    BENCH_C_LOOKBACK_LEAF,
    BENCH_C_LOOKBACK_MIXED,
} bench_c_model;

typedef struct {
    const char* name;
    const char* fixture;
    size_t iterations;
    int mutate_context;
    bench_c_model c_model;
} bench_case;

typedef struct {
    const char* name;
    const char* fixture;
    size_t iterations;
    const char* field;
    int free_result;
    bench_c_model c_model;
} typed_bench_case;

typedef struct {
    const char* name;
    const char* fixture;
    size_t iterations;
    bench_c_model c_model;
} lookback_bench_case;

static volatile double g_sink = 0.0;

#ifndef CXPR_BENCH_FIXTURE_DIR
#error "CXPR_BENCH_FIXTURE_DIR must name the benchmark fixture directory"
#endif

typedef struct {
    char* source;
    cxpr_model* model;
    const cxpr_expr_ast* result;
} bench_model_source;

static char* read_text_file(const char* path) {
    FILE* file = fopen(path, "rb");
    long size;
    char* text;
    if (!file || fseek(file, 0, SEEK_END) != 0) return NULL;
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    text = (char*)malloc((size_t)size + 1u);
    if (!text || fread(text, 1u, (size_t)size, file) != (size_t)size) {
        free(text);
        fclose(file);
        return NULL;
    }
    text[size] = '\0';
    fclose(file);
    return text;
}

static bench_model_source load_bench_model(const char* fixture) {
    bench_model_source loaded = {0};
    cxpr_error err = {0};
    char path[1024];
    size_t i;

    snprintf(path, sizeof(path), "%s/%s", CXPR_BENCH_FIXTURE_DIR, fixture);
    loaded.source = read_text_file(path);
    if (!loaded.source) {
        fprintf(stderr, "Failed to read benchmark fixture '%s'\n", path);
        exit(1);
    }
    loaded.model = cxpr_model_parse(loaded.source, &err);
    if (!loaded.model) {
        fprintf(stderr, "Failed to parse benchmark fixture '%s': %s\n",
                path, err.message ? err.message : "(null)");
        exit(1);
    }
    for (i = 0u; i < cxpr_model_binding_count(loaded.model); ++i) {
        if (strcmp(cxpr_model_binding_name(loaded.model, i), "result") == 0) {
            loaded.result = cxpr_model_binding_expr(loaded.model, i);
            break;
        }
    }
    if (!loaded.result) {
        fprintf(stderr, "Benchmark fixture '%s' has no result binding\n", path);
        exit(1);
    }
    return loaded;
}

static void free_bench_model(bench_model_source* loaded) {
    cxpr_model_free(loaded->model);
    free(loaded->source);
    *loaded = (bench_model_source){0};
}

enum { LOOKBACK_BARS = 4096 };
static double g_close[LOOKBACK_BARS];
static double g_high[LOOKBACK_BARS];
static int64_t g_lookback_cursor = 0;

static const char* bench_c_model_inline_path(bench_c_model model) {
    switch (model) {
#ifdef CXPR_BENCH_IR_SIMPLE_ARITH_INLINE
    case BENCH_C_SIMPLE_ARITH: return CXPR_BENCH_IR_SIMPLE_ARITH_INLINE;
#endif
#ifdef CXPR_BENCH_IR_NESTED_EXPR_INLINE
    case BENCH_C_NESTED_EXPR: return CXPR_BENCH_IR_NESTED_EXPR_INLINE;
#endif
#ifdef CXPR_BENCH_IR_FUNCTION_CALL_INLINE
    case BENCH_C_FUNCTION_CALL: return CXPR_BENCH_IR_FUNCTION_CALL_INLINE;
#endif
#ifdef CXPR_BENCH_IR_DEFINED_FN_INLINE
    case BENCH_C_DEFINED_FN: return CXPR_BENCH_IR_DEFINED_FN_INLINE;
#endif
#ifdef CXPR_BENCH_IR_DEFINED_CHAIN_INLINE
    case BENCH_C_DEFINED_CHAIN: return CXPR_BENCH_IR_DEFINED_CHAIN_INLINE;
#endif
#ifdef CXPR_BENCH_IR_DEEP_DEFINED_INLINE
    case BENCH_C_DEEP_DEFINED: return CXPR_BENCH_IR_DEEP_DEFINED_INLINE;
#endif
#ifdef CXPR_BENCH_IR_COMPLEX_SIGNAL_INLINE
    case BENCH_C_COMPLEX_SIGNAL: return CXPR_BENCH_IR_COMPLEX_SIGNAL_INLINE;
#endif
#ifdef CXPR_BENCH_IR_LARGE_ARITH_INLINE
    case BENCH_C_LARGE_ARITH: return CXPR_BENCH_IR_LARGE_ARITH_INLINE;
#endif
#ifdef CXPR_BENCH_IR_LARGE_BRANCH_INLINE
    case BENCH_C_LARGE_BRANCH: return CXPR_BENCH_IR_LARGE_BRANCH_INLINE;
#endif
#ifdef CXPR_BENCH_IR_LARGE_MATH_INLINE
    case BENCH_C_LARGE_MATH: return CXPR_BENCH_IR_LARGE_MATH_INLINE;
#endif
#ifdef CXPR_BENCH_IR_MIXED_EXPR_INLINE
    case BENCH_C_MIXED_EXPR: return CXPR_BENCH_IR_MIXED_EXPR_INLINE;
#endif
#ifdef CXPR_BENCH_IR_MIXED_PIPE_INLINE
    case BENCH_C_MIXED_PIPE: return CXPR_BENCH_IR_MIXED_PIPE_INLINE;
#endif
#ifdef CXPR_BENCH_IR_CONTEXT_CHURN_INLINE
    case BENCH_C_CONTEXT_CHURN: return CXPR_BENCH_IR_CONTEXT_CHURN_INLINE;
#endif
#ifdef CXPR_BENCH_IR_STRUCT_SCALAR_MUL_INLINE
    case BENCH_C_STRUCT_SCALAR_MUL: return CXPR_BENCH_IR_STRUCT_SCALAR_MUL_INLINE;
#endif
#ifdef CXPR_BENCH_IR_SCALAR_STRUCT_MUL_INLINE
    case BENCH_C_SCALAR_STRUCT_MUL: return CXPR_BENCH_IR_SCALAR_STRUCT_MUL_INLINE;
#endif
#ifdef CXPR_BENCH_IR_STRUCT_STRUCT_MUL_INLINE
    case BENCH_C_STRUCT_STRUCT_MUL: return CXPR_BENCH_IR_STRUCT_STRUCT_MUL_INLINE;
#endif
#ifdef CXPR_BENCH_IR_STRUCT_STRUCT_ADD_INLINE
    case BENCH_C_STRUCT_STRUCT_ADD: return CXPR_BENCH_IR_STRUCT_STRUCT_ADD_INLINE;
#endif
#ifdef CXPR_BENCH_IR_STRUCT_SCALAR_MUL_ALL_FIELDS_INLINE
    case BENCH_C_STRUCT_SCALAR_MUL_ALL_FIELDS: return CXPR_BENCH_IR_STRUCT_SCALAR_MUL_ALL_FIELDS_INLINE;
#endif
#ifdef CXPR_BENCH_IR_SCALAR_STRUCT_MUL_ALL_FIELDS_INLINE
    case BENCH_C_SCALAR_STRUCT_MUL_ALL_FIELDS: return CXPR_BENCH_IR_SCALAR_STRUCT_MUL_ALL_FIELDS_INLINE;
#endif
#ifdef CXPR_BENCH_IR_STRUCT_STRUCT_MUL_ALL_FIELDS_INLINE
    case BENCH_C_STRUCT_STRUCT_MUL_ALL_FIELDS: return CXPR_BENCH_IR_STRUCT_STRUCT_MUL_ALL_FIELDS_INLINE;
#endif
#ifdef CXPR_BENCH_IR_STRUCT_STRUCT_ADD_ALL_FIELDS_INLINE
    case BENCH_C_STRUCT_STRUCT_ADD_ALL_FIELDS: return CXPR_BENCH_IR_STRUCT_STRUCT_ADD_ALL_FIELDS_INLINE;
#endif
#ifdef CXPR_BENCH_IR_LOOKBACK_LEAF_INLINE
    case BENCH_C_LOOKBACK_LEAF: return CXPR_BENCH_IR_LOOKBACK_LEAF_INLINE;
#endif
#ifdef CXPR_BENCH_IR_LOOKBACK_MIXED_INLINE
    case BENCH_C_LOOKBACK_MIXED: return CXPR_BENCH_IR_LOOKBACK_MIXED_INLINE;
#endif
    case BENCH_C_NONE:
    default:
        return NULL;
    }
}

static int print_file(FILE* out, const char* path) {
    char buf[4096];
    size_t nread;
    int last = '\n';
    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open generated C '%s'\n", path);
        return 1;
    }
    while ((nread = fread(buf, 1, sizeof(buf), file)) > 0) {
        if (fwrite(buf, 1, nread, out) != nread) {
            fclose(file);
            return 1;
        }
        last = buf[nread - 1];
    }
    if (ferror(file)) {
        fprintf(stderr, "Failed to read generated C '%s'\n", path);
        fclose(file);
        return 1;
    }
    fclose(file);
    if (last != '\n') fputc('\n', out);
    return 0;
}

static int print_generated_c_case(const char* name, const char* fixture, bench_c_model model, const char* filter, int* matched) {
    const char* path = bench_c_model_inline_path(model);
    if (filter && strcmp(name, filter) != 0) return 0;
    if (filter) *matched = 1;
    if (!path) {
        if (filter) {
            printf("\n=== %s ===\n", name);
            printf(".cxpr fixture: %s\n", fixture);
            printf(".inc path: -\n");
            printf("No generated .cxpr C for this benchmark case.\n");
        }
        return 0;
    }
    printf("\n=== %s ===\n", name);
    printf(".cxpr fixture: %s\n", fixture);
    printf(".inc path: %s\n", path);
    printf("--- generated C ---\n");
    return print_file(stdout, path);
}

static double native_sq(double x) {
    return x * x;
}

static double native_hyp2(double x, double y) {
    return sqrt(x * x + y * y);
}

static double native_f3(double x, double y, double z) {
    return sqrt((x * x + y * y) + z * z);
}

static double native_f5(double a, double b, double c, double d) {
    const double t1 = a * a + b * b;
    const double t2 = c * c + d * d;
    return sqrt(t1 + t2);
}

static double native_f5_adapter(const double* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return native_f5(args[0], args[1], args[2], args[3]);
}

static void bench_macd(const double* args, size_t argc,
                       cxpr_value* out, size_t field_count,
                       void* userdata) {
    (void)userdata;
    (void)field_count;
    if (argc != 3) {
        out[0] = cxpr_num(NAN);
        out[1] = cxpr_num(NAN);
        out[2] = cxpr_num(NAN);
        return;
    }

    out[0] = cxpr_num((args[0] - args[1]) * 0.1);
    out[1] = cxpr_num(args[2] * 0.25);
    out[2] = cxpr_num(out[0].d - out[1].d);
}

static cxpr_value bench_tf_value_fn(const cxpr_value* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return cxpr_num(args[0].d + 1.0);
}

static cxpr_value bench_tf_ast_handler_fn(const cxpr_expr_ast* call_ast,
                                          const cxpr_context* ctx,
                                          const cxpr_registry* reg,
                                          void* userdata,
                                          cxpr_error* err) {
    double value = 0.0;

    (void)userdata;
    if (!cxpr_eval_ast_number(cxpr_expr_ast_call_arg(call_ast, 0), ctx, reg, &value, err)) {
        return cxpr_num(NAN);
    }

    if (cxpr_expr_ast_call_arg_count(call_ast) == 2) {
        const cxpr_expr_ast* timeframe = cxpr_expr_ast_call_arg(call_ast, 1);
        if (!timeframe || cxpr_expr_ast_kind_of(timeframe) != CXPR_NODE_STRING ||
            strcmp(cxpr_expr_ast_string_value(timeframe), "1h") != 0) {
            if (err) {
                err->code = CXPR_ERR_SYNTAX;
                err->message = "bench_tf expects timeframe \"1h\"";
            }
            return cxpr_num(NAN);
        }
        return cxpr_num(value + 1000.0);
    }

    return cxpr_num(value + 1.0);
}

static long long now_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void set_base_values(cxpr_context* ctx) {
    cxpr_context_set(ctx, "a", 1.5);
    cxpr_context_set(ctx, "b", 2.5);
    cxpr_context_set(ctx, "c", 3.5);
    cxpr_context_set(ctx, "d", 4.5);
    cxpr_context_set(ctx, "e", 5.5);
    cxpr_context_set(ctx, "f", 6.5);
    cxpr_context_set(ctx, "g", 7.5);
    cxpr_context_set(ctx, "h", 8.5);
    cxpr_context_set(ctx, "i", 9.5);
    cxpr_context_set(ctx, "j", 10.5);
    cxpr_context_set(ctx, "x", 11.5);
    cxpr_context_set(ctx, "y", 12.5);
    cxpr_context_set(ctx, "z", 13.5);
    cxpr_context_set(ctx, "m", 14.5);
    cxpr_context_set(ctx, "n", -15.5);
}

static void set_base_struct_values(cxpr_context* ctx) {
    const char* fields[] = {"x", "y", "z"};
    cxpr_value vector_values[] = {
        cxpr_num(2.0),
        cxpr_num(4.0),
        cxpr_num(8.0),
    };
    cxpr_value weight_values[] = {
        cxpr_num(3.0),
        cxpr_num(5.0),
        cxpr_num(7.0),
    };
    cxpr_struct_value* vector = cxpr_struct_value_new(fields, vector_values, 3u);
    cxpr_struct_value* weights = cxpr_struct_value_new(fields, weight_values, 3u);

    if (!vector || !weights) {
        fprintf(stderr, "Failed to allocate benchmark structs\n");
        exit(1);
    }
    cxpr_context_set_struct(ctx, "vector", vector);
    cxpr_context_set_struct(ctx, "weights", weights);
    cxpr_struct_value_free(weights);
    cxpr_struct_value_free(vector);
}

static void init_lookback_bars(void) {
    uint64_t state = 0x9e3779b97f4a7c15ULL;

    for (size_t i = 0u; i < LOOKBACK_BARS; ++i) {
        double open_value;
        double close_value;
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        open_value = 100.0 + (double)((state >> 33u) % 1000u) / 100.0;
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        close_value = 100.0 + (double)((state >> 33u) % 1000u) / 100.0;
        g_close[i] = close_value;
        g_high[i] = (open_value > close_value ? open_value : close_value) + 0.75;
    }
}

static void set_lookback_bar(cxpr_context* ctx, size_t i) {
    g_lookback_cursor = (int64_t)(i % LOOKBACK_BARS);
    cxpr_context_set(ctx, "close", g_close[g_lookback_cursor]);
    cxpr_context_set(ctx, "high", g_high[g_lookback_cursor]);
}

static void mutate_values(cxpr_context* ctx, size_t i) {
    const double t = (double)(i % 1000) * 0.001;
    cxpr_context_set(ctx, "a", 1.5 + t);
    cxpr_context_set(ctx, "b", 2.5 + t * 2.0);
    cxpr_context_set(ctx, "c", 3.5 + t * 3.0);
    cxpr_context_set(ctx, "d", 4.5 + t * 4.0);
    cxpr_context_set(ctx, "e", 5.5 + t * 5.0);
    cxpr_context_set(ctx, "x", 11.5 - t);
    cxpr_context_set(ctx, "y", 12.5 + t * 0.5);
    cxpr_context_set(ctx, "z", 13.5 - t * 0.25);
}

static void set_base_values_array(cxpr_context* ctx) {
    cxpr_context_set_array(ctx, (cxpr_context_entry[]) {
        {"a", 1.5},
        {"b", 2.5},
        {"c", 3.5},
        {"d", 4.5},
        {"e", 5.5},
        {"f", 6.5},
        {"g", 7.5},
        {"h", 8.5},
        {"i", 9.5},
        {"j", 10.5},
        {"x", 11.5},
        {"y", 12.5},
        {"z", 13.5},
        {"m", 14.5},
        {"n", -15.5},
        {NULL, 0.0}
    });
}

static void mutate_values_array(cxpr_context* ctx, size_t i) {
    const double t = (double)(i % 1000) * 0.001;
    cxpr_context_set_array(ctx, (cxpr_context_entry[]) {
        {"a", 1.5 + t},
        {"b", 2.5 + t * 2.0},
        {"c", 3.5 + t * 3.0},
        {"d", 4.5 + t * 4.0},
        {"e", 5.5 + t * 5.0},
        {"x", 11.5 - t},
        {"y", 12.5 + t * 0.5},
        {"z", 13.5 - t * 0.25},
        {NULL, 0.0}
    });
}

typedef struct {
    cxpr_context_slot a, b, c, d, e, x, y, z;
} churn_slots;

typedef struct {
    unsigned long a, b, c, d, e, x, y, z;
} churn_hashes;

typedef struct {
    unsigned long p1, p2, p3, p4, p5, p6, p7, p8;
} param_hashes;

static churn_hashes make_churn_hashes(void) {
    churn_hashes h;
    h.a = cxpr_hash_string("a");
    h.b = cxpr_hash_string("b");
    h.c = cxpr_hash_string("c");
    h.d = cxpr_hash_string("d");
    h.e = cxpr_hash_string("e");
    h.x = cxpr_hash_string("x");
    h.y = cxpr_hash_string("y");
    h.z = cxpr_hash_string("z");
    return h;
}

static param_hashes make_param_hashes(void) {
    param_hashes h;
    h.p1 = cxpr_hash_string("p1");
    h.p2 = cxpr_hash_string("p2");
    h.p3 = cxpr_hash_string("p3");
    h.p4 = cxpr_hash_string("p4");
    h.p5 = cxpr_hash_string("p5");
    h.p6 = cxpr_hash_string("p6");
    h.p7 = cxpr_hash_string("p7");
    h.p8 = cxpr_hash_string("p8");
    return h;
}

static void mutate_values_prehashed(cxpr_context* ctx, const churn_hashes* h, size_t i) {
    const double t = (double)(i % 1000) * 0.001;
    cxpr_context_set_prehashed(ctx, "a", h->a, 1.5 + t);
    cxpr_context_set_prehashed(ctx, "b", h->b, 2.5 + t * 2.0);
    cxpr_context_set_prehashed(ctx, "c", h->c, 3.5 + t * 3.0);
    cxpr_context_set_prehashed(ctx, "d", h->d, 4.5 + t * 4.0);
    cxpr_context_set_prehashed(ctx, "e", h->e, 5.5 + t * 5.0);
    cxpr_context_set_prehashed(ctx, "x", h->x, 11.5 - t);
    cxpr_context_set_prehashed(ctx, "y", h->y, 12.5 + t * 0.5);
    cxpr_context_set_prehashed(ctx, "z", h->z, 13.5 - t * 0.25);
}

static void mutate_values_slots(churn_slots* s, size_t i) {
    const double t = (double)(i % 1000) * 0.001;
    cxpr_context_slot_set(&s->a, 1.5 + t);
    cxpr_context_slot_set(&s->b, 2.5 + t * 2.0);
    cxpr_context_slot_set(&s->c, 3.5 + t * 3.0);
    cxpr_context_slot_set(&s->d, 4.5 + t * 4.0);
    cxpr_context_slot_set(&s->e, 5.5 + t * 5.0);
    cxpr_context_slot_set(&s->x, 11.5 - t);
    cxpr_context_slot_set(&s->y, 12.5 + t * 0.5);
    cxpr_context_slot_set(&s->z, 13.5 - t * 0.25);
}

static void set_base_params(cxpr_context* ctx) {
    cxpr_context_set_param(ctx, "p1", 1.5);
    cxpr_context_set_param(ctx, "p2", 2.5);
    cxpr_context_set_param(ctx, "p3", 3.5);
    cxpr_context_set_param(ctx, "p4", 4.5);
    cxpr_context_set_param(ctx, "p5", 5.5);
    cxpr_context_set_param(ctx, "p6", 6.5);
    cxpr_context_set_param(ctx, "p7", 7.5);
    cxpr_context_set_param(ctx, "p8", 8.5);
}

static void set_base_params_array(cxpr_context* ctx) {
    cxpr_context_set_param_array(ctx, (cxpr_context_entry[]) {
        {"p1", 1.5},
        {"p2", 2.5},
        {"p3", 3.5},
        {"p4", 4.5},
        {"p5", 5.5},
        {"p6", 6.5},
        {"p7", 7.5},
        {"p8", 8.5},
        {NULL, 0.0}
    });
}

static void mutate_params(cxpr_context* ctx, size_t i) {
    const double t = (double)(i % 1000) * 0.001;
    cxpr_context_set_param(ctx, "p1", 1.5 + t);
    cxpr_context_set_param(ctx, "p2", 2.5 + t * 2.0);
    cxpr_context_set_param(ctx, "p3", 3.5 + t * 3.0);
    cxpr_context_set_param(ctx, "p4", 4.5 + t * 4.0);
    cxpr_context_set_param(ctx, "p5", 5.5 + t * 5.0);
    cxpr_context_set_param(ctx, "p6", 6.5 - t);
    cxpr_context_set_param(ctx, "p7", 7.5 + t * 0.5);
    cxpr_context_set_param(ctx, "p8", 8.5 - t * 0.25);
}

static void mutate_params_array(cxpr_context* ctx, size_t i) {
    const double t = (double)(i % 1000) * 0.001;
    cxpr_context_set_param_array(ctx, (cxpr_context_entry[]) {
        {"p1", 1.5 + t},
        {"p2", 2.5 + t * 2.0},
        {"p3", 3.5 + t * 3.0},
        {"p4", 4.5 + t * 4.0},
        {"p5", 5.5 + t * 5.0},
        {"p6", 6.5 - t},
        {"p7", 7.5 + t * 0.5},
        {"p8", 8.5 - t * 0.25},
        {NULL, 0.0}
    });
}

static void mutate_params_prehashed(cxpr_context* ctx, const param_hashes* h, size_t i) {
    const double t = (double)(i % 1000) * 0.001;
    cxpr_context_set_param_prehashed(ctx, "p1", h->p1, 1.5 + t);
    cxpr_context_set_param_prehashed(ctx, "p2", h->p2, 2.5 + t * 2.0);
    cxpr_context_set_param_prehashed(ctx, "p3", h->p3, 3.5 + t * 3.0);
    cxpr_context_set_param_prehashed(ctx, "p4", h->p4, 4.5 + t * 4.0);
    cxpr_context_set_param_prehashed(ctx, "p5", h->p5, 5.5 + t * 5.0);
    cxpr_context_set_param_prehashed(ctx, "p6", h->p6, 6.5 - t);
    cxpr_context_set_param_prehashed(ctx, "p7", h->p7, 7.5 + t * 0.5);
    cxpr_context_set_param_prehashed(ctx, "p8", h->p8, 8.5 - t * 0.25);
}

static double time_set_base_values(cxpr_context* ctx, size_t iterations) {
    size_t i;
    long long start, end;

    start = now_ns();
    for (i = 0; i < iterations; ++i) {
        set_base_values(ctx);
    }
    end = now_ns();
    return (double)(end - start) / (double)iterations;
}

static double time_set_base_values_array(cxpr_context* ctx, size_t iterations) {
    size_t i;
    long long start, end;

    start = now_ns();
    for (i = 0; i < iterations; ++i) {
        set_base_values_array(ctx);
    }
    end = now_ns();
    return (double)(end - start) / (double)iterations;
}

static double time_mutate_values(cxpr_context* ctx, size_t iterations) {
    size_t i;
    long long start, end;

    start = now_ns();
    for (i = 0; i < iterations; ++i) {
        mutate_values(ctx, i);
    }
    end = now_ns();
    return (double)(end - start) / (double)iterations;
}

static double time_mutate_values_array(cxpr_context* ctx, size_t iterations) {
    size_t i;
    long long start, end;

    start = now_ns();
    for (i = 0; i < iterations; ++i) {
        mutate_values_array(ctx, i);
    }
    end = now_ns();
    return (double)(end - start) / (double)iterations;
}

static double time_mutate_values_prehashed_only(cxpr_context* ctx, size_t iterations) {
    size_t i;
    long long start, end;
    churn_hashes hashes = make_churn_hashes();

    start = now_ns();
    for (i = 0; i < iterations; ++i) {
        mutate_values_prehashed(ctx, &hashes, i);
    }
    end = now_ns();
    return (double)(end - start) / (double)iterations;
}

static double time_mutate_values_slots_only(cxpr_context* ctx, size_t iterations) {
    size_t i;
    long long start, end;
    churn_slots s;

    set_base_values(ctx);
    if (!cxpr_context_slot_bind(ctx, "a", &s.a) ||
        !cxpr_context_slot_bind(ctx, "b", &s.b) ||
        !cxpr_context_slot_bind(ctx, "c", &s.c) ||
        !cxpr_context_slot_bind(ctx, "d", &s.d) ||
        !cxpr_context_slot_bind(ctx, "e", &s.e) ||
        !cxpr_context_slot_bind(ctx, "x", &s.x) ||
        !cxpr_context_slot_bind(ctx, "y", &s.y) ||
        !cxpr_context_slot_bind(ctx, "z", &s.z)) {
        fprintf(stderr, "Slot bind failed\n");
        exit(1);
    }

    start = now_ns();
    for (i = 0; i < iterations; ++i) {
        mutate_values_slots(&s, i);
    }
    end = now_ns();
    return (double)(end - start) / (double)iterations;
}

static double time_set_base_params(cxpr_context* ctx, size_t iterations) {
    size_t i;
    long long start, end;

    start = now_ns();
    for (i = 0; i < iterations; ++i) {
        set_base_params(ctx);
    }
    end = now_ns();
    return (double)(end - start) / (double)iterations;
}

static double time_set_base_params_array(cxpr_context* ctx, size_t iterations) {
    size_t i;
    long long start, end;

    start = now_ns();
    for (i = 0; i < iterations; ++i) {
        set_base_params_array(ctx);
    }
    end = now_ns();
    return (double)(end - start) / (double)iterations;
}

static double time_mutate_params(cxpr_context* ctx, size_t iterations) {
    size_t i;
    long long start, end;

    start = now_ns();
    for (i = 0; i < iterations; ++i) {
        mutate_params(ctx, i);
    }
    end = now_ns();
    return (double)(end - start) / (double)iterations;
}

static double time_mutate_params_array_only(cxpr_context* ctx, size_t iterations) {
    size_t i;
    long long start, end;

    start = now_ns();
    for (i = 0; i < iterations; ++i) {
        mutate_params_array(ctx, i);
    }
    end = now_ns();
    return (double)(end - start) / (double)iterations;
}

static double time_mutate_params_prehashed_only(cxpr_context* ctx, size_t iterations) {
    size_t i;
    long long start, end;
    param_hashes hashes = make_param_hashes();

    start = now_ns();
    for (i = 0; i < iterations; ++i) {
        mutate_params_prehashed(ctx, &hashes, i);
    }
    end = now_ns();
    return (double)(end - start) / (double)iterations;
}

static double time_ast(const cxpr_expr_ast* ast, cxpr_context* ctx, const cxpr_registry* reg,
                       size_t iterations, int mutate_context) {
    size_t i;
    double total = 0.0;
    cxpr_error err = {0};
    churn_hashes hashes = make_churn_hashes();

    for (i = 0; i < iterations; ++i) {
        double value = 0.0;
        if (mutate_context) mutate_values_prehashed(ctx, &hashes, i);
        if (!cxpr_eval_ast_number(ast, ctx, reg, &value, &err)) {
            fprintf(stderr, "AST benchmark eval failed at iter %zu: %s\n", i, err.message);
            exit(1);
        }
        total += value;
    }

    return total;
}

static double time_ir(const cxpr_expr_compiled* program, cxpr_context* ctx, const cxpr_registry* reg,
                      size_t iterations, int mutate_context) {
    size_t i;
    double total = 0.0;
    cxpr_error err = {0};
    churn_hashes hashes = make_churn_hashes();

    for (i = 0; i < iterations; ++i) {
        double value = 0.0;
        if (mutate_context) mutate_values_prehashed(ctx, &hashes, i);
        if (!cxpr_expr_compiled_eval_number(program, ctx, reg, &value, &err)) {
            fprintf(stderr, "IR benchmark eval failed at iter %zu: %s\n", i, err.message);
            exit(1);
        }
        total += value;
    }

    return total;
}

static void fill_inputs_abcde(double* inputs) {
    inputs[0] = 1.5;
    inputs[1] = 2.5;
    inputs[2] = 3.5;
    inputs[3] = 4.5;
    inputs[4] = 5.5;
}

static void fill_inputs_abcdefghi(double* inputs) {
    fill_inputs_abcde(inputs);
    inputs[5] = 6.5;
    inputs[6] = 7.5;
    inputs[7] = 8.5;
    inputs[8] = 9.5;
}

static void fill_inputs_context_churn(double* inputs, size_t i) {
    const double t = (double)(i % 1000) * 0.001;
    inputs[0] = 1.5 + t;
    inputs[1] = 2.5 + t * 2.0;
    inputs[2] = 3.5 + t * 3.0;
    inputs[3] = 4.5 + t * 4.0;
    inputs[4] = 5.5 + t * 5.0;
    inputs[5] = 11.5 - t;
    inputs[6] = 12.5 + t * 0.5;
    inputs[7] = 13.5 - t * 0.25;
}

static double time_c_simple_arith(size_t iterations, double* out_total) {
    cxpr_bench_ir_simple_arith_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_simple_arith_state*, const double*, const double*, double*) =
        cxpr_bench_ir_simple_arith;
    double inputs[5];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    fill_inputs_abcde(inputs);
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_nested_expr(size_t iterations, double* out_total) {
    cxpr_bench_ir_nested_expr_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_nested_expr_state*, const double*, const double*, double*) =
        cxpr_bench_ir_nested_expr;
    double inputs[9];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    fill_inputs_abcdefghi(inputs);
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_function_call(size_t iterations, double* out_total) {
    cxpr_bench_ir_function_call_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_function_call_state*, const double*, const double*, double*) =
        cxpr_bench_ir_function_call;
    double inputs[4];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    inputs[0] = 1.5;
    inputs[1] = 2.5;
    inputs[2] = 3.5;
    inputs[3] = 4.5;
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_defined_fn(size_t iterations, double* out_total) {
    cxpr_bench_ir_defined_fn_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_defined_fn_state*, const double*, const double*, double*) =
        cxpr_bench_ir_defined_fn;
    double inputs[5];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    fill_inputs_abcde(inputs);
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_defined_chain(size_t iterations, double* out_total) {
    cxpr_bench_ir_defined_chain_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_defined_chain_state*, const double*, const double*, double*) =
        cxpr_bench_ir_defined_chain;
    double inputs[7];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    inputs[0] = 1.5;
    inputs[1] = 2.5;
    inputs[2] = 3.5;
    inputs[3] = 4.5;
    inputs[4] = 5.5;
    inputs[5] = 6.5;
    inputs[6] = 7.5;
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_deep_defined(size_t iterations, double* out_total) {
    cxpr_bench_ir_deep_defined_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_deep_defined_state*, const double*, const double*, double*) =
        cxpr_bench_ir_deep_defined;
    double inputs[8];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    inputs[0] = 1.5;
    inputs[1] = 2.5;
    inputs[2] = 3.5;
    inputs[3] = 4.5;
    inputs[4] = 5.5;
    inputs[5] = 6.5;
    inputs[6] = 7.5;
    inputs[7] = 8.5;
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_complex_signal(size_t iterations, double* out_total) {
    cxpr_bench_ir_complex_signal_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_complex_signal_state*, const double*, const double*, double*) =
        cxpr_bench_ir_complex_signal;
    double inputs[14];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    inputs[0] = 1.5;
    inputs[1] = 2.5;
    inputs[2] = 3.5;
    inputs[3] = 4.5;
    inputs[4] = 5.5;
    inputs[5] = 6.5;
    inputs[6] = 7.5;
    inputs[7] = 8.5;
    inputs[8] = 9.5;
    inputs[9] = 11.5;
    inputs[10] = 12.5;
    inputs[11] = 13.5;
    inputs[12] = 14.5;
    inputs[13] = -15.5;
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static void fill_inputs_large(double* inputs) {
    inputs[0] = 1.5;
    inputs[1] = 2.5;
    inputs[2] = 3.5;
    inputs[3] = 4.5;
    inputs[4] = 5.5;
    inputs[5] = 6.5;
    inputs[6] = 7.5;
    inputs[7] = 8.5;
    inputs[8] = 9.5;
    inputs[9] = 10.5;
    inputs[10] = 11.5;
    inputs[11] = 12.5;
    inputs[12] = 13.5;
    inputs[13] = 14.5;
    inputs[14] = -15.5;
}

#define DEFINE_LARGE_C_BENCH(suffix)                                                \
static double time_c_##suffix(size_t iterations, double* out_total) {               \
    cxpr_bench_ir_##suffix##_state state = {0};                                     \
    void (*volatile tick)(cxpr_bench_ir_##suffix##_state*, const double*,           \
                          const double*, double*) = cxpr_bench_ir_##suffix;          \
    double inputs[15];                                                               \
    double outputs[1] = {0};                                                         \
    double total = 0.0;                                                              \
    long long start, end;                                                            \
    fill_inputs_large(inputs);                                                       \
    start = now_ns();                                                                \
    for (size_t i = 0u; i < iterations; ++i) {                                      \
        tick(&state, inputs, NULL, outputs);                                         \
        total += outputs[0];                                                         \
    }                                                                                \
    end = now_ns();                                                                  \
    *out_total = total;                                                              \
    return (double)(end - start) / (double)iterations;                              \
}

DEFINE_LARGE_C_BENCH(large_arith)
DEFINE_LARGE_C_BENCH(large_branch)
DEFINE_LARGE_C_BENCH(large_math)

static void fill_inputs_mixed(double* inputs) {
    inputs[0] = 1.5;
    inputs[1] = 2.5;
    inputs[2] = 3.5;
    inputs[3] = 4.5;
    inputs[4] = 5.5;
    inputs[5] = -15.5;
}

static double time_c_mixed_expr(size_t iterations, double* out_total) {
    cxpr_bench_ir_mixed_expr_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_mixed_expr_state*, const double*, const double*, double*) =
        cxpr_bench_ir_mixed_expr;
    double inputs[6];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    fill_inputs_mixed(inputs);
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_mixed_pipe(size_t iterations, double* out_total) {
    cxpr_bench_ir_mixed_pipe_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_mixed_pipe_state*, const double*, const double*, double*) =
        cxpr_bench_ir_mixed_pipe;
    double inputs[6];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    fill_inputs_mixed(inputs);
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static void fill_inputs_struct_unary(double* inputs) {
    inputs[0] = 2.0;
    inputs[1] = 4.0;
    inputs[2] = 8.0;
}

static void fill_inputs_struct_binary(double* inputs) {
    fill_inputs_struct_unary(inputs);
    inputs[3] = 3.0;
    inputs[4] = 5.0;
    inputs[5] = 7.0;
}

static double time_c_struct_scalar_mul(size_t iterations, double* out_total) {
    cxpr_bench_ir_struct_scalar_mul_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_struct_scalar_mul_state*, const double*, const double*, double*) =
        cxpr_bench_ir_struct_scalar_mul;
    double inputs[3];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    fill_inputs_struct_unary(inputs);
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_scalar_struct_mul(size_t iterations, double* out_total) {
    cxpr_bench_ir_scalar_struct_mul_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_scalar_struct_mul_state*, const double*, const double*, double*) =
        cxpr_bench_ir_scalar_struct_mul;
    double inputs[3];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    fill_inputs_struct_unary(inputs);
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_struct_struct_mul(size_t iterations, double* out_total) {
    cxpr_bench_ir_struct_struct_mul_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_struct_struct_mul_state*, const double*, const double*, double*) =
        cxpr_bench_ir_struct_struct_mul;
    double inputs[6];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    fill_inputs_struct_binary(inputs);
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_struct_struct_add(size_t iterations, double* out_total) {
    cxpr_bench_ir_struct_struct_add_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_struct_struct_add_state*, const double*, const double*, double*) =
        cxpr_bench_ir_struct_struct_add;
    double inputs[6];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    fill_inputs_struct_binary(inputs);
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_struct_scalar_mul_all_fields(size_t iterations, double* out_total) {
    cxpr_bench_ir_struct_scalar_mul_all_fields_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_struct_scalar_mul_all_fields_state*, const double*, const double*, double*) =
        cxpr_bench_ir_struct_scalar_mul_all_fields;
    double inputs[3];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    fill_inputs_struct_unary(inputs);
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_scalar_struct_mul_all_fields(size_t iterations, double* out_total) {
    cxpr_bench_ir_scalar_struct_mul_all_fields_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_scalar_struct_mul_all_fields_state*, const double*, const double*, double*) =
        cxpr_bench_ir_scalar_struct_mul_all_fields;
    double inputs[3];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    fill_inputs_struct_unary(inputs);
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_struct_struct_mul_all_fields(size_t iterations, double* out_total) {
    cxpr_bench_ir_struct_struct_mul_all_fields_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_struct_struct_mul_all_fields_state*, const double*, const double*, double*) =
        cxpr_bench_ir_struct_struct_mul_all_fields;
    double inputs[6];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    fill_inputs_struct_binary(inputs);
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_struct_struct_add_all_fields(size_t iterations, double* out_total) {
    cxpr_bench_ir_struct_struct_add_all_fields_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_struct_struct_add_all_fields_state*, const double*, const double*, double*) =
        cxpr_bench_ir_struct_struct_add_all_fields;
    double inputs[6];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    fill_inputs_struct_binary(inputs);
    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_context_churn(size_t iterations, double* out_total) {
    cxpr_bench_ir_context_churn_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_context_churn_state*, const double*, const double*, double*) =
        cxpr_bench_ir_context_churn;
    double inputs[8];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        fill_inputs_context_churn(inputs, i);
        tick(&state, inputs, NULL, outputs);
        total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_lookback_leaf(size_t iterations, double* out_total) {
    cxpr_bench_ir_lookback_leaf_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_lookback_leaf_state*, const double*, const double*, double*) =
        cxpr_bench_ir_lookback_leaf;
    double inputs[1];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        if (i > 0u && i % LOOKBACK_BARS == 0u) state = (cxpr_bench_ir_lookback_leaf_state){0};
        inputs[0] = g_close[i % LOOKBACK_BARS];
        tick(&state, inputs, NULL, outputs);
        if (isfinite(outputs[0])) total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_lookback_mixed(size_t iterations, double* out_total) {
    cxpr_bench_ir_lookback_mixed_state state = {0};
    void (*volatile tick)(cxpr_bench_ir_lookback_mixed_state*, const double*, const double*, double*) =
        cxpr_bench_ir_lookback_mixed;
    double inputs[2];
    double outputs[1] = {0};
    double total = 0.0;
    long long start, end;

    start = now_ns();
    for (size_t i = 0; i < iterations; ++i) {
        if (i > 0u && i % LOOKBACK_BARS == 0u) state = (cxpr_bench_ir_lookback_mixed_state){0};
        inputs[0] = g_close[i % LOOKBACK_BARS];
        inputs[1] = g_high[i % LOOKBACK_BARS];
        tick(&state, inputs, NULL, outputs);
        if (isfinite(outputs[0])) total += outputs[0];
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static double time_c_model(bench_c_model model, size_t iterations, double* out_total) {
    if (out_total) *out_total = 0.0;
    switch (model) {
    case BENCH_C_SIMPLE_ARITH: return time_c_simple_arith(iterations, out_total);
    case BENCH_C_NESTED_EXPR: return time_c_nested_expr(iterations, out_total);
    case BENCH_C_FUNCTION_CALL: return time_c_function_call(iterations, out_total);
    case BENCH_C_DEFINED_FN: return time_c_defined_fn(iterations, out_total);
    case BENCH_C_DEFINED_CHAIN: return time_c_defined_chain(iterations, out_total);
    case BENCH_C_DEEP_DEFINED: return time_c_deep_defined(iterations, out_total);
    case BENCH_C_COMPLEX_SIGNAL: return time_c_complex_signal(iterations, out_total);
    case BENCH_C_LARGE_ARITH: return time_c_large_arith(iterations, out_total);
    case BENCH_C_LARGE_BRANCH: return time_c_large_branch(iterations, out_total);
    case BENCH_C_LARGE_MATH: return time_c_large_math(iterations, out_total);
    case BENCH_C_MIXED_EXPR: return time_c_mixed_expr(iterations, out_total);
    case BENCH_C_MIXED_PIPE: return time_c_mixed_pipe(iterations, out_total);
    case BENCH_C_CONTEXT_CHURN: return time_c_context_churn(iterations, out_total);
    case BENCH_C_STRUCT_SCALAR_MUL: return time_c_struct_scalar_mul(iterations, out_total);
    case BENCH_C_SCALAR_STRUCT_MUL: return time_c_scalar_struct_mul(iterations, out_total);
    case BENCH_C_STRUCT_STRUCT_MUL: return time_c_struct_struct_mul(iterations, out_total);
    case BENCH_C_STRUCT_STRUCT_ADD: return time_c_struct_struct_add(iterations, out_total);
    case BENCH_C_STRUCT_SCALAR_MUL_ALL_FIELDS: return time_c_struct_scalar_mul_all_fields(iterations, out_total);
    case BENCH_C_SCALAR_STRUCT_MUL_ALL_FIELDS: return time_c_scalar_struct_mul_all_fields(iterations, out_total);
    case BENCH_C_STRUCT_STRUCT_MUL_ALL_FIELDS: return time_c_struct_struct_mul_all_fields(iterations, out_total);
    case BENCH_C_STRUCT_STRUCT_ADD_ALL_FIELDS: return time_c_struct_struct_add_all_fields(iterations, out_total);
    case BENCH_C_LOOKBACK_LEAF: return time_c_lookback_leaf(iterations, out_total);
    case BENCH_C_LOOKBACK_MIXED: return time_c_lookback_mixed(iterations, out_total);
    default: return NAN;
    }
}

static double typed_value_to_double(const cxpr_value* value, const char* field) {
    bool found = false;

    if (value->type == CXPR_VALUE_NUMBER) return value->d;
    if (value->type == CXPR_VALUE_BOOL) return value->b ? 1.0 : 0.0;
    if (value->type != CXPR_VALUE_STRUCT || !field) return NAN;

    for (size_t i = 0; i < value->s->field_count; ++i) {
        if (strcmp(value->s->field_names[i], field) == 0) {
            found = true;
            if (value->s->field_values[i].type == CXPR_VALUE_NUMBER) return value->s->field_values[i].d;
            if (value->s->field_values[i].type == CXPR_VALUE_BOOL) {
                return value->s->field_values[i].b ? 1.0 : 0.0;
            }
            break;
        }
    }

    return found ? NAN : NAN;
}

static double time_ast_typed(const cxpr_expr_ast* ast, cxpr_context* ctx, const cxpr_registry* reg,
                             size_t iterations, const char* field, int free_result) {
    size_t i;
    double total = 0.0;
    cxpr_error err = {0};

    for (i = 0; i < iterations; ++i) {
        cxpr_value value = {0};
        if (!cxpr_eval_ast(ast, ctx, reg, &value, &err)) {
            fprintf(stderr, "Typed AST benchmark eval failed at iter %zu: %s\n", i, err.message);
            exit(1);
        }
        total += typed_value_to_double(&value, field);
        if (free_result) cxpr_value_free(&value);
    }

    return total;
}

static double time_ir_typed(const cxpr_expr_compiled* program, cxpr_context* ctx, const cxpr_registry* reg,
                            size_t iterations, const char* field, int free_result) {
    size_t i;
    double total = 0.0;
    cxpr_error err = {0};

    for (i = 0; i < iterations; ++i) {
        cxpr_value value = {0};
        if (!cxpr_expr_compiled_eval(program, ctx, reg, &value, &err)) {
            fprintf(stderr, "Typed IR benchmark eval failed at iter %zu: %s\n", i, err.message);
            exit(1);
        }
        total += typed_value_to_double(&value, field);
        if (free_result) cxpr_value_free(&value);
    }

    return total;
}

static void validate_ast_vs_ir(const cxpr_expr_ast* ast, const cxpr_expr_compiled* program,
                               cxpr_context* ctx, const cxpr_registry* reg,
                               const bench_case* c) {
    size_t i;
    cxpr_error ast_err = {0};
    cxpr_error ir_err = {0};
    churn_hashes hashes = make_churn_hashes();

    set_base_values(ctx);

    for (i = 0; i < c->iterations; ++i) {
        double ast_value, ir_value;

        if (c->mutate_context) mutate_values_prehashed(ctx, &hashes, i);

        ast_err = (cxpr_error){0};
        ir_err = (cxpr_error){0};
        if (!cxpr_eval_ast_number(ast, ctx, reg, &ast_value, &ast_err)) {
            ast_value = NAN;
        }
        if (!cxpr_expr_compiled_eval_number(program, ctx, reg, &ir_value, &ir_err)) {
            ir_value = NAN;
        }

        if (ast_err.code != ir_err.code) {
            fprintf(stderr,
                    "AST/IR error-code mismatch for '%s' at iter %zu: ast=%d ir=%d\n",
                    c->name, i, ast_err.code, ir_err.code);
            exit(1);
        }

        if (ast_err.code != CXPR_OK) {
            fprintf(stderr,
                    "AST/IR runtime error for '%s' at iter %zu: %s\n",
                    c->name, i, ast_err.message ? ast_err.message : "(null)");
            exit(1);
        }

        if (fabs(ast_value - ir_value) > 1e-9 * (1.0 + fabs(ast_value))) {
            fprintf(stderr,
                    "AST/IR mismatch for '%s' at iter %zu: %.17g vs %.17g\n",
                    c->name, i, ast_value, ir_value);
            exit(1);
        }
    }
}

static void bench_one(cxpr_expr_parser* parser, cxpr_context* ctx, cxpr_registry* reg,
                      const bench_case* c) {
    long long ast_start, ast_end, ir_start, ir_end;
    double ast_total, ir_total, c_total, ast_ns, ir_ns, c_ns;
    cxpr_error err = {0};
    bench_model_source source = load_bench_model(c->fixture);
    const cxpr_expr_ast* ast = source.result;
    cxpr_expr_compiled* program;

    (void)parser;

    program = cxpr_expr_compile(ast, reg, &err);
    if (!program) {
        fprintf(stderr, "Compile failed for '%s': %s\n", c->name, err.message);
        free_bench_model(&source);
        exit(1);
    }

    set_base_values(ctx);
    ast_start = now_ns();
    ast_total = time_ast(ast, ctx, reg, c->iterations, c->mutate_context);
    ast_end = now_ns();

    set_base_values(ctx);
    ir_start = now_ns();
    ir_total = time_ir(program, ctx, reg, c->iterations, c->mutate_context);
    ir_end = now_ns();

    validate_ast_vs_ir(ast, program, ctx, reg, c);

    ast_ns = (double)(ast_end - ast_start) / (double)c->iterations;
    ir_ns = (double)(ir_end - ir_start) / (double)c->iterations;
    c_ns = time_c_model(c->c_model, c->iterations, &c_total);
    if (!isnan(c_ns)) {
        if (fabs(ast_total - c_total) > 1e-9 * (1.0 + fabs(ast_total))) {
            fprintf(stderr, "AST/.cxpr C mismatch for '%s': %.17g vs %.17g\n",
                    c->name, ast_total, c_total);
            exit(1);
        }
        g_sink += c_total;
    }
    g_sink += ast_total + ir_total;

    if (isnan(c_ns)) {
        printf("%-24s  %10zu  %12.2f  %12.2f  %14s  %8.2fx  %8s\n",
               c->name,
               c->iterations,
               ast_ns,
               ir_ns,
               "-",
               ast_ns / ir_ns,
               "-");
    } else {
        printf("%-24s  %10zu  %12.2f  %12.2f  %14.2f  %8.2fx  %8.2fx\n",
               c->name,
               c->iterations,
               ast_ns,
               ir_ns,
               c_ns,
               ast_ns / ir_ns,
               ir_ns / c_ns);
    }

    cxpr_expr_compiled_free(program);
    free_bench_model(&source);
}

static void bench_one_typed(cxpr_expr_parser* parser, cxpr_context* ctx, cxpr_registry* reg,
                            const typed_bench_case* c) {
    long long ast_start, ast_end, ir_start, ir_end;
    double ast_total, ir_total, c_total = 0.0, ast_ns, ir_ns, c_ns = NAN;
    cxpr_error err = {0};
    bench_model_source source = load_bench_model(c->fixture);
    const cxpr_expr_ast* ast = source.result;
    cxpr_expr_compiled* program;

    (void)parser;

    program = cxpr_expr_compile(ast, reg, &err);
    if (!program) {
        fprintf(stderr, "Compile failed for '%s': %s\n", c->name, err.message);
        free_bench_model(&source);
        exit(1);
    }

    set_base_values(ctx);
    set_base_struct_values(ctx);
    ast_start = now_ns();
    ast_total = time_ast_typed(ast, ctx, reg, c->iterations, c->field, c->free_result);
    ast_end = now_ns();

    set_base_values(ctx);
    set_base_struct_values(ctx);
    ir_start = now_ns();
    ir_total = time_ir_typed(program, ctx, reg, c->iterations, c->field, c->free_result);
    ir_end = now_ns();

    if (fabs(ast_total - ir_total) > 1e-9 * (1.0 + fabs(ast_total))) {
        fprintf(stderr, "Typed AST/IR mismatch for '%s': %.17g vs %.17g\n",
                c->name, ast_total, ir_total);
        exit(1);
    }

    ast_ns = (double)(ast_end - ast_start) / (double)c->iterations;
    ir_ns = (double)(ir_end - ir_start) / (double)c->iterations;
    g_sink += ast_total + ir_total;

    if (c->c_model != BENCH_C_NONE) {
        c_ns = time_c_model(c->c_model, c->iterations, &c_total);
        if (fabs(ast_total - c_total) > 1e-9 * (1.0 + fabs(ast_total))) {
            fprintf(stderr, "Typed AST/.cxpr C mismatch for '%s': %.17g vs %.17g\n",
                    c->name, ast_total, c_total);
            exit(1);
        }
        g_sink += c_total;
        printf("%-24s  %10zu  %12.2f  %12.2f  %14.2f  %8.2fx  %8.2fx\n",
               c->name,
               c->iterations,
               ast_ns,
               ir_ns,
               c_ns,
               ast_ns / ir_ns,
               ir_ns / c_ns);
    } else {
        printf("%-24s  %10zu  %12.2f  %12.2f  %14s  %8.2fx  %8s\n",
               c->name,
               c->iterations,
               ast_ns,
               ir_ns,
               "-",
               ast_ns / ir_ns,
               "-");
    }

    cxpr_expr_compiled_free(program);
    free_bench_model(&source);
}

static double time_ast_lookback(const cxpr_expr_ast* ast, cxpr_context* ctx,
                                const cxpr_registry* reg, size_t iterations) {
    size_t i;
    double total = 0.0;
    cxpr_error err = {0};

    for (i = 0; i < iterations; ++i) {
        double value = 0.0;
        set_lookback_bar(ctx, i);
        if (!cxpr_eval_ast_number(ast, ctx, reg, &value, &err)) {
            fprintf(stderr, "AST lookback benchmark eval failed at iter %zu: %s\n",
                    i, err.message ? err.message : "(null)");
            exit(1);
        }
        if (isfinite(value)) total += value;
    }

    return total;
}

static double time_ir_lookback(const cxpr_expr_compiled* program, cxpr_context* ctx,
                               const cxpr_registry* reg, size_t iterations) {
    size_t i;
    double total = 0.0;
    cxpr_error err = {0};

    for (i = 0; i < iterations; ++i) {
        double value = 0.0;
        set_lookback_bar(ctx, i);
        if (!cxpr_expr_compiled_eval_number(program, ctx, reg, &value, &err)) {
            fprintf(stderr, "IR lookback benchmark eval failed at iter %zu: %s\n",
                    i, err.message ? err.message : "(null)");
            exit(1);
        }
        if (isfinite(value)) total += value;
    }

    return total;
}

static void validate_lookback_ast_vs_ir(const cxpr_expr_ast* ast, const cxpr_expr_compiled* program,
                                        cxpr_context* ctx, const cxpr_registry* reg,
                                        const lookback_bench_case* c) {
    cxpr_error ast_err = {0};
    cxpr_error ir_err = {0};

    for (size_t i = 0u; i < LOOKBACK_BARS; ++i) {
        double ast_value = NAN;
        double ir_value = NAN;
        set_lookback_bar(ctx, i);
        if (!cxpr_eval_ast_number(ast, ctx, reg, &ast_value, &ast_err)) ast_value = NAN;
        if (!cxpr_expr_compiled_eval_number(program, ctx, reg, &ir_value, &ir_err)) ir_value = NAN;
        if (ast_err.code != ir_err.code) {
            fprintf(stderr,
                    "Lookback AST/IR error-code mismatch for '%s' at bar %zu: ast=%d ir=%d\n",
                    c->name, i, ast_err.code, ir_err.code);
            exit(1);
        }
        if (ast_err.code != CXPR_OK) {
            fprintf(stderr,
                    "Lookback AST/IR runtime error for '%s' at bar %zu: %s\n",
                    c->name, i, ast_err.message ? ast_err.message : "(null)");
            exit(1);
        }
        if ((isnan(ast_value) && isnan(ir_value)) ||
            fabs(ast_value - ir_value) <= 1e-9 * (1.0 + fabs(ast_value))) {
            ast_err = (cxpr_error){0};
            ir_err = (cxpr_error){0};
            continue;
        }
        fprintf(stderr,
                "Lookback AST/IR mismatch for '%s' at bar %zu: %.17g vs %.17g\n",
                c->name, i, ast_value, ir_value);
        exit(1);
    }
}

static void bench_one_lookback(cxpr_expr_parser* parser, cxpr_context* ctx, cxpr_registry* reg,
                               const lookback_bench_case* c) {
    long long ast_start, ast_end, ir_start, ir_end;
    double ast_total, ir_total, c_total, ast_ns, ir_ns, c_ns;
    cxpr_error err = {0};
    bench_model_source source = load_bench_model(c->fixture);
    const cxpr_expr_ast* ast = source.result;
    cxpr_expr_compiled* program;

    (void)parser;
    program = cxpr_expr_compile(ast, reg, &err);
    if (!program) {
        fprintf(stderr, "Compile failed for lookback '%s': %s\n", c->name, err.message);
        free_bench_model(&source);
        exit(1);
    }

    ast_start = now_ns();
    ast_total = time_ast_lookback(ast, ctx, reg, c->iterations);
    ast_end = now_ns();

    ir_start = now_ns();
    ir_total = time_ir_lookback(program, ctx, reg, c->iterations);
    ir_end = now_ns();

    validate_lookback_ast_vs_ir(ast, program, ctx, reg, c);

    ast_ns = (double)(ast_end - ast_start) / (double)c->iterations;
    ir_ns = (double)(ir_end - ir_start) / (double)c->iterations;
    c_ns = time_c_model(c->c_model, c->iterations, &c_total);
    if (!isnan(c_ns)) {
        if (fabs(ast_total - c_total) > 1e-9 * (1.0 + fabs(ast_total))) {
            fprintf(stderr, "Lookback AST/.cxpr C mismatch for '%s': %.17g vs %.17g\n",
                    c->name, ast_total, c_total);
            exit(1);
        }
        g_sink += c_total;
    }
    g_sink += ast_total + ir_total;

    if (isnan(c_ns)) {
        printf("%-24s  %10zu  %12.2f  %12.2f  %14s  %8.2fx  %8s\n",
               c->name,
               c->iterations,
               ast_ns,
               ir_ns,
               "-",
               ast_ns / ir_ns,
               "-");
    } else {
        printf("%-24s  %10zu  %12.2f  %12.2f  %14.2f  %8.2fx  %8.2fx\n",
               c->name,
               c->iterations,
               ast_ns,
               ir_ns,
               c_ns,
               ast_ns / ir_ns,
               ir_ns / c_ns);
    }

    cxpr_expr_compiled_free(program);
    free_bench_model(&source);
}

static void bench_slot_churn(cxpr_expr_parser* parser, cxpr_context* ctx, cxpr_registry* reg) {
    const char* expr = "a + b * c - d / e + x * y - z";
    const size_t iterations = 200000;
    long long churn_start, churn_end;
    double churn_total, churn_ns;
    churn_slots s;
    size_t i;
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, expr, &err);
    cxpr_expr_compiled* program;

    if (!ast) { fprintf(stderr, "Parse failed: %s\n", err.message); exit(1); }
    program = cxpr_expr_compile(ast, reg, &err);
    if (!program) { fprintf(stderr, "Compile failed: %s\n", err.message); exit(1); }

    set_base_values(ctx);
    if (!cxpr_context_slot_bind(ctx, "a", &s.a) ||
        !cxpr_context_slot_bind(ctx, "b", &s.b) ||
        !cxpr_context_slot_bind(ctx, "c", &s.c) ||
        !cxpr_context_slot_bind(ctx, "d", &s.d) ||
        !cxpr_context_slot_bind(ctx, "e", &s.e) ||
        !cxpr_context_slot_bind(ctx, "x", &s.x) ||
        !cxpr_context_slot_bind(ctx, "y", &s.y) ||
        !cxpr_context_slot_bind(ctx, "z", &s.z)) {
        fprintf(stderr, "Slot bind failed\n"); exit(1);
    }

    churn_start = now_ns();
    churn_total = 0.0;
    for (i = 0; i < iterations; ++i) {
        double value = 0.0;
        mutate_values_slots(&s, i);
        if (!cxpr_expr_compiled_eval_number(program, ctx, reg, &value, &err)) {
            fprintf(stderr, "Slot benchmark eval failed: %s\n", err.message); exit(1);
        }
        churn_total += value;
    }
    churn_end = now_ns();

    churn_ns = (double)(churn_end - churn_start) / (double)iterations;
    g_sink += churn_total;

    printf("%-24s  %10zu  %12s  %12.2f  %14s  %8s  %8s\n",
           "context_slot", iterations, "-", churn_ns, "-", "-", "-");

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
}

static void bench_context_update_paths(cxpr_context* ctx) {
    const size_t iterations = 500000;
    double set_ns;
    double array_ns;
    double mutate_set_ns;
    double mutate_array_ns;
    double mutate_prehashed_ns;
    double mutate_slot_ns;

    set_ns = time_set_base_values(ctx, iterations);
    array_ns = time_set_base_values_array(ctx, iterations);
    mutate_set_ns = time_mutate_values(ctx, iterations);
    mutate_array_ns = time_mutate_values_array(ctx, iterations);
    mutate_prehashed_ns = time_mutate_values_prehashed_only(ctx, iterations);
    mutate_slot_ns = time_mutate_values_slots_only(ctx, iterations);

    printf("%-24s  %10s  %12s  %12s  %8s\n",
           "case", "iters", "set ns/op", "alt ns/op", "speedup");
    printf("%-24s  %10zu  %12.2f  %12.2f  %8.2fx\n",
           "base_array", iterations, set_ns, array_ns, set_ns / array_ns);
    printf("%-24s  %10zu  %12.2f  %12.2f  %8.2fx\n",
           "mutate_array", iterations, mutate_set_ns, mutate_array_ns, mutate_set_ns / mutate_array_ns);
    printf("%-24s  %10zu  %12.2f  %12.2f  %8.2fx\n",
           "mutate_prehashed", iterations, mutate_set_ns, mutate_prehashed_ns, mutate_set_ns / mutate_prehashed_ns);
    printf("%-24s  %10zu  %12.2f  %12.2f  %8.2fx\n",
           "mutate_slot", iterations, mutate_set_ns, mutate_slot_ns, mutate_set_ns / mutate_slot_ns);
}

static void bench_param_update_paths(cxpr_context* ctx) {
    const size_t iterations = 500000;
    double set_ns;
    double array_ns;
    double mutate_set_ns;
    double mutate_array_ns;
    double mutate_prehashed_ns;

    set_ns = time_set_base_params(ctx, iterations);
    array_ns = time_set_base_params_array(ctx, iterations);
    mutate_set_ns = time_mutate_params(ctx, iterations);
    mutate_array_ns = time_mutate_params_array_only(ctx, iterations);
    mutate_prehashed_ns = time_mutate_params_prehashed_only(ctx, iterations);

    printf("%-24s  %10s  %12s  %12s  %8s\n",
           "case", "iters", "set ns/op", "alt ns/op", "speedup");
    printf("%-24s  %10zu  %12.2f  %12.2f  %8.2fx\n",
           "base_param_array", iterations, set_ns, array_ns, set_ns / array_ns);
    printf("%-24s  %10zu  %12.2f  %12.2f  %8.2fx\n",
           "mutate_param_array", iterations, mutate_set_ns, mutate_array_ns, mutate_set_ns / mutate_array_ns);
    printf("%-24s  %10zu  %12.2f  %12.2f  %8.2fx\n",
           "mutate_param_hash", iterations, mutate_set_ns, mutate_prehashed_ns, mutate_set_ns / mutate_prehashed_ns);
}

static double time_context_get_path(const cxpr_context* ctx, const char* name,
                                    size_t iterations) {
    size_t i;
    bool found = false;
    double total = 0.0;
    long long start, end;

    start = now_ns();
    for (i = 0; i < iterations; ++i) {
        total += cxpr_context_get(ctx, name, &found);
        if (!found) {
            fprintf(stderr, "Context overlay benchmark missed '%s'\n", name);
            exit(1);
        }
    }
    end = now_ns();
    g_sink += total;
    return (double)(end - start) / (double)iterations;
}

static double time_context_overlay_alloc_free(const cxpr_context* parent,
                                              size_t iterations) {
    size_t i;
    bool found = false;
    double total = 0.0;
    long long start, end;

    start = now_ns();
    for (i = 0; i < iterations; ++i) {
        cxpr_context* overlay = cxpr_context_overlay_new(parent);
        if (!overlay) {
            fprintf(stderr, "Context overlay allocation failed\n");
            exit(1);
        }
        total += cxpr_context_get(overlay, "a", &found);
        cxpr_context_free(overlay);
        if (!found) {
            fprintf(stderr, "Context overlay alloc/free benchmark missed parent value\n");
            exit(1);
        }
    }
    end = now_ns();
    g_sink += total;
    return (double)(end - start) / (double)iterations;
}

static double time_expression_number(const cxpr_expr_compiled* program, cxpr_context* ctx,
                                     const cxpr_registry* reg, size_t iterations,
                                     double* out_total) {
    size_t i;
    double total = 0.0;
    cxpr_error err = {0};
    long long start, end;

    start = now_ns();
    for (i = 0; i < iterations; ++i) {
        double value = 0.0;
        if (!cxpr_expr_compiled_eval_number(program, ctx, reg, &value, &err)) {
            fprintf(stderr, "Overlay expression benchmark failed at iter %zu: %s\n",
                    i, err.message);
            exit(1);
        }
        total += value;
    }
    end = now_ns();
    *out_total = total;
    return (double)(end - start) / (double)iterations;
}

static void bench_defined_overlay_prefix(cxpr_expr_parser* parser, cxpr_registry* reg) {
    const char* expr = "pickx(src)";
    const size_t iterations = 200000;
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_expr_compiled* program;
    double total = 0.0;
    double ns;

    if (!ctx) {
        fprintf(stderr, "Failed to allocate overlay benchmark context\n");
        exit(1);
    }
    cxpr_context_set(ctx, "src.x", 42.0);

    ast = cxpr_expr_ast_parse(parser, expr, &err);
    if (!ast) {
        fprintf(stderr, "Parse failed for defined_overlay_prefix: %s\n", err.message);
        exit(1);
    }
    program = cxpr_expr_compile(ast, reg, &err);
    if (!program) {
        fprintf(stderr, "Compile failed for defined_overlay_prefix: %s\n", err.message);
        exit(1);
    }

    ns = time_expression_number(program, ctx, reg, iterations, &total);
    if (fabs(total - (42.0 * (double)iterations)) > 1e-9 * total) {
        fprintf(stderr, "defined_overlay_prefix output mismatch: %.17g\n", total);
        exit(1);
    }
    g_sink += total;

    printf("%-24s  %10zu  %14.2f  %12.6f\n",
           "defined_prefix", iterations, ns, total / (double)iterations);

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(ctx);
}

static void bench_context_overlay_paths(void) {
    const size_t lookup_iterations = 1000000;
    const size_t alloc_iterations = 200000;
    cxpr_context* parent = cxpr_context_new();
    cxpr_context* fallback = NULL;
    cxpr_context* override = NULL;
    double direct_ns;
    double fallback_ns;
    double override_ns;
    double alloc_ns;

    if (!parent) {
        fprintf(stderr, "Failed to allocate context overlay benchmark parent\n");
        exit(1);
    }
    cxpr_context_set(parent, "a", 11.5);

    fallback = cxpr_context_overlay_new(parent);
    override = cxpr_context_overlay_new(parent);
    if (!fallback || !override) {
        fprintf(stderr, "Failed to allocate context overlays\n");
        exit(1);
    }
    cxpr_context_set(override, "a", 21.5);

    direct_ns = time_context_get_path(parent, "a", lookup_iterations);
    fallback_ns = time_context_get_path(fallback, "a", lookup_iterations);
    override_ns = time_context_get_path(override, "a", lookup_iterations);
    alloc_ns = time_context_overlay_alloc_free(parent, alloc_iterations);

    printf("%-24s  %10s  %14s  %12s\n",
           "case", "iters", "ns/op", "output");
    printf("%-24s  %10zu  %14.2f  %12.6f\n",
           "parent_get", lookup_iterations, direct_ns, 11.5);
    printf("%-24s  %10zu  %14.2f  %12.6f\n",
           "overlay_fallback_get", lookup_iterations, fallback_ns, 11.5);
    printf("%-24s  %10zu  %14.2f  %12.6f\n",
           "overlay_override_get", lookup_iterations, override_ns, 21.5);
    printf("%-24s  %10zu  %14.2f  %12.6f\n",
           "overlay_alloc_free_get", alloc_iterations, alloc_ns, 11.5);

    cxpr_context_free(override);
    cxpr_context_free(fallback);
    cxpr_context_free(parent);
}

static void print_bench_header(const char* title) {
    printf("\n%s\n", title);
    printf("%-24s  %10s  %12s  %12s  %14s  %8s  %8s\n",
           "case", "iters", "AST ns/eval", "IR ns/eval", ".cxpr C ns/eval", "AST/IR", "IR/C");
}

static int print_generated_c(const bench_case* cases, size_t case_count,
                             const typed_bench_case* typed_cases, size_t typed_case_count,
                             const lookback_bench_case* lookback_cases, size_t lookback_case_count,
                             const char* filter) {
    size_t i;
    int matched = filter ? 0 : 1;
    for (i = 0; i < case_count; ++i) {
        if (print_generated_c_case(cases[i].name, cases[i].fixture, cases[i].c_model, filter, &matched)) return 1;
    }
    for (i = 0; i < typed_case_count; ++i) {
        if (print_generated_c_case(typed_cases[i].name, typed_cases[i].fixture, typed_cases[i].c_model, filter, &matched)) return 1;
    }
    for (i = 0; i < lookback_case_count; ++i) {
        if (print_generated_c_case(lookback_cases[i].name, lookback_cases[i].fixture, lookback_cases[i].c_model, filter, &matched)) return 1;
    }
    if (!matched) {
        fprintf(stderr, "Unknown benchmark case '%s'\n", filter);
        return 1;
    }
    return 0;
}

static void print_usage(const char* argv0) {
    printf("usage: %s [--print-c [case]]\n", argv0);
    printf("  --print-c [case]   print generated .cxpr C include files and exit\n");
}

int main(int argc, char** argv) {
    const bench_case cases[] = {
        { "simple_arith", "ir_simple_arith.cxpr", 500000, 0, BENCH_C_SIMPLE_ARITH },
        { "nested_expr", "ir_nested_expr.cxpr", 400000, 0, BENCH_C_NESTED_EXPR },
        { "function_call", "ir_function_call.cxpr", 250000, 0, BENCH_C_FUNCTION_CALL },
        { "defined_fn", "ir_defined_fn.cxpr", 200000, 0, BENCH_C_DEFINED_FN },
        { "defined_chain", "ir_defined_chain.cxpr", 120000, 0, BENCH_C_DEFINED_CHAIN },
        { "deep_defined", "ir_deep_defined.cxpr", 80000, 0, BENCH_C_DEEP_DEFINED },
        { "complex_signal", "ir_complex_signal.cxpr", 80000, 0, BENCH_C_COMPLEX_SIGNAL },
        { "large_arith", "ir_large_arith.cxpr", 60000, 0, BENCH_C_LARGE_ARITH },
        { "large_branch", "ir_large_branch.cxpr", 60000, 0, BENCH_C_LARGE_BRANCH },
        { "large_math", "ir_large_math.cxpr", 400000, 0, BENCH_C_LARGE_MATH },
        { "mixed_expr", "ir_mixed_expr.cxpr", 120000, 0, BENCH_C_MIXED_EXPR },
        { "mixed_pipe", "ir_mixed_pipe.cxpr", 120000, 0, BENCH_C_MIXED_PIPE },
        { "context_churn", "ir_context_churn.cxpr", 200000, 1, BENCH_C_CONTEXT_CHURN },
    };
    const typed_bench_case typed_cases[] = {
        { "struct_scalar_mul", "ir_struct_scalar_mul.cxpr", 120000, NULL, 0, BENCH_C_STRUCT_SCALAR_MUL },
        { "scalar_struct_mul", "ir_scalar_struct_mul.cxpr", 120000, NULL, 0, BENCH_C_SCALAR_STRUCT_MUL },
        { "struct_struct_mul", "ir_struct_struct_mul.cxpr", 100000, NULL, 0, BENCH_C_STRUCT_STRUCT_MUL },
        { "struct_struct_add", "ir_struct_struct_add.cxpr", 100000, NULL, 0, BENCH_C_STRUCT_STRUCT_ADD },
        { "struct_scalar_mul_all", "ir_struct_scalar_mul_all_fields.cxpr", 100000, NULL, 0, BENCH_C_STRUCT_SCALAR_MUL_ALL_FIELDS },
        { "scalar_struct_mul_all", "ir_scalar_struct_mul_all_fields.cxpr", 100000, NULL, 0, BENCH_C_SCALAR_STRUCT_MUL_ALL_FIELDS },
        { "struct_struct_mul_all", "ir_struct_struct_mul_all_fields.cxpr", 80000, NULL, 0, BENCH_C_STRUCT_STRUCT_MUL_ALL_FIELDS },
        { "struct_struct_add_all", "ir_struct_struct_add_all_fields.cxpr", 80000, NULL, 0, BENCH_C_STRUCT_STRUCT_ADD_ALL_FIELDS },
    };
    const lookback_bench_case lookback_cases[] = {
        { "lookback_leaf", "ir_lookback_leaf.cxpr", 250000, BENCH_C_LOOKBACK_LEAF },
        { "lookback_mixed", "ir_lookback_mixed.cxpr", 200000, BENCH_C_LOOKBACK_MIXED },
    };
    const size_t case_count = sizeof(cases) / sizeof(cases[0]);
    const size_t typed_case_count = sizeof(typed_cases) / sizeof(typed_cases[0]);
    const size_t lookback_case_count = sizeof(lookback_cases) / sizeof(lookback_cases[0]);
    size_t i;
    cxpr_error err = {0};
    cxpr_expr_parser* parser;
    cxpr_context* ctx;
    cxpr_registry* reg;

    if (argc > 3 || (argc >= 2 && strcmp(argv[1], "--print-c") != 0 && strcmp(argv[1], "--help") != 0)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--print-c") == 0) {
        return print_generated_c(cases, case_count, typed_cases, typed_case_count,
                                 lookback_cases, lookback_case_count, argc == 3 ? argv[2] : NULL);
    }

    parser = cxpr_expr_parser_new();
    ctx = cxpr_context_new();
    reg = cxpr_registry_new();

    if (!parser || !ctx || !reg) {
        fprintf(stderr, "Failed to initialize benchmark state\n");
        return 1;
    }
    cxpr_register_defaults(reg);
    cxpr_registry_add_unary(reg, "native_sq", native_sq);
    cxpr_registry_add_binary(reg, "native_hyp2", native_hyp2);
    cxpr_registry_add_ternary(reg, "native_f3", native_f3);
    cxpr_registry_add(reg, "native_f5", native_f5_adapter, 4, 4, NULL, NULL);
    cxpr_registry_add_value(reg, "bench_tf", bench_tf_value_fn, 1, 1, NULL, NULL);
    cxpr_registry_add_ast_handler(reg, "bench_tf", bench_tf_ast_handler_fn, 1, 2, NULL, NULL);
    {
        const char* macd_fields[] = {"line", "signal", "histogram"};
        cxpr_registry_add_struct(reg, "macd", bench_macd, 3, 3, macd_fields, 3, NULL, NULL);
    }
    {
        const cxpr_lookback_column columns[] = {
            { "close", &g_close[0], sizeof(g_close[0]), LOOKBACK_BARS },
            { "high", &g_high[0], sizeof(g_high[0]), LOOKBACK_BARS },
        };
        init_lookback_bars();
        if (!cxpr_register_column_lookback(reg, columns, 2u, &g_lookback_cursor)) {
            fprintf(stderr, "Failed to register lookback columns\n");
            cxpr_registry_free(reg);
            cxpr_context_free(ctx);
            cxpr_expr_parser_free(parser);
            return 1;
        }
    }

    err = cxpr_registry_define_fn(reg, "sq(x) => x * x");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Failed to define sq: %s\n", err.message);
        cxpr_registry_free(reg);
        cxpr_context_free(ctx);
        cxpr_expr_parser_free(parser);
        return 1;
    }

    err = cxpr_registry_define_fn(reg, "hyp2(x, y) => sqrt(sq(x) + sq(y))");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Failed to define hyp2: %s\n", err.message);
        cxpr_registry_free(reg);
        cxpr_context_free(ctx);
        cxpr_expr_parser_free(parser);
        return 1;
    }

    err = cxpr_registry_define_fn(reg, "f3(x, y, z) => sqrt(hyp2(x, y) + sq(z))");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Failed to define f3: %s\n", err.message);
        cxpr_registry_free(reg);
        cxpr_context_free(ctx);
        cxpr_expr_parser_free(parser);
        return 1;
    }

    err = cxpr_registry_define_fn(reg, "f5(a, b, c, d) => sqrt((a*a + b*b) + (c*c + d*d))");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Failed to define f5: %s\n", err.message);
        cxpr_registry_free(reg);
        cxpr_context_free(ctx);
        cxpr_expr_parser_free(parser);
        return 1;
    }

    err = cxpr_registry_define_fn(reg, "add(x, y) => x + y");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Failed to define add: %s\n", err.message);
        cxpr_registry_free(reg);
        cxpr_context_free(ctx);
        cxpr_expr_parser_free(parser);
        return 1;
    }

    err = cxpr_registry_define_fn(reg, "div(x, y) => x / y");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Failed to define div: %s\n", err.message);
        cxpr_registry_free(reg);
        cxpr_context_free(ctx);
        cxpr_expr_parser_free(parser);
        return 1;
    }

    err = cxpr_registry_define_fn(reg, "clamp(x, lo, hi) => x < lo ? lo : (x > hi ? hi : x)");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Failed to define clamp: %s\n", err.message);
        cxpr_registry_free(reg);
        cxpr_context_free(ctx);
        cxpr_expr_parser_free(parser);
        return 1;
    }

    err = cxpr_registry_define_fn(reg, "pickx(v) => v.x");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Failed to define pickx: %s\n", err.message);
        cxpr_registry_free(reg);
        cxpr_context_free(ctx);
        cxpr_expr_parser_free(parser);
        return 1;
    }

    printf("cxpr generated C vs AST vs IR benchmark (.cxpr source)\n");

    print_bench_header("Scalar");
    for (i = 0; i < case_count; ++i) {
        bench_one(parser, ctx, reg, &cases[i]);
    }

    print_bench_header("Typed Struct");
    for (i = 0; i < typed_case_count; ++i) {
        bench_one_typed(parser, ctx, reg, &typed_cases[i]);
    }

    print_bench_header("Lookback");
    for (i = 0; i < lookback_case_count; ++i) {
        bench_one_lookback(parser, ctx, reg, &lookback_cases[i]);
    }

    print_bench_header("IR-only");
    bench_slot_churn(parser, ctx, reg);

    printf("\nContext Update Paths\n");
    bench_context_update_paths(ctx);

    printf("\nParam Update Paths\n");
    bench_param_update_paths(ctx);

    printf("\nOverlay Paths\n");
    bench_context_overlay_paths();
    bench_defined_overlay_prefix(parser, reg);

    printf("sink=%.6f\n", g_sink);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_parser_free(parser);
    return 0;
}
