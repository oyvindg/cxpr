#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

static cxpr_value as_bool(const cxpr_value* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return cxpr_fv_bool(args[0].d > args[1].d);
}

static void test_registry_call_variants(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_value typed_args[2];
    cxpr_value result;
    cxpr_value_type arg_types[] = {CXPR_VALUE_NUMBER, CXPR_VALUE_NUMBER};

    assert(reg);
    cxpr_registry_add_typed(reg, "gt", as_bool, 2, 2, arg_types, CXPR_VALUE_BOOL, NULL, NULL);

    typed_args[0] = cxpr_fv_double(5.0);
    typed_args[1] = cxpr_fv_double(3.0);
    result = cxpr_registry_call_typed(reg, "gt", typed_args, 2, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_BOOL);
    assert(result.b == true);

    result = cxpr_registry_call_value(reg, "gt", (double[]){1.0, 2.0}, 2, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_BOOL);
    assert(result.b == false);

    typed_args[0] = cxpr_fv_bool(true);
    typed_args[1] = cxpr_fv_double(1.0);
    result = cxpr_registry_call_typed(reg, "gt", typed_args, 2, &err);
    assert(isnan(result.d));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);

    cxpr_registry_free(reg);
}

int main(void) {
    test_registry_call_variants();
    printf("  \xE2\x9C\x93 registry_call\n");
    return 0;
}
