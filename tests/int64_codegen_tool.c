#include <cxpr/cxpr.h>

#include <stdio.h>
#include <stdlib.h>

static int emit(FILE* out, const char* source, const char* name) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, source, &err);
    cxpr_expr_compiled* program = ast ? cxpr_expr_compile(ast, registry, &err) : NULL;
    char* code = program ? cxpr_expr_compiled_to_c_typed_checked_function(
                               program, "static", name, &err) : NULL;
    if (code) fputs(code, out);
    else fprintf(stderr, "%s: %s\n", name, err.message ? err.message : "codegen failed");
    free(code);
    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
    return code != NULL;
}

int main(int argc, char** argv) {
    FILE* out;
    int ok;
    if (argc != 2) return 2;
    out = fopen(argv[1], "wb");
    if (!out) return 2;
    fputs("#include <cxpr/cxpr.h>\n", out);
    ok = emit(out, "9007199254740993i64 + 2i64", "gen_int_add") &&
         emit(out, "5i64 / 2i64", "gen_int_div") &&
         emit(out, "5i64 + 0.5", "gen_mixed_add") &&
         emit(out, "5i64 == 5.0", "gen_mixed_equal") &&
         emit(out, "9007199254740993i64 > 9007199254740992.0", "gen_boundary_gt") &&
         emit(out, "9223372036854775807i64 + 1i64", "gen_overflow");
    fclose(out);
    return ok ? 0 : 1;
}
