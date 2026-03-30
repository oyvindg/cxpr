#include <cxpr/cxpr.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    const char* name;
    const char* expr;
    size_t iterations;
    int mutate_context;
} bench_case;

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

static double time_ast(const cxpr_ast* ast, cxpr_context* ctx, const cxpr_registry* reg,
                       size_t iterations, int mutate_context) {
    size_t i;
    double total = 0.0;
    cxpr_error err = {0};

    for (i = 0; i < iterations; ++i) {
        if (mutate_context) mutate_values(ctx, i);
        total += cxpr_ast_eval_double(ast, ctx, reg, &err);
        if (err.code != CXPR_OK) {
            fprintf(stderr, "AST benchmark eval failed at iter %zu: %s\n", i, err.message);
            exit(1);
        }
    }

    return total;
}

static double time_ir(const cxpr_program* program, cxpr_context* ctx, const cxpr_registry* reg,
                      size_t iterations, int mutate_context) {
    size_t i;
    double total = 0.0;
    cxpr_error err = {0};

    for (i = 0; i < iterations; ++i) {
        if (mutate_context) mutate_values(ctx, i);
        total += cxpr_ir_eval_double(program, ctx, reg, &err);
        if (err.code != CXPR_OK) {
            fprintf(stderr, "IR benchmark eval failed at iter %zu: %s\n", i, err.message);
            exit(1);
        }
    }

    return total;
}

static void validate_ast_vs_ir(const cxpr_ast* ast, const cxpr_program* program,
                               cxpr_context* ctx, const cxpr_registry* reg,
                               const bench_case* c) {
    size_t i;
    cxpr_error ast_err = {0};
    cxpr_error ir_err = {0};

    set_base_values(ctx);

    for (i = 0; i < c->iterations; ++i) {
        double ast_value, ir_value;

        if (c->mutate_context) mutate_values(ctx, i);

        ast_err = (cxpr_error){0};
        ir_err = (cxpr_error){0};
        ast_value = cxpr_ast_eval_double(ast, ctx, reg, &ast_err);
        ir_value = cxpr_ir_eval_double(program, ctx, reg, &ir_err);

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
    size_t i;
    cxpr_error err = {0};
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();

    if (!parser || !ctx || !reg) {
        fprintf(stderr, "Failed to initialize benchmark state\n");
        return 1;
    }

    cxpr_register_builtins(reg);
    cxpr_registry_add_unary(reg, "native_sq", native_sq);
    cxpr_registry_add_binary(reg, "native_hyp2", native_hyp2);
    cxpr_registry_add_ternary(reg, "native_f3", native_f3);
    cxpr_registry_add(reg, "native_f5", native_f5_adapter, 4, 4, NULL, NULL);

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
    printf("%-18s  %10s  %12s  %12s  %8s\n",
           "case", "iters", "AST ns/eval", "IR ns/eval", "speedup");

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        bench_one(parser, ctx, reg, &cases[i]);
    }

    printf("sink=%.6f\n", g_sink);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
    return 0;
}
