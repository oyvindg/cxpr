#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>

double cxpr_ir_context_get_prehashed(const cxpr_context* ctx, const char* name,
                                     unsigned long hash, bool* found);
cxpr_value cxpr_ir_struct_get_field(const cxpr_struct_value* value,
                                    const char* field, bool* found);
cxpr_value cxpr_ir_load_field_value(const cxpr_context* ctx, const cxpr_registry* reg,
                                    const cxpr_ir_instr* instr, cxpr_error* err);
cxpr_value cxpr_ir_load_chain_value(const cxpr_context* ctx, const cxpr_ir_instr* instr,
                                    cxpr_error* err);
bool cxpr_ir_push_squared(cxpr_value* stack, size_t* sp, cxpr_value value, cxpr_error* err);
bool cxpr_ir_pop1(cxpr_value* stack, size_t* sp, cxpr_value* out, cxpr_error* err);
bool cxpr_ir_pop2(cxpr_value* stack, size_t* sp, cxpr_value* left, cxpr_value* right, cxpr_error* err);

static void test_ir_exec_lookup_helpers(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    const char* fields[] = {"x", "y"};
    double values[] = {3.0, 4.0};
    cxpr_ir_instr field_instr = {.name = "pose.x", .hash = 0};
    cxpr_ir_instr chain_instr = {.name = "pose.x"};
    cxpr_value stack[4];
    cxpr_value a, b;
    size_t sp = 0;
    bool found = false;
    cxpr_value value;

    assert(ctx && reg);
    field_instr.hash = cxpr_hash_string(field_instr.name);
    cxpr_context_set_fields(ctx, "pose", fields, values, 2);

    assert(cxpr_ir_context_get_prehashed(ctx, "missing", cxpr_hash_string("missing"), &found) == 0.0);
    assert(!found);

    value = cxpr_ir_load_field_value(ctx, reg, &field_instr, &err);
    assert(err.code == CXPR_OK);
    assert(value.type == CXPR_VALUE_NUMBER);
    assert(value.d == 3.0);

    value = cxpr_ir_load_chain_value(ctx, &chain_instr, &err);
    assert(err.code == CXPR_OK);
    assert(value.d == 3.0);

    assert(cxpr_ir_push_squared(stack, &sp, cxpr_fv_double(3.0), &err));
    assert(stack[0].d == 9.0);
    assert(cxpr_ir_push_squared(stack, &sp, cxpr_fv_double(4.0), &err));
    assert(cxpr_ir_pop2(stack, &sp, &a, &b, &err));
    assert(a.d == 9.0 && b.d == 16.0);
    stack[0] = cxpr_fv_double(5.0);
    sp = 1;
    assert(cxpr_ir_pop1(stack, &sp, &a, &err));
    assert(a.d == 5.0);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

int main(void) {
    test_ir_exec_lookup_helpers();
    printf("  \xE2\x9C\x93 ir_exec_lookup\n");
    return 0;
}
