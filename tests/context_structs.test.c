#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>

static void test_context_struct_storage_paths(void) {
    cxpr_context* ctx = cxpr_context_new();
    const char* fields[] = {"x", "y"};
    double values[] = {3.0, 4.0};
    const cxpr_struct_value* s;
    cxpr_value field;
    bool found = false;

    assert(ctx);
    cxpr_context_set_fields(ctx, "pose", fields, values, 2);
    s = cxpr_context_get_struct(ctx, "pose");
    assert(s);
    assert(s->field_count == 2);

    field = cxpr_context_get_field(ctx, "pose", "x", &found);
    assert(found);
    assert(field.type == CXPR_VALUE_NUMBER);
    assert(field.d == 3.0);

    cxpr_context_clear_cached_structs(ctx);
    field = cxpr_context_get_field(ctx, "pose", "y", &found);
    assert(found);
    assert(field.d == 4.0);

    cxpr_context_free(ctx);
}

int main(void) {
    test_context_struct_storage_paths();
    printf("  \xE2\x9C\x93 context_structs\n");
    return 0;
}
