#include <cxpr/model/model.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CXPR_TEST_SOURCE_DIR
#define CXPR_TEST_SOURCE_DIR "."
#endif

int main(void) {
    const char* relative = "/fixtures/scale/large_host_neutral.cxpr";
    char path[1024];
    FILE* file;
    long size;
    char* source;
    cxpr_error error = {0};
    cxpr_model* model;

    (void)snprintf(path, sizeof(path), "%s%s", CXPR_TEST_SOURCE_DIR, relative);
    file = fopen(path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size > 0);
    rewind(file);
    source = (char*)malloc((size_t)size + 1u);
    assert(source != NULL);
    assert(fread(source, 1u, (size_t)size, file) == (size_t)size);
    source[size] = '\0';
    fclose(file);

    model = cxpr_model_parse(source, &error);
    if (!model) fprintf(stderr, "%s\n", error.message ? error.message : "parse failed");
    assert(model != NULL);
    assert(cxpr_model_use_count(model) == 2u);
    assert(cxpr_model_output_count(model) == 8u);
    assert(strstr(source, "input_a[2]") != NULL);
    assert(strstr(source, "mean(window(input_a, $short_window))") != NULL);
    assert(strstr(source, "previous_score := score_04") != NULL);
    assert(strstr(source, "pair\n}") != NULL);

    cxpr_model_free(model);
    free(source);
    puts("cxpr host-neutral scale fixture tests passed.");
    return 0;
}
