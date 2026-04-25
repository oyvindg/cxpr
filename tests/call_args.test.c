#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

bool cxpr_call_bind_args(const cxpr_ast* ast, const cxpr_func_entry* entry,
                         const cxpr_ast** out_args,
                         cxpr_error_code* out_code,
                         const char** out_message);

static void test_call_arg_binding_reorders_named_arguments(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_ast* ast;
    cxpr_error err = {0};
    cxpr_func_entry* entry;
    const cxpr_ast* ordered[2] = {0};
    const char* params[] = {"slow", "fast"};

    assert(parser && reg);
    cxpr_register_defaults(reg);
    cxpr_registry_add_binary(reg, "spread", fmax);
    assert(cxpr_registry_set_param_names(reg, "spread", params, 2));

    ast = cxpr_parse(parser, "spread(fast=9, slow=21)", &err);
    assert(ast);
    entry = cxpr_registry_find(reg, "spread");
    assert(entry);

    assert(cxpr_call_bind_args(ast, entry, ordered, NULL, NULL));
    assert(cxpr_ast_type(ordered[0]) == CXPR_NODE_NUMBER);
    assert(cxpr_ast_type(ordered[1]) == CXPR_NODE_NUMBER);
    assert(cxpr_ast_number_value(ordered[0]) == 21.0);
    assert(cxpr_ast_number_value(ordered[1]) == 9.0);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_parser_free(parser);
}

int main(void) {
    test_call_arg_binding_reorders_named_arguments();
    printf("  \xE2\x9C\x93 call_args\n");
    return 0;
}
