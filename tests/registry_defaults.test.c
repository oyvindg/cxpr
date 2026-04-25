#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>

static void test_registry_defaults_are_callable(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_value result;

    assert(reg);
    cxpr_register_defaults(reg);

    result = cxpr_registry_call_value(reg, "sqrt", (double[]){9.0}, 1, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_NUMBER);
    assert(result.d == 3.0);

    result = cxpr_registry_call_value(reg, "min", (double[]){3.0, 1.0, 2.0}, 3, &err);
    assert(err.code == CXPR_OK);
    assert(result.d == 1.0);

    result = cxpr_registry_call_value(reg, "max", (double[]){3.0, 1.0, 2.0}, 3, &err);
    assert(err.code == CXPR_OK);
    assert(result.d == 3.0);

    cxpr_registry_free(reg);
}

int main(void) {
    test_registry_defaults_are_callable();
    printf("  \xE2\x9C\x93 registry_defaults\n");
    return 0;
}
