#include <stdio.h>
#include <string.h>

#include <cxpr/provider.h>
#include <cxpr/cxpr.h>

#define CXPR_ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

typedef struct {
    double close;
} example_values;

static int example_resolve(uint64_t handle,
                           const char* source_name,
                           double* out_value,
                           void* userdata) {
    const example_values* values = userdata;

    if (!values || !out_value || !source_name || handle != 7u) return 0;
    if (strcmp(source_name, "close") == 0) {
        *out_value = values->close;
        return 1;
    }
    return 0;
}

int main(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    size_t min_args = 0u;
    size_t max_args = 0u;
    static const cxpr_provider_scope_spec kScope = {
        "selector",
        1,
    };
    static const cxpr_scoped_source_spec kSources[] = {
        {"close", 0u, 1u, &kScope},
    };
    static const example_values kValues = {
        103.75,
    };
    const cxpr_scope_resolver resolver = {
        .resolve = example_resolve,
        .userdata = (void*)&kValues,
    };
    const double args[] = {7.0};
    double value = 0.0;

    if (!reg) return 1;

    cxpr_scoped_source_functions_register(
        reg, kSources, CXPR_ARRAY_COUNT(kSources), &resolver, NULL);

    if (!cxpr_registry_lookup(reg, "close", &min_args, &max_args) ||
        min_args != 0u ||
        max_args != 1u) {
        fprintf(stderr, "close was not registered with expected arity\n");
        cxpr_registry_free(reg);
        return 1;
    }

    value = cxpr_registry_call(reg, "close", args, 1u, &err);
    if (err.code != CXPR_OK) {
        fprintf(stderr, "close failed: %s\n", err.message ? err.message : "unknown error");
        cxpr_registry_free(reg);
        return 1;
    }

    printf("close(7) = %.2f\n", value);
    cxpr_registry_free(reg);
    return 0;
}
