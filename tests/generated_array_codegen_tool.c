#include <cxpr/cxpr.h>

#include <stdio.h>
#include <stdlib.h>

static int emit(FILE* out, const char* source, const char* function_name,
                const cxpr_c_program_arg* args, size_t arg_count) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_expr_compiled* program;
    char* code;
    if (!parser || !registry) return 0;
    cxpr_register_defaults(registry);
    ast = cxpr_expr_ast_parse(parser, source, &err);
    program = ast ? cxpr_expr_compile(ast, registry, &err) : NULL;
    code = program ? cxpr_expr_compiled_to_c_function(
                         program, "static", "double", function_name,
                         args, arg_count, &err)
                   : NULL;
    if (code) fputs(code, out);
    else fprintf(stderr, "%s: %s\n", function_name, err.message ? err.message : "codegen failed");
    free(code);
    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
    return code != NULL;
}

static int emit_checked(FILE* out, const char* source, const char* function_name,
                        const cxpr_c_program_arg* args, size_t arg_count) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_expr_compiled* program;
    char* code;
    if (!parser || !registry) return 0;
    cxpr_register_defaults(registry);
    ast = cxpr_expr_ast_parse(parser, source, &err);
    program = ast ? cxpr_expr_compile(ast, registry, &err) : NULL;
    code = program ? cxpr_expr_compiled_to_c_checked_function(
                         program, "static", function_name, args, arg_count, &err)
                   : NULL;
    if (code) fputs(code, out);
    free(code);
    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
    return code != NULL;
}

static int rejects_non_scalar(const char* source) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_expr_compiled* program;
    char* code;
    if (!parser || !registry) return 0;
    cxpr_register_defaults(registry);
    ast = cxpr_expr_ast_parse(parser, source, &err);
    program = ast ? cxpr_expr_compile(ast, registry, &err) : NULL;
    code = program ? cxpr_expr_compiled_to_c_function(
                         program, "static", "double", "unsupported_value",
                         NULL, 0u, &err)
                   : NULL;
    free(code);
    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
    return code == NULL && err.code != CXPR_OK;
}

int main(int argc, char** argv) {
    const cxpr_c_program_arg index_arg[] = {
        {.kind = CXPR_C_PROGRAM_ARG_PARAM, .name = "index"},
    };
    const cxpr_c_program_arg nested_args[] = {
        {.kind = CXPR_C_PROGRAM_ARG_PARAM, .name = "outer"},
        {.kind = CXPR_C_PROGRAM_ARG_PARAM, .name = "inner"},
    };
    FILE* out;
    int ok;
    if (argc != 2) return 2;
    out = fopen(argv[1], "wb");
    if (!out) return 2;
    fputs("#include <math.h>\n#include <stdint.h>\n#include <stddef.h>\n", out);
    ok = emit(out, "[10, 20, 30][$index]", "generated_number_index", index_arg, 1u) &&
         emit(out, "[true, false, true][$index]", "generated_bool_index", index_arg, 1u) &&
         emit(out, "[[1, 2], [3, 4]][$outer][$inner]", "generated_nested_index", nested_args, 2u) &&
         emit(out, "[[1, 2], [3, 4]][$outer]", "generated_aggregate_index", nested_args, 1u) &&
         emit_checked(out, "[10, 20, 30][$index]", "generated_number_index_checked", index_arg, 1u) &&
         emit_checked(out, "[[1, 2], [3, 4]][$outer]", "generated_aggregate_index_checked", nested_args, 1u) &&
         rejects_non_scalar("[\"left\", \"right\"][0]");
    fclose(out);
    return ok ? 0 : 1;
}
