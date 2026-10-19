#include <cxpr/doc_tooling.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* read_stdin(void) {
    char* data = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    int ch;
    while ((ch = fgetc(stdin)) != EOF) {
        if (length + 1u >= capacity) {
            size_t next_capacity = capacity ? capacity * 2u : 4096u;
            char* next = (char*)realloc(data, next_capacity);
            if (!next) { free(data); return NULL; }
            data = next;
            capacity = next_capacity;
        }
        data[length++] = (char)ch;
    }
    if (!data) data = (char*)malloc(1u);
    else if (length + 1u >= capacity) data = (char*)realloc(data, length + 1u);
    if (data) data[length] = '\0';
    return data;
}

int main(int argc, char** argv) {
    const char* source_name = "<stdin>";
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--source-name") == 0 && i + 1 < argc) source_name = argv[++i];
        else if (strcmp(argv[i], "--help") == 0) {
            fputs("usage: cxpr_document_tooling [--source-name PATH] < file.cxpr\n", stdout);
            return 0;
        }
    }
    char* source = read_stdin();
    char* json = source ? cxpr_document_analyze_json(source, source_name) : NULL;
    free(source);
    if (!json) {
        fputs("{\"ok\":false,\"error\":\"Out of memory\"}\n", stdout);
        return 1;
    }
    fputs(json, stdout);
    int status = strncmp(json, "{\"ok\":false", 11u) == 0 ? 2 : 0;
    cxpr_document_json_free(json);
    return status;
}
