#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cxpr/provider.h>
#include <cxpr/cxpr.h>

/* Keep this test aligned with examples/scoped_sources.md and scoped_sources.c. */

#define CXPR_ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

typedef struct {
    double open;
    double high;
    double low;
    double close;
    double volume;
    double timestamp;
} scope_test_values;

static int scope_test_resolve(uint64_t handle,
                              const char* source_name,
                              double* out_value,
                              void* userdata) {
    const scope_test_values* values = userdata;

    if (!values || !out_value || !source_name || handle != 7u) return 0;
    if (strcmp(source_name, "open") == 0) {
        *out_value = values->open;
        return 1;
    }
    if (strcmp(source_name, "high") == 0) {
        *out_value = values->high;
        return 1;
    }
    if (strcmp(source_name, "low") == 0) {
        *out_value = values->low;
        return 1;
    }
    if (strcmp(source_name, "close") == 0) {
        *out_value = values->close;
        return 1;
    }
    if (strcmp(source_name, "volume") == 0) {
        *out_value = values->volume;
        return 1;
    }
    if (strcmp(source_name, "timestamp") == 0) {
        *out_value = values->timestamp;
        return 1;
    }
    return 0;
}

static void assert_registered_arity(const cxpr_registry* reg,
                                    const char* name,
                                    size_t expected_min_args,
                                    size_t expected_max_args) {
    size_t min_args = 99u;
    size_t max_args = 99u;
    int ok;

    ok = cxpr_registry_lookup(reg, name, &min_args, &max_args);
    if (!ok || min_args != expected_min_args || max_args != expected_max_args) {
        fprintf(stderr, "registered arity mismatch for %s\n", name);
        abort();
    }
}

static void test_scoped_source_functions_register_registers_scoped_family(void) {
    cxpr_registry* reg = cxpr_registry_new();
    static const cxpr_provider_scope_spec kSelectorScope = {
        "selector",
        1,
    };
    static const cxpr_scoped_source_spec families[] = {
        {"open", 0u, 1u, &kSelectorScope},
        {"high", 0u, 1u, &kSelectorScope},
        {"low", 0u, 1u, &kSelectorScope},
        {"close", 0u, 1u, &kSelectorScope},
        {"volume", 0u, 1u, &kSelectorScope},
        {"timestamp", 0u, 1u, &kSelectorScope},
    };
    static const scope_test_values values = {
        101.5,
        105.0,
        99.25,
        103.75,
        42.0,
        1710000000.0,
    };
    cxpr_scope_resolver resolver = {
        .resolve = scope_test_resolve,
        .userdata = (void*)&values,
    };
    cxpr_error err = {0};
    const double args[] = {7.0};
    double close_value;
    double volume_value;

    assert(reg != NULL);

    cxpr_scoped_source_functions_register(
        reg, families, CXPR_ARRAY_COUNT(families), &resolver, NULL);

    assert_registered_arity(reg, "open", 0u, 1u);
    assert_registered_arity(reg, "high", 0u, 1u);
    assert_registered_arity(reg, "low", 0u, 1u);
    assert_registered_arity(reg, "close", 0u, 1u);
    assert_registered_arity(reg, "volume", 0u, 1u);
    assert_registered_arity(reg, "timestamp", 0u, 1u);
    close_value = cxpr_registry_call(reg, "close", args, 1u, &err);
    if (!(fabs(close_value - 103.75) < 1e-12) || err.code != CXPR_OK) abort();
    volume_value = cxpr_registry_call(reg, "volume", args, 1u, &err);
    if (!(fabs(volume_value - 42.0) < 1e-12) || err.code != CXPR_OK) abort();

    cxpr_registry_free(reg);
}

int main(void) {
    test_scoped_source_functions_register_registers_scoped_family();
    printf("  ✓ cxpr scope family registration\n");
    return 0;
}
