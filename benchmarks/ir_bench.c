#include <cxpr/cxpr.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    const char* name;
    const char* expr;
    size_t iterations;
    int mutate_context;
} bench_case;

typedef struct {
    const char* name;
    const char* expr;
    size_t iterations;
    const char* field;
} typed_bench_case;

static volatile double g_sink = 0.0;

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
        out[0] = cxpr_fv_double(NAN);
        out[1] = cxpr_fv_double(NAN);
        out[2] = cxpr_fv_double(NAN);
        return;
    }

    out[0] = cxpr_fv_double((args[0] - args[1]) * 0.1);
    out[1] = cxpr_fv_double(args[2] * 0.25);
    out[2] = cxpr_fv_double(out[0].d - out[1].d);
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

static double time_ast(const cxpr_ast* ast, cxpr_context* ctx, const cxpr_registry* reg,
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

static double time_ir(const cxpr_program* program, cxpr_context* ctx, const cxpr_registry* reg,
                      size_t iterations, int mutate_context) {
    size_t i;
    double total = 0.0;
    cxpr_error err = {0};
    churn_hashes hashes = make_churn_hashes();

    for (i = 0; i < iterations; ++i) {
        double value = 0.0;
        if (mutate_context) mutate_values_prehashed(ctx, &hashes, i);
        if (!cxpr_eval_program_number(program, ctx, reg, &value, &err)) {
            fprintf(stderr, "IR benchmark eval failed at iter %zu: %s\n", i, err.message);
            exit(1);
        }
        total += value;
    }

    return total;
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

static double time_ast_typed(const cxpr_ast* ast, cxpr_context* ctx, const cxpr_registry* reg,
                             size_t iterations, const char* field) {
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
    }

    return total;
}

static double time_ir_typed(const cxpr_program* program, cxpr_context* ctx, const cxpr_registry* reg,
                            size_t iterations, const char* field) {
    size_t i;
    double total = 0.0;
    cxpr_error err = {0};

    for (i = 0; i < iterations; ++i) {
        cxpr_value value = {0};
        if (!cxpr_eval_program(program, ctx, reg, &value, &err)) {
            fprintf(stderr, "Typed IR benchmark eval failed at iter %zu: %s\n", i, err.message);
            exit(1);
        }
        total += typed_value_to_double(&value, field);
    }

    return total;
}

static void validate_ast_vs_ir(const cxpr_ast* ast, const cxpr_program* program,
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
        if (!cxpr_eval_program_number(program, ctx, reg, &ir_value, &ir_err)) {
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

static void bench_one(cxpr_parser* parser, cxpr_context* ctx, cxpr_registry* reg,
                      const bench_case* c) {
    long long ast_start, ast_end, ir_start, ir_end;
    double ast_total, ir_total, ast_ns, ir_ns;
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser, c->expr, &err);
    cxpr_program* program;

    if (!ast) {
        fprintf(stderr, "Parse failed for '%s': %s\n", c->name, err.message);
        exit(1);
    }

    program = cxpr_compile(ast, reg, &err);
    if (!program) {
        fprintf(stderr, "Compile failed for '%s': %s\n", c->name, err.message);
        cxpr_ast_free(ast);
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
    g_sink += ast_total + ir_total;

    printf("%-18s  %10zu  %12.2f  %12.2f  %8.2fx\n",
           c->name,
           c->iterations,
           ast_ns,
           ir_ns,
           ast_ns / ir_ns);

    cxpr_program_free(program);
    cxpr_ast_free(ast);
}

static void bench_one_typed(cxpr_parser* parser, cxpr_context* ctx, cxpr_registry* reg,
                            const typed_bench_case* c) {
    long long ast_start, ast_end, ir_start, ir_end;
    double ast_total, ir_total, ast_ns, ir_ns;
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser, c->expr, &err);
    cxpr_program* program;

    if (!ast) {
        fprintf(stderr, "Parse failed for '%s': %s\n", c->name, err.message);
        exit(1);
    }

    program = cxpr_compile(ast, reg, &err);
    if (!program) {
        fprintf(stderr, "Compile failed for '%s': %s\n", c->name, err.message);
        cxpr_ast_free(ast);
        exit(1);
    }

    set_base_values(ctx);
    ast_start = now_ns();
    ast_total = time_ast_typed(ast, ctx, reg, c->iterations, c->field);
    ast_end = now_ns();

    set_base_values(ctx);
    ir_start = now_ns();
    ir_total = time_ir_typed(program, ctx, reg, c->iterations, c->field);
    ir_end = now_ns();

    if (fabs(ast_total - ir_total) > 1e-9 * (1.0 + fabs(ast_total))) {
        fprintf(stderr, "Typed AST/IR mismatch for '%s': %.17g vs %.17g\n",
                c->name, ast_total, ir_total);
        exit(1);
    }

    ast_ns = (double)(ast_end - ast_start) / (double)c->iterations;
    ir_ns = (double)(ir_end - ir_start) / (double)c->iterations;
    g_sink += ast_total + ir_total;

    printf("%-18s  %10zu  %12.2f  %12.2f  %8.2fx\n",
           c->name,
           c->iterations,
           ast_ns,
           ir_ns,
           ast_ns / ir_ns);

    cxpr_program_free(program);
    cxpr_ast_free(ast);
}

static void bench_slot_churn(cxpr_parser* parser, cxpr_context* ctx, cxpr_registry* reg) {
    const char* expr = "a + b * c - d / e + x * y - z";
    const size_t iterations = 200000;
    long long churn_start, churn_end;
    double churn_total, churn_ns;
    churn_slots s;
    size_t i;
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser, expr, &err);
    cxpr_program* program;

    if (!ast) { fprintf(stderr, "Parse failed: %s\n", err.message); exit(1); }
    program = cxpr_compile(ast, reg, &err);
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
        if (!cxpr_eval_program_number(program, ctx, reg, &value, &err)) {
            fprintf(stderr, "Slot benchmark eval failed: %s\n", err.message); exit(1);
        }
        churn_total += value;
    }
    churn_end = now_ns();

    churn_ns = (double)(churn_end - churn_start) / (double)iterations;
    g_sink += churn_total;

    printf("%-18s  %10zu  %12s  %12.2f  %8s\n",
           "context_slot", iterations, "-", churn_ns, "-");

    cxpr_program_free(program);
    cxpr_ast_free(ast);
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

    printf("%-18s  %10s  %12s  %12s  %8s\n",
           "case", "iters", "set ns/op", "alt ns/op", "speedup");
    printf("%-18s  %10zu  %12.2f  %12.2f  %8.2fx\n",
           "base_array", iterations, set_ns, array_ns, set_ns / array_ns);
    printf("%-18s  %10zu  %12.2f  %12.2f  %8.2fx\n",
           "mutate_array", iterations, mutate_set_ns, mutate_array_ns, mutate_set_ns / mutate_array_ns);
    printf("%-18s  %10zu  %12.2f  %12.2f  %8.2fx\n",
           "mutate_prehashed", iterations, mutate_set_ns, mutate_prehashed_ns, mutate_set_ns / mutate_prehashed_ns);
    printf("%-18s  %10zu  %12.2f  %12.2f  %8.2fx\n",
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

    printf("%-18s  %10s  %12s  %12s  %8s\n",
           "case", "iters", "set ns/op", "alt ns/op", "speedup");
    printf("%-18s  %10zu  %12.2f  %12.2f  %8.2fx\n",
           "base_param_array", iterations, set_ns, array_ns, set_ns / array_ns);
    printf("%-18s  %10zu  %12.2f  %12.2f  %8.2fx\n",
           "mutate_param_array", iterations, mutate_set_ns, mutate_array_ns, mutate_set_ns / mutate_array_ns);
    printf("%-18s  %10zu  %12.2f  %12.2f  %8.2fx\n",
           "mutate_param_hash", iterations, mutate_set_ns, mutate_prehashed_ns, mutate_set_ns / mutate_prehashed_ns);
}

static void print_bench_header(const char* title) {
    printf("\n%s\n", title);
    printf("%-18s  %10s  %12s  %12s  %8s\n",
           "case", "iters", "AST ns/eval", "IR ns/eval", "speedup");
}

int main(void) {
    const bench_case cases[] = {
        { "simple_arith", "a + b * c - d / e", 500000, 0 },
        { "nested_expr", "((a + b) * (c - d) / (e + f)) > g ? h : i", 400000, 0 },
        { "function_call", "sqrt(a*a + b*b) + pow(c, 2) - abs(d)", 250000, 0 },
        { "defined_fn", "hyp2(a, b) + hyp2(c, d) - sq(e)", 200000, 0 },
        { "native_fn", "native_hyp2(a, b) + native_hyp2(c, d) - native_sq(e)", 200000, 0 },
        { "defined_chain", "f3(a, b, c) + f3(d, e, f) - sq(g)", 120000, 0 },
        { "native_chain", "native_f3(a, b, c) + native_f3(d, e, f) - native_sq(g)", 120000, 0 },
        { "mixed_chain", "f3(a, b, c) + native_f3(d, e, f) - native_sq(g)", 120000, 0 },
        { "deep_defined", "f5(a, b, c, d) + f5(e, f, g, h)", 80000, 0 },
        { "deep_native", "native_f5(a, b, c, d) + native_f5(e, f, g, h)", 80000, 0 },
        { "context_churn", "a + b * c - d / e + x * y - z", 200000, 1 },
    };
    const typed_bench_case typed_cases[] = {
        { "producer_field", "macd(12, 26, 9).histogram + macd(12, 26, 9).signal", 150000, NULL },
        { "producer_struct", "macd(12, 26, 9)", 150000, "histogram" },
    };
    size_t i;
    cxpr_error err = {0};
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();

    if (!parser || !ctx || !reg) {
        fprintf(stderr, "Failed to initialize benchmark state\n");
        return 1;
    }
    cxpr_register_defaults(reg);
    cxpr_registry_add_unary(reg, "native_sq", native_sq);
    cxpr_registry_add_binary(reg, "native_hyp2", native_hyp2);
    cxpr_registry_add_ternary(reg, "native_f3", native_f3);
    cxpr_registry_add(reg, "native_f5", native_f5_adapter, 4, 4, NULL, NULL);
    {
        const char* macd_fields[] = {"line", "signal", "histogram"};
        cxpr_registry_add_struct(reg, "macd", bench_macd, 3, 3, macd_fields, 3, NULL, NULL);
    }

    err = cxpr_registry_define_fn(reg, "sq(x) => x * x");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Failed to define sq: %s\n", err.message);
        cxpr_registry_free(reg);
        cxpr_context_free(ctx);
        cxpr_parser_free(parser);
        return 1;
    }

    err = cxpr_registry_define_fn(reg, "hyp2(x, y) => sqrt(sq(x) + sq(y))");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Failed to define hyp2: %s\n", err.message);
        cxpr_registry_free(reg);
        cxpr_context_free(ctx);
        cxpr_parser_free(parser);
        return 1;
    }

    err = cxpr_registry_define_fn(reg, "f3(x, y, z) => sqrt(hyp2(x, y) + sq(z))");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Failed to define f3: %s\n", err.message);
        cxpr_registry_free(reg);
        cxpr_context_free(ctx);
        cxpr_parser_free(parser);
        return 1;
    }

    err = cxpr_registry_define_fn(reg, "f5(a, b, c, d) => sqrt((a*a + b*b) + (c*c + d*d))");
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Failed to define f5: %s\n", err.message);
        cxpr_registry_free(reg);
        cxpr_context_free(ctx);
        cxpr_parser_free(parser);
        return 1;
    }

    printf("cxpr AST vs IR benchmark\n");

    print_bench_header("Scalar");
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        bench_one(parser, ctx, reg, &cases[i]);
    }

    print_bench_header("Typed Struct");
    for (i = 0; i < sizeof(typed_cases) / sizeof(typed_cases[0]); ++i) {
        bench_one_typed(parser, ctx, reg, &typed_cases[i]);
    }

    print_bench_header("IR-only");
    bench_slot_churn(parser, ctx, reg);

    printf("\nContext Update Paths\n");
    bench_context_update_paths(ctx);

    printf("\nParam Update Paths\n");
    bench_param_update_paths(ctx);

    printf("sink=%.6f\n", g_sink);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
    return 0;
}
