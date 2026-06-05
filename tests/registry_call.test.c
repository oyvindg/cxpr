#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static cxpr_value as_bool(const cxpr_value* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return cxpr_bool(args[0].d > args[1].d);
}

static cxpr_value invert_value(const cxpr_value* args, size_t argc, void* userdata) {
    (void)userdata;
    assert(argc == 1);
    assert(args[0].type == CXPR_VALUE_BOOL);
    return cxpr_bool(!args[0].b);
}

static cxpr_value is_1h_value(const cxpr_value* args, size_t argc, void* userdata) {
    (void)userdata;
    assert(argc == 1);
    assert(args[0].type == CXPR_VALUE_STRING);
    return cxpr_bool(strcmp(args[0].str, "1h") == 0);
}

static void test_registry_call_variants(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_value typed_args[2];
    cxpr_value result;
    cxpr_value_type arg_types[] = {CXPR_VALUE_NUMBER, CXPR_VALUE_NUMBER};

    assert(reg);
    cxpr_registry_add_typed(reg, "gt", as_bool, 2, 2, arg_types, CXPR_VALUE_BOOL, NULL, NULL);

    typed_args[0] = cxpr_num(5.0);
    typed_args[1] = cxpr_num(3.0);
    result = cxpr_registry_call_typed(reg, "gt", typed_args, 2, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_BOOL);
    assert(result.b == true);

    result = cxpr_registry_call_value(reg, "gt", (double[]){1.0, 2.0}, 2, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_BOOL);
    assert(result.b == false);

    typed_args[0] = cxpr_bool(true);
    typed_args[1] = cxpr_num(1.0);
    result = cxpr_registry_call_typed(reg, "gt", typed_args, 2, &err);
    assert(isnan(result.d));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);

    cxpr_registry_add_value(reg, "invert", invert_value, 1, 1, NULL, NULL);
    typed_args[0] = cxpr_bool(false);
    result = cxpr_registry_call_typed(reg, "invert", typed_args, 1, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_BOOL);
    assert(result.b == true);

    cxpr_registry_add_value(reg, "is_1h", is_1h_value, 1, 1, NULL, NULL);
    typed_args[0] = cxpr_string("1h");
    result = cxpr_registry_call_typed(reg, "is_1h", typed_args, 1, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_BOOL);
    assert(result.b == true);

    cxpr_registry_free(reg);
}

int main(void) {
    test_registry_call_variants();
    printf("  \xE2\x9C\x93 registry_call\n");
    return 0;
}
