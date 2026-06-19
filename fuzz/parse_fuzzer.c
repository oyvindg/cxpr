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

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    char* source;
    cxpr_parser* parser;
    cxpr_ast* ast;
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

    parser = cxpr_parser_new();
    if (!parser) {
        free(source);
        return 0;
    }

    ast = cxpr_parse(parser, source, &err);
    if (ast) {
        cxpr_registry* reg = cxpr_registry_new();
        if (reg) {
            cxpr_register_defaults(reg);

            cxpr_program* program = cxpr_compile(ast, reg, &err);
            if (program) {
                cxpr_context* ctx = cxpr_context_new();
                if (ctx) {
                    double num = 0.0;
                    bool flag = false;
                    /* Exercise both typed exit points of the executor. */
                    (void)cxpr_eval_program_number(program, ctx, reg, &num, &err);
                    (void)cxpr_eval_program_bool(program, ctx, reg, &flag, &err);
                    cxpr_context_free(ctx);
                }
                cxpr_program_free(program);
            }
            cxpr_registry_free(reg);
        }
        cxpr_ast_free(ast);
    }

    cxpr_parser_free(parser);
    free(source);
    return 0;
}
