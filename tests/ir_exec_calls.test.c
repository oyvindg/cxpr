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
                                       const cxpr_expr_ast* call_ast,
                                       const cxpr_context* ctx,
                                       const cxpr_registry* reg,
                                       const cxpr_value* args,
                                       size_t argc, cxpr_error* err);
cxpr_error cxpr_registry_define_record_fn(cxpr_registry* reg, const char* name,
                                          const char* const* param_names,
                                          size_t param_count,
                                          const char* const* field_names,
                                          const cxpr_expr_ast* const* field_bodies,
                                          size_t field_count);
static char* test_strdup(const char* text) {
    size_t len = strlen(text) + 1;
    char* copy = (char*)malloc(len);
    assert(copy);
    memcpy(copy, text, len);
    return copy;
}

static void pair_producer(const double* args, size_t argc, cxpr_value* out, size_t field_count,
                          void* userdata) {
    (void)argc;
    (void)userdata;
    assert(field_count == 2);
    out[0] = cxpr_num(args[0] + args[1]);
    out[1] = cxpr_num(args[0] - args[1]);
}

static void test_ir_exec_call_helpers(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_func_entry entry = {0};
    cxpr_value args[2] = {cxpr_num(5.0), cxpr_num(2.0)};
    cxpr_value value;

    assert(ctx && reg);
    entry.struct_producer = pair_producer;
    entry.min_args = 2;
    entry.max_args = 2;
    entry.fields_per_arg = 2;
    entry.struct_fields = (char**)calloc(2, sizeof(char*));
    assert(entry.struct_fields);
    entry.struct_fields[0] = test_strdup("sum");
    entry.struct_fields[1] = test_strdup("diff");
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
        value = cxpr_ir_call_defined_scalar(def, NULL, ctx, reg, args, 2, &err);
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

static void test_struct_field_binding_fallback_and_error(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    const char* params[] = {"v"};
    const char* fields[] = {"sum"};
    cxpr_expr_ast* body = cxpr_expr_ast_parse(parser, "v.x + v.y", &err);
    const cxpr_expr_ast* bodies[] = {body};
    cxpr_expr_ast* call;
    cxpr_func_entry* entry;
    cxpr_value arg = cxpr_num(0.0);
    cxpr_value value;

    assert(parser && reg && ctx && body);
    assert(cxpr_registry_define_record_fn(reg, "project", params, 1u,
                                          fields, bodies, 1u).code == CXPR_OK);
    call = cxpr_expr_ast_parse(parser, "project(vector)", &err);
    assert(call);
    entry = cxpr_registry_find(reg, "project");
    assert(entry);

    cxpr_context_set(ctx, "vector.x", 10.0);
    cxpr_context_set(ctx, "vector_x", 1.0);
    cxpr_context_set(ctx, "vector_y", 2.0);
    value = cxpr_ir_call_defined_scalar(entry, call, ctx, reg, &arg, 1u, &err);
    assert(err.code == CXPR_OK);
    assert(value.type == CXPR_VALUE_STRUCT);
    assert(value.s->field_values[0].d == 12.0); /* dot wins for x, underscore supplies y */
    cxpr_value_free(&value);

    cxpr_context_clear(ctx);
    err = (cxpr_error){0};
    value = cxpr_ir_call_defined_scalar(entry, call, ctx, reg, &arg, 1u, &err);
    assert(value.type == CXPR_VALUE_NUMBER);
    assert(err.code == CXPR_ERR_UNKNOWN_IDENTIFIER);

    cxpr_expr_ast_free(call);
    cxpr_expr_ast_free(body);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_expr_parser_free(parser);
}

int main(void) {
    test_ir_exec_call_helpers();
    test_struct_field_binding_fallback_and_error();
    printf("  \xE2\x9C\x93 ir_exec_calls\n");
    return 0;
}
