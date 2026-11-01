#include <cxpr/doc_tooling.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    long size;
    char* source;

    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0) {
        fclose(file);
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
    char* source;
    char* json;

    if (argc != 3) {
        fputs("usage: doc_tooling_native_baseline FIXTURE SOURCE_NAME\n", stderr);
        return 64;
    }
    source = read_file(argv[1]);
    if (!source) {
        fprintf(stderr, "failed to read fixture: %s\n", argv[1]);
        return 1;
    }
    json = cxpr_document_analyze_json(source, argv[2]);
    free(source);
    if (!json) {
        fputs("cxpr_document_analyze_json failed\n", stderr);
        return 1;
    }
    fwrite(json, 1u, strlen(json), stdout);
    cxpr_document_json_free(json);
    return 0;
}
