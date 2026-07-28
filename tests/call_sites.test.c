#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <cxpr/cxpr.h>

typedef struct {
    size_t count;
    int found_custom_scope;
    int found_nested_scope;
} visit_state;

static int visit_arg(
    const cxpr_static_named_string_arg* arg,
    void* userdata) {
    visit_state* state = (visit_state*)userdata;
    state->count++;
    if (strcmp(arg->callee, "custom") == 0 &&
        strcmp(arg->argument, "scope") == 0 &&
        strcmp(arg->value, "daily") == 0) {
        assert(arg->call != NULL);
        state->found_custom_scope = 1;
    }
    if (strcmp(arg->callee, "base") == 0 &&
        strcmp(arg->argument, "partition") == 0 &&
        strcmp(arg->value, "archive") == 0) {
        state->found_nested_scope = 1;
    }
    return 1;
}

int main(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    visit_state state = {0};

    assert(parser);
    ast = cxpr_parse(
        parser,
        "custom(source=base(partition=\"archive\"), scope=\"daily\", "
        "label=$dynamic)",
        &err);
    assert(ast);
    assert(cxpr_visit_static_named_string_args(ast, visit_arg, &state));
    assert(state.count == 2u);
    assert(state.found_custom_scope);
    assert(state.found_nested_scope);

    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    puts("call-site metadata tests passed");
    return 0;
}
