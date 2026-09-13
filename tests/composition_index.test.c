#include <cxpr/model/model.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CXPR_TEST_SOURCE_DIR
#define CXPR_TEST_SOURCE_DIR "."
#endif

static char* read_fixture(const char* relative_path) {
    char path[1024];
    FILE* file;
    long size;
    char* source;
    (void)snprintf(path, sizeof(path), "%s/%s", CXPR_TEST_SOURCE_DIR, relative_path);
    file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) return NULL;
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
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

int main(void) {
    cxpr_error error = {0};
    char* source = read_fixture("fixtures/index/composition.cxpr");
    cxpr_model* model;
    assert(source != NULL);
    model = cxpr_model_parse(source, &error);
    if (!model) fprintf(stderr, "composition fixture: %s\n", error.message);
    assert(model != NULL);
    assert(cxpr_model_output_count(model) == 5u);
    assert(strstr(source, "samples[sample_index]") != NULL);
    assert(strstr(source, "sensor[history_offset]") != NULL);
    assert(strstr(source, "path[distance]") != NULL);
    assert(strstr(source, "(path[distance]).x") != NULL);
    cxpr_model_free(model);
    free(source);
    puts("cxpr index composition fixture tests passed.");
    return 0;
}
