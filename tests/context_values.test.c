#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include "../src/hashmap/internal.h"

bool cxpr_context_get_local_param_bool(const cxpr_context* ctx, const char* name, bool* found);
const char* cxpr_context_get_local_param_string(const cxpr_context* ctx, const char* name,
                                                bool* found);

static void set_test_value(cxpr_context* ctx, const char* name, cxpr_value_type type,
                           bool param) {
    cxpr_value element = cxpr_num(6.0);
    const char* fields[] = {"field"};
    cxpr_struct_value* struct_value;
    cxpr_array_value* array_value;
    cxpr_value value;

    switch (type) {
    case CXPR_VALUE_NUMBER:
        if (param) cxpr_context_set_param(ctx, name, 1.5);
        else cxpr_context_set(ctx, name, 1.5);
        return;
    case CXPR_VALUE_BOOL:
        if (param) cxpr_context_set_param_bool(ctx, name, true);
        else cxpr_context_set_bool(ctx, name, true);
        return;
    case CXPR_VALUE_INT64:
        value = cxpr_int64(23);
        break;
    case CXPR_VALUE_STRING:
        value = cxpr_string("final");
        break;
    case CXPR_VALUE_ARRAY:
        array_value = cxpr_array_value_new(&element, 1u);
        assert(array_value);
        value = cxpr_array(array_value);
        if (param) cxpr_context_set_param_value(ctx, name, &value);
        else cxpr_context_set_value(ctx, name, &value);
        cxpr_array_value_free(array_value);
        return;
    case CXPR_VALUE_STRUCT:
        struct_value = cxpr_struct_value_new(fields, &element, 1u);
        assert(struct_value);
        value = cxpr_struct(struct_value);
        if (param) cxpr_context_set_param_value(ctx, name, &value);
        else cxpr_context_set_value(ctx, name, &value);
        cxpr_struct_value_free(struct_value);
        return;
    default:
        assert(false);
        return;
    }
    if (param) cxpr_context_set_param_value(ctx, name, &value);
    else cxpr_context_set_value(ctx, name, &value);
}

static void test_type_replacement_matrix(void) {
    const cxpr_value_type types[] = {CXPR_VALUE_NUMBER, CXPR_VALUE_BOOL, CXPR_VALUE_INT64,
                                     CXPR_VALUE_STRING, CXPR_VALUE_ARRAY, CXPR_VALUE_STRUCT};
    for (size_t param = 0u; param < 2u; ++param) {
        for (size_t first = 0u; first < 6u; ++first) {
            for (size_t second = 0u; second < 6u; ++second) {
                cxpr_context* ctx = cxpr_context_new();
                bool found = false;
                cxpr_value loaded;
                assert(ctx);
                set_test_value(ctx, "value", types[first], param != 0u);
                set_test_value(ctx, "value", types[second], param != 0u);
                loaded = param ? cxpr_context_get_param_typed(ctx, "value", &found)
                               : cxpr_context_get_typed(ctx, "value", &found);
                assert(found);
                assert(loaded.type == types[second]);
                if (loaded.type == CXPR_VALUE_ARRAY || loaded.type == CXPR_VALUE_STRUCT) {
                    cxpr_value_free(&loaded);
                }
                cxpr_context_free(ctx);
            }
        }
    }
}

static void test_numeric_get_without_found(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_value element = cxpr_num(3.0);
    const char* fields[] = {"field"};
    cxpr_struct_value* struct_value = cxpr_struct_value_new(fields, &element, 1u);
    cxpr_array_value* array_value = cxpr_array_value_new(&element, 1u);
    cxpr_value value;
    assert(ctx && struct_value && array_value);
    cxpr_context_set(ctx, "x", 42.0);
    assert(cxpr_context_get(ctx, "x", NULL) == 42.0);
    cxpr_context_set_bool(ctx, "x", true);
    assert(cxpr_context_get(ctx, "x", NULL) == 1.0);
    cxpr_context_set_bool(ctx, "x", false);
    assert(cxpr_context_get(ctx, "x", NULL) == 0.0);
    assert(cxpr_context_get(ctx, "missing", NULL) == 0.0);

    value = cxpr_array(array_value);
    cxpr_context_set_value(ctx, "owned", &value);
    assert(cxpr_context_get(ctx, "owned", NULL) == 0.0);
    value = cxpr_struct(struct_value);
    cxpr_context_set_value(ctx, "owned", &value);
    assert(cxpr_context_get(ctx, "owned", NULL) == 0.0);
    cxpr_array_value_free(array_value);
    cxpr_struct_value_free(struct_value);
    cxpr_context_free(ctx);
}

static void test_borrowed_array_element(void) {
    cxpr_context* parent = cxpr_context_new();
    cxpr_context* child;
    cxpr_value elements[] = {cxpr_num(3.0), cxpr_string("borrowed")};
    cxpr_array_value* array_value = cxpr_array_value_new(elements, 2u);
    cxpr_value array;
    cxpr_value borrowed = cxpr_null();

    assert(parent && array_value);
    array = cxpr_array(array_value);
    cxpr_context_set_value(parent, "series", &array);
    child = cxpr_context_overlay_new(parent);
    assert(child);
    assert(cxpr_context_array_elem_borrow(child, "series", 0u, &borrowed));
    assert(borrowed.type == CXPR_VALUE_NUMBER && borrowed.d == 3.0);
    assert(cxpr_context_array_elem_borrow(child, "series", 1u, &borrowed));
    assert(borrowed.type == CXPR_VALUE_STRING && strcmp(borrowed.str, "borrowed") == 0);
    assert(!cxpr_context_array_elem_borrow(child, "series", 2u, &borrowed));
    assert(!cxpr_context_array_elem_borrow(child, "missing", 0u, &borrowed));

    cxpr_context_free(child);
    cxpr_array_value_free(array_value);
    cxpr_context_free(parent);
}

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

static void test_struct_param_value_paths(void) {
    cxpr_context* ctx = cxpr_context_new();
    const char* fields[] = {"x", "y", "z"};
    cxpr_value values[] = {cxpr_num(0.0), cxpr_num(1.0), cxpr_num(2.0)};
    cxpr_struct_value* zero = cxpr_struct_value_new(fields, values, 3u);
    cxpr_value zero_value = cxpr_struct(zero);
    cxpr_value loaded;
    bool found = false;

    assert(ctx);
    assert(zero);
    cxpr_context_set_param_value(ctx, "zero", &zero_value);
    cxpr_struct_value_free(zero);

    loaded = cxpr_context_get_param_typed(ctx, "zero", &found);
    assert(found);
    assert(loaded.type == CXPR_VALUE_STRUCT);
    assert(loaded.s != NULL);
    assert(loaded.s->field_count == 3u);
    cxpr_value_free(&loaded);

    loaded = cxpr_context_get_param_typed(ctx, "zero.y", &found);
    assert(found);
    assert(loaded.type == CXPR_VALUE_NUMBER);
    assert(loaded.d == 1.0);
    cxpr_value_free(&loaded);

    cxpr_context_set_param(ctx, "zero", 5.0);
    loaded = cxpr_context_get_param_typed(ctx, "zero.y", &found);
    assert(!found);
    cxpr_value_free(&loaded);

    cxpr_context_free(ctx);
}

int main(void) {
    test_numeric_get_without_found();
    test_borrowed_array_element();
    test_type_replacement_matrix();
    test_context_value_paths();
    test_extended_value_clone_paths();
    test_struct_param_value_paths();
    printf("  \xE2\x9C\x93 context_values\n");
    return 0;
}
