#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include "../src/hashmap/internal.h"

bool cxpr_context_get_local_param_bool(const cxpr_context* ctx, const char* name, bool* found);
const char* cxpr_context_get_local_param_string(const cxpr_context* ctx, const char* name,
                                                bool* found);

static void test_context_value_paths(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_context_entry vars[] = {{"a", 1.0}, {"b", 2.0}, {NULL, 0.0}};
    cxpr_context_entry params[] = {{"len", 14.0}, {NULL, 0.0}};
    bool found = false;
    cxpr_value value;

    assert(ctx);
    cxpr_context_set_array(ctx, vars);
    cxpr_context_set_param_array(ctx, params);
    cxpr_context_set_prehashed(ctx, "c", cxpr_hash_string("c"), 3.0);
    cxpr_context_set_param_prehashed(ctx, "mult", cxpr_hash_string("mult"), 4.0);
    cxpr_context_set_bool(ctx, "flag", true);
    cxpr_context_set_param_bool(ctx, "enabled", true);
    cxpr_context_set_string(ctx, "symbol", "AAPL");
    cxpr_context_set_param_string(ctx, "timeframe", "1h");

    assert(cxpr_context_get(ctx, "a", &found) == 1.0 && found);
    assert(cxpr_context_get(ctx, "c", &found) == 3.0 && found);
    assert(cxpr_context_get_param(ctx, "len", &found) == 14.0 && found);
    assert(cxpr_context_get_param(ctx, "mult", &found) == 4.0 && found);

    value = cxpr_context_get_typed(ctx, "b", &found);
    assert(found);
    assert(value.type == CXPR_VALUE_NUMBER);
    assert(value.d == 2.0);

    value = cxpr_context_get_typed(ctx, "flag", &found);
    assert(found);
    assert(value.type == CXPR_VALUE_BOOL);
    assert(value.b == true);
    assert(cxpr_context_get(ctx, "flag", &found) == 1.0 && found);
    assert(cxpr_context_get_bool(ctx, "flag", &found) == true && found);
    assert(cxpr_context_get_param_bool(ctx, "enabled", &found) == true && found);
    assert(cxpr_context_get_local_param_bool(ctx, "enabled", &found) == true && found);

    value = cxpr_context_get_typed(ctx, "symbol", &found);
    assert(found);
    assert(value.type == CXPR_VALUE_STRING);
    assert(strcmp(value.str, "AAPL") == 0);
    assert(strcmp(cxpr_context_get_string(ctx, "symbol", &found), "AAPL") == 0 && found);
    assert(strcmp(cxpr_context_get_param_string(ctx, "timeframe", &found), "1h") == 0 && found);
    assert(strcmp(cxpr_context_get_local_param_string(ctx, "timeframe", &found), "1h") == 0 &&
           found);

    {
        cxpr_context* clone = cxpr_context_clone(ctx);
        assert(clone);
        cxpr_context_set_string(ctx, "symbol", "MSFT");
        value = cxpr_context_get_typed(clone, "symbol", &found);
        assert(found);
        assert(value.type == CXPR_VALUE_STRING);
        assert(strcmp(value.str, "AAPL") == 0);
        assert(strcmp(cxpr_context_get_param_string(clone, "timeframe", &found), "1h") == 0 &&
               found);
        cxpr_context_free(clone);
    }

    cxpr_context_set(ctx, "flag", 5.0);
    value = cxpr_context_get_typed(ctx, "flag", &found);
    assert(found);
    assert(value.type == CXPR_VALUE_NUMBER);
    assert(value.d == 5.0);
    assert(cxpr_context_get_bool(ctx, "flag", &found) == false && !found);

    cxpr_context_set_bool(ctx, "parent_flag", true);
    {
        cxpr_context* overlay = cxpr_context_overlay_new(ctx);
        assert(overlay);
        cxpr_context_set(overlay, "parent_flag", 7.0);
        value = cxpr_context_get_typed(overlay, "parent_flag", &found);
        assert(found);
        assert(value.type == CXPR_VALUE_NUMBER);
        assert(value.d == 7.0);
        cxpr_context_free(overlay);
    }

    cxpr_context_set(ctx, "symbol", 9.0);
    value = cxpr_context_get_typed(ctx, "symbol", &found);
    assert(found);
    assert(value.type == CXPR_VALUE_NUMBER);
    assert(value.d == 9.0);
    assert(cxpr_context_get_string(ctx, "symbol", &found) == NULL && !found);

    cxpr_context_free(ctx);
}

static void test_extended_value_clone_paths(void) {
    cxpr_value values[] = {
        cxpr_string("EU"),
        cxpr_timestamp(1700000000000000000LL),
        cxpr_duration(60000000000LL),
        cxpr_null()
    };
    cxpr_value array_value;
    cxpr_value clone;

    array_value = cxpr_array(cxpr_array_value_new(values, 4u));
    assert(array_value.a != NULL);
    assert(array_value.a->count == 4u);
    assert(array_value.a->values[0].type == CXPR_VALUE_STRING);
    assert(strcmp(array_value.a->values[0].str, "EU") == 0);
    assert(array_value.a->values[1].type == CXPR_VALUE_TIMESTAMP);
    assert(array_value.a->values[1].i64 == 1700000000000000000LL);
    assert(array_value.a->values[2].type == CXPR_VALUE_DURATION);
    assert(array_value.a->values[2].i64 == 60000000000LL);
    assert(array_value.a->values[3].type == CXPR_VALUE_NULL);

    clone = cxpr_value_clone(&array_value);
    assert(clone.type == CXPR_VALUE_ARRAY);
    assert(clone.a != NULL);
    assert(clone.a != array_value.a);
    assert(clone.a->count == 4u);
    assert(clone.a->values[0].type == CXPR_VALUE_STRING);
    assert(strcmp(clone.a->values[0].str, "EU") == 0);

    cxpr_value_free(&array_value);
    assert(clone.a->values[0].type == CXPR_VALUE_STRING);
    assert(strcmp(clone.a->values[0].str, "EU") == 0);
    cxpr_value_free(&clone);
}

int main(void) {
    test_context_value_paths();
    test_extended_value_clone_paths();
    printf("  \xE2\x9C\x93 context_values\n");
    return 0;
}
