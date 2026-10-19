#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* cxpr_ir_build_struct_cache_key(const char* name, const double* args, size_t argc,
                                           char* local_buf, size_t local_cap, char** heap_buf);
char* cxpr_ir_build_constant_producer_key(const char* name, const cxpr_expr_ast* const* args,
                                          size_t argc, const cxpr_registry* reg);
unsigned long cxpr_eval_ast_hash(const cxpr_expr_ast* ast);
bool cxpr_eval_ast_equal(const cxpr_expr_ast* lhs, const cxpr_expr_ast* rhs);

static void test_ir_cache_key_builders(void) {
    char buf[128];
    char* heap = NULL;
    const char* key;
    cxpr_expr_ast* args[2];
    char* const_key;

    key = cxpr_ir_build_struct_cache_key("ema", (double[]){12.0, 26.0}, 2, buf, sizeof(buf), &heap);
    assert(key == buf);
    assert(strncmp(key, "ema(", 4) == 0);
    assert(heap == NULL);

    args[0] = cxpr_expr_ast_number_new(12.0);
    args[1] = cxpr_expr_ast_number_new(26.0);
    const_key = cxpr_ir_build_constant_producer_key("ema", (const cxpr_expr_ast* const*)args, 2, NULL);
    assert(const_key);
    assert(strncmp(const_key, "ema(", 4) == 0);
    free(const_key);
    cxpr_expr_ast_free(args[0]);
    cxpr_expr_ast_free(args[1]);
}

static void test_numeric_memo_keys_are_value_normalized(void) {
    cxpr_expr_ast* integer = cxpr_expr_ast_int64_new(5);
    cxpr_expr_ast* number = cxpr_expr_ast_number_new(5.0);
    cxpr_expr_ast* rounded = cxpr_expr_ast_number_new(9007199254740992.0);
    cxpr_expr_ast* exact = cxpr_expr_ast_int64_new(INT64_C(9007199254740993));

    assert(integer && number && rounded && exact);
    assert(cxpr_eval_ast_hash(integer) == cxpr_eval_ast_hash(number));
    assert(cxpr_eval_ast_equal(integer, number));
    assert(!cxpr_eval_ast_equal(exact, rounded));

    cxpr_expr_ast_free(integer);
    cxpr_expr_ast_free(number);
    cxpr_expr_ast_free(rounded);
    cxpr_expr_ast_free(exact);
}

int main(void) {
    test_ir_cache_key_builders();
    test_numeric_memo_keys_are_value_normalized();
    printf("  \xE2\x9C\x93 ir_cache_key\n");
    return 0;
}
