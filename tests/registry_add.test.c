#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static double twice(double x) { return x * 2.0; }
static double add2(const double* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return args[0] + args[1];
}

static void test_registry_add_paths(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_func_entry* entry;
    const char* params[] = {"value"};

    assert(reg);
    cxpr_registry_add_unary(reg, "twice", twice);
    assert(cxpr_registry_set_param_names(reg, "twice", params, 1));

    entry = cxpr_registry_find(reg, "twice");
    assert(entry);
    assert(entry->native_kind == CXPR_NATIVE_KIND_UNARY);
    assert(entry->min_args == 1);
    assert(entry->max_args == 1);
    assert(entry->param_name_count == 1);
    assert(strcmp(entry->param_names[0], "value") == 0);

    cxpr_registry_add_numeric(reg, "add2", add2, 2, 2, NULL, NULL);
    entry = cxpr_registry_find(reg, "add2");
    assert(entry);
    assert(entry->sync_func == add2);
    assert(entry->native_kind == CXPR_NATIVE_KIND_NONE);
    assert(entry->return_type == CXPR_VALUE_NUMBER);
    assert(entry->has_return_type);

    cxpr_registry_free(reg);
}

int main(void) {
    test_registry_add_paths();
    printf("  \xE2\x9C\x93 registry_add\n");
    return 0;
}
