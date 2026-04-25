#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cxpr_value cxpr_ir_call_producer(cxpr_func_entry* entry, const char* name,
                                 const cxpr_context* ctx,
                                 const cxpr_value* stack_args,
                                 size_t argc, cxpr_error* err);
cxpr_value cxpr_ir_call_producer_field(cxpr_func_entry* entry, const char* name,
                                       const cxpr_context* ctx,
                                       const cxpr_value* stack_args,
                                       size_t argc, const char* field,
                                       cxpr_error* err);
cxpr_value cxpr_ir_call_defined_scalar(cxpr_func_entry* entry,
                                       const cxpr_context* ctx,
                                       const cxpr_registry* reg,
                                       const cxpr_value* args,
                                       size_t argc, cxpr_error* err);

static void pair_producer(const double* args, size_t argc, cxpr_value* out, size_t field_count,
                          void* userdata) {
    (void)argc;
    (void)userdata;
    assert(field_count == 2);
    out[0] = cxpr_fv_double(args[0] + args[1]);
    out[1] = cxpr_fv_double(args[0] - args[1]);
}

static void test_ir_exec_call_helpers(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_func_entry entry = {0};
    cxpr_value args[2] = {cxpr_fv_double(5.0), cxpr_fv_double(2.0)};
    cxpr_value value;

    assert(ctx && reg);
    entry.struct_producer = pair_producer;
    entry.min_args = 2;
    entry.max_args = 2;
    entry.fields_per_arg = 2;
    entry.struct_fields = (char**)calloc(2, sizeof(char*));
    assert(entry.struct_fields);
    entry.struct_fields[0] = strdup("sum");
    entry.struct_fields[1] = strdup("diff");
    assert(entry.struct_fields[0] && entry.struct_fields[1]);

    value = cxpr_ir_call_producer(&entry, "pair", ctx, args, 2, &err);
    assert(err.code == CXPR_OK);
    assert(value.type == CXPR_VALUE_STRUCT);

    value = cxpr_ir_call_producer_field(&entry, "pair", ctx, args, 2, "diff", &err);
    assert(err.code == CXPR_OK);
    assert(value.type == CXPR_VALUE_NUMBER);
    assert(value.d == 3.0);

    assert(cxpr_registry_define_fn(reg, "sum2(a, b) => a + b").code == CXPR_OK);
    {
        cxpr_func_entry* def = cxpr_registry_find(reg, "sum2");
        assert(def);
        value = cxpr_ir_call_defined_scalar(def, ctx, reg, args, 2, &err);
        assert(err.code == CXPR_OK);
        assert(value.type == CXPR_VALUE_NUMBER);
        assert(value.d == 7.0);
    }

    free(entry.struct_fields[0]);
    free(entry.struct_fields[1]);
    free(entry.struct_fields);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

int main(void) {
    test_ir_exec_call_helpers();
    printf("  \xE2\x9C\x93 ir_exec_calls\n");
    return 0;
}
