/**
 * @file parse_fuzzer.c
 * @brief libFuzzer entry point exercising parse -> compile -> evaluate.
 *
 * The fuzzer feeds arbitrary bytes as an expression string through the full
 * pipeline so that AddressSanitizer/UndefinedBehaviorSanitizer can flag any
 * memory or UB defect reachable from untrusted input. Every allocation is
 * released on each iteration so leaks surface as failures rather than noise.
 *
 * Build with the `fuzz` CMake preset (Clang + libFuzzer + ASan/UBSan) and run
 * the resulting `cxpr_fuzz_parse` binary, optionally pointing it at a corpus
 * directory.
 */

#include <cxpr/cxpr.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool scalar_values_equal(const cxpr_value* a, const cxpr_value* b) {
    if (a->type != b->type) return false;
    switch (a->type) {
    case CXPR_VALUE_NUMBER: return a->d == b->d || (a->d != a->d && b->d != b->d);
    case CXPR_VALUE_BOOL: return a->b == b->b;
    case CXPR_VALUE_INT64:
    case CXPR_VALUE_TIMESTAMP:
    case CXPR_VALUE_DURATION: return a->i64 == b->i64;
    case CXPR_VALUE_NULL: return true;
    case CXPR_VALUE_STRING: return strcmp(a->str, b->str) == 0;
    default: return true; /* Nested ownership is still exercised and freed below. */
    }
}

static void free_nested_value(cxpr_value* value) {
    if (value->type == CXPR_VALUE_ARRAY || value->type == CXPR_VALUE_STRUCT) {
        cxpr_value_free(value);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    char* source;
    cxpr_expr_parser* parser;
    cxpr_expr_ast* ast;
    cxpr_error err = {0};

    /* cxpr consumes NUL-terminated C strings; reject embedded NULs and bound
     * the size so the fuzzer spends its budget on parser logic, not megabytes. */
    if (size > 4096u || memchr(data, '\0', size) != NULL) {
        return 0;
    }

    source = (char*)malloc(size + 1u);
    if (!source) {
        return 0;
    }
    memcpy(source, data, size);
    source[size] = '\0';

    parser = cxpr_expr_parser_new();
    if (!parser) {
        free(source);
        return 0;
    }

    ast = cxpr_expr_ast_parse(parser, source, &err);
    if (ast) {
        cxpr_registry* reg = cxpr_registry_new();
        if (reg) {
            cxpr_register_defaults(reg);

            cxpr_expr_compiled* program = cxpr_expr_compile(ast, reg, &err);
            if (program) {
                cxpr_context* ast_ctx = cxpr_context_new();
                cxpr_context* ir_ctx = cxpr_context_new();
                if (ast_ctx && ir_ctx) {
                    cxpr_value ast_value = cxpr_num(0.0);
                    cxpr_value ir_value = cxpr_num(0.0);
                    cxpr_error ast_err = {0};
                    cxpr_error ir_err = {0};
                    bool ast_ok = cxpr_eval_ast(ast, ast_ctx, reg, &ast_value, &ast_err);
                    bool ir_ok = cxpr_expr_compiled_eval(program, ir_ctx, reg, &ir_value, &ir_err);
                    if (ast_ok != ir_ok || (!ast_ok && ast_err.code != ir_err.code) ||
                        (ast_ok && !scalar_values_equal(&ast_value, &ir_value))) {
                        __builtin_trap();
                    }
                    if (ast_ok) free_nested_value(&ast_value);
                    if (ir_ok) free_nested_value(&ir_value);
                }
                cxpr_context_free(ast_ctx);
                cxpr_context_free(ir_ctx);
                cxpr_expr_compiled_free(program);
            }
            cxpr_registry_free(reg);
        }
        cxpr_expr_ast_free(ast);
    }

    cxpr_expr_parser_free(parser);
    free(source);
    return 0;
}
