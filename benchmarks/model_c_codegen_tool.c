#include <cxpr/cxpr.h>
#include <stdio.h>
#include <stdlib.h>

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    long size;
    char* data;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    data = (char*)malloc((size_t)size + 1u);
    if (!data) {
        fclose(f);
        return NULL;
    }
    if (fread(data, 1u, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    data[size] = '\0';
    fclose(f);
    return data;
}

int main(int argc, char** argv) {
    cxpr_error err = {0};
    char* source;
    char* code;
    cxpr_model* model;
    cxpr_model_program* program;
    cxpr_context* ctx = NULL;
    double* param_values = NULL;
    size_t param_count = 0u;
    const char* qualifiers = argc == 5 ? argv[3] : NULL;
    const char* function_name = argc == 5 ? argv[4] : "cxpr_bench_rsi_state_tick_c";
    FILE* out;

    if (argc != 3 && argc != 5) {
        fprintf(stderr, "usage: %s <model.cxpr> <out.c> [qualifiers function_name]\n", argv[0]);
        return 2;
    }
    source = read_file(argv[1]);
    if (!source) {
        fprintf(stderr, "failed to read %s\n", argv[1]);
        return 1;
    }
    model = cxpr_parse_model(source, &err);
    if (!model) {
        fprintf(stderr, "parse failed: %s\n", err.message);
        free(source);
        return 1;
    }
    program = cxpr_compile_model(model, NULL, &err);
    if (!program) {
        fprintf(stderr, "compile failed: %s\n", err.message);
        cxpr_model_free(model);
        free(source);
        return 1;
    }
    param_count = cxpr_model_program_c_param_count(program);
    if (param_count > 0u) {
        ctx = cxpr_context_new();
        param_values = (double*)calloc(param_count, sizeof(double));
        if (!ctx || !param_values) {
            fprintf(stderr, "out of memory while preparing specialized params\n");
            cxpr_context_free(ctx);
            free(param_values);
            cxpr_model_program_free(program);
            cxpr_model_free(model);
            free(source);
            return 1;
        }
        if (!cxpr_model_program_seed_defaults(program, ctx, NULL, &err)) {
            fprintf(stderr, "default param eval failed: %s\n", err.message);
            cxpr_context_free(ctx);
            free(param_values);
            cxpr_model_program_free(program);
            cxpr_model_free(model);
            free(source);
            return 1;
        }
        for (size_t i = 0u; i < param_count; ++i) {
            bool found = false;
            const char* name = cxpr_model_program_c_param_name(program, i);
            param_values[i] = cxpr_context_get_param(ctx, name, &found);
            if (!found) {
                fprintf(stderr, "default param missing: %s\n", name ? name : "(null)");
                cxpr_context_free(ctx);
                free(param_values);
                cxpr_model_program_free(program);
                cxpr_model_free(model);
                free(source);
                return 1;
            }
        }
    }
    code = param_values
        ? cxpr_model_program_to_c_tick_function_with_params(program, qualifiers,
                                                            function_name,
                                                            param_values,
                                                            param_count,
                                                            &err)
        : cxpr_model_program_to_c_tick_function(program, qualifiers,
                                                function_name,
                                                &err);
    if (!code) {
        fprintf(stderr, "C emit failed: %s\n", err.message);
        cxpr_context_free(ctx);
        free(param_values);
        cxpr_model_program_free(program);
        cxpr_model_free(model);
        free(source);
        return 1;
    }
    out = fopen(argv[2], "wb");
    if (!out) {
        fprintf(stderr, "failed to open %s\n", argv[2]);
        free(code);
        cxpr_model_program_free(program);
        cxpr_model_free(model);
        free(source);
        return 1;
    }
    fputs("#include <math.h>\n#include <stddef.h>\n\n", out);
    fputs(code, out);
    fclose(out);
    free(code);
    cxpr_context_free(ctx);
    free(param_values);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    free(source);
    return 0;
}
