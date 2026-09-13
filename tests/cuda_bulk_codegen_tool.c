#include <cxpr/cxpr.h>
#include <cxpr/plugins/cuda.h>

#include <stdio.h>
#include <stdlib.h>

static char* read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    long size;
    char* source;
    if (!file || fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0) {
        if (file) fclose(file);
        return NULL;
    }
    rewind(file);
    source = (char*)malloc((size_t)size + 1u);
    if (!source || fread(source, 1u, (size_t)size, file) != (size_t)size) {
        free(source);
        fclose(file);
        return NULL;
    }
    source[size] = '\0';
    fclose(file);
    return source;
}

int main(int argc, char** argv) {
    cxpr_error err = {0};
    cxpr_registry* registry = NULL;
    cxpr_model* model = NULL;
    cxpr_model_compiled* program = NULL;
    cxpr_cuda_plugin_options options = {
        "cxpr_klein_gordon_tick", "static __device__ __forceinline__"};
    char* input = NULL;
    char* generated = NULL;
    FILE* output = NULL;
    int result = 1;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: %s MODEL.cxpr OUTPUT.cuh [FUNCTION]\n", argv[0]);
        return 2;
    }
    if (argc == 4) options.function_name = argv[3];
    input = read_file(argv[1]);
    registry = cxpr_registry_new();
    if (!input || !registry) goto done;
    cxpr_register_defaults(registry);
    model = cxpr_model_parse(input, &err);
    program = model ? cxpr_model_compile(model, registry, &err) : NULL;
    generated = program ? cxpr_cuda_plugin_source_from_program(program, &options, &err) : NULL;
    if (!generated) {
        fprintf(stderr, "CUDA codegen failed: %s\n", err.message ? err.message : "unknown error");
        goto done;
    }
    output = fopen(argv[2], "wb");
    if (!output || fputs(generated, output) < 0 || fclose(output) != 0) {
        output = NULL;
        fprintf(stderr, "cannot write %s\n", argv[2]);
        goto done;
    }
    output = NULL;
    result = 0;

done:
    if (output) fclose(output);
    cxpr_cuda_plugin_source_free(generated);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
    cxpr_registry_free(registry);
    free(input);
    return result;
}
