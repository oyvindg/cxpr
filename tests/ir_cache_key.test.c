#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* cxpr_ir_build_struct_cache_key(const char* name, const double* args, size_t argc,
                                           char* local_buf, size_t local_cap, char** heap_buf);
char* cxpr_ir_build_constant_producer_key(const char* name, const cxpr_ast* const* args,
                                          size_t argc, const cxpr_registry* reg);

static void test_ir_cache_key_builders(void) {
    char buf[128];
    char* heap = NULL;
    const char* key;
    cxpr_ast* args[2];
    char* const_key;

    key = cxpr_ir_build_struct_cache_key("ema", (double[]){12.0, 26.0}, 2, buf, sizeof(buf), &heap);
    assert(key == buf);
    assert(strncmp(key, "ema(", 4) == 0);
    assert(heap == NULL);

    args[0] = cxpr_ast_new_number(12.0);
    args[1] = cxpr_ast_new_number(26.0);
    const_key = cxpr_ir_build_constant_producer_key("ema", (const cxpr_ast* const*)args, 2, NULL);
    assert(const_key);
    assert(strncmp(const_key, "ema(", 4) == 0);
    free(const_key);
    cxpr_ast_free(args[0]);
    cxpr_ast_free(args[1]);
}

int main(void) {
    test_ir_cache_key_builders();
    printf("  \xE2\x9C\x93 ir_cache_key\n");
    return 0;
}
