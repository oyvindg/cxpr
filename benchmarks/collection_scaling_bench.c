#include <cxpr/cxpr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { TRIALS = 9, EVALUATIONS = 1000000 };

static volatile double sink;

static double now_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static int compare_double(const void* left, const void* right) {
    double a = *(const double*)left;
    double b = *(const double*)right;
    return (a > b) - (a < b);
}

static void append(char* output, size_t capacity, const char* text) {
    size_t used = strlen(output);
    if (used < capacity) snprintf(output + used, capacity - used, "%s", text);
}

static void build_expression(char* output, size_t capacity,
                             size_t count, int kind) {
    char part[128];
    output[0] = '\0';
    if (kind == 0) {
        append(output, capacity, "0");
        for (size_t i = 0u; i < count; ++i) {
            snprintf(part, sizeof(part), "+a%zu*%.6f", i,
                     0.17 + 0.031 * (double)i);
            append(output, capacity, part);
        }
        return;
    }
    append(output, capacity, kind == 1 ? "reduce([" : "reduce(map([");
    for (size_t i = 0u; i < count; ++i) {
        if (i) append(output, capacity, ",");
        snprintf(part, sizeof(part), "{signal=a%zu,gain=%.6f}", i,
                 0.17 + 0.031 * (double)i);
        append(output, capacity, part);
    }
    append(output, capacity,
           kind == 1
               ? "],0,step=>step.acc+step.value.signal*step.value.gain)"
               : "],item=>item.signal*item.gain),0,step=>step.acc+step.value)");
}

static void run_case(cxpr_expr_parser* parser, cxpr_registry* registry,
                     cxpr_context* context, size_t count, int kind) {
    static const char* names[] = {"manual", "reduce", "map_reduce"};
    char source[8192];
    double samples[TRIALS];
    size_t instruction_count = 0u;

    build_expression(source, sizeof(source), count, kind);
    for (size_t trial = 0u; trial < TRIALS; ++trial) {
        cxpr_error error = {0};
        cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, source, &error);
        cxpr_expr_compiled* program = ast
            ? cxpr_expr_compile(ast, registry, &error) : NULL;
        double result = 0.0;
        double start;
        if (!program) {
            fprintf(stderr, "collection scaling compile failed: %s\n",
                    error.message ? error.message : "unknown error");
            exit(1);
        }
        instruction_count = cxpr_expr_compiled_ir_count(program);
        for (size_t i = 0u; i < instruction_count; ++i) {
            cxpr_ir_instruction instruction;
            if (!cxpr_expr_compiled_ir_instruction(program, i, &instruction) ||
                instruction.op == CXPR_IR_OP_CALL_AST) {
                fprintf(stderr, "collection scaling retained CALL_AST\n");
                exit(1);
            }
        }
        start = now_ns();
        for (size_t i = 0u; i < EVALUATIONS; ++i) {
            if (!cxpr_expr_compiled_eval_number(
                    program, context, registry, &result, &error)) {
                fprintf(stderr, "collection scaling evaluation failed: %s\n",
                        error.message ? error.message : "unknown error");
                exit(1);
            }
        }
        samples[trial] = (now_ns() - start) / (double)EVALUATIONS;
        sink += result;
        cxpr_expr_compiled_free(program);
        cxpr_expr_ast_free(ast);
    }
    qsort(samples, TRIALS, sizeof(samples[0]), compare_double);
    printf("%8zu  %-10s  %12.2f  %12zu\n",
           count, names[kind], samples[TRIALS / 2], instruction_count);
}

int main(void) {
    static const size_t sizes[] = {1u, 2u, 4u, 8u, 16u};
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_context* context = cxpr_context_new();
    if (!registry || !parser || !context) return 1;
    cxpr_register_defaults(registry);
    for (size_t i = 0u; i < 16u; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "a%zu", i);
        cxpr_context_set(context, name, 1.0 + (double)i);
    }
    printf("elements  case          IR ns/fold  instructions\n");
    for (size_t i = 0u; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
        for (int kind = 0; kind < 3; ++kind)
            run_case(parser, registry, context, sizes[i], kind);
    fprintf(stderr, "sink=%.6f\n", sink);
    cxpr_context_free(context);
    cxpr_expr_parser_free(parser);
    cxpr_registry_free(registry);
    return 0;
}
