#include <assert.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

typedef void* (*new_fn)(void);
typedef void (*free_fn)(void*);
typedef void* (*parse_fn)(void*, const char*, void*);
typedef void* (*compile_fn)(const void*, const void*, void*);
typedef void (*set_fn)(void*, const char*, double);
typedef void (*set_int64_fn)(void*, const char*, int64_t);
typedef int64_t (*get_int64_fn)(const void*, const char*, bool*);
typedef bool (*eval_number_fn)(const void*, const void*, const void*, double*, void*);

static void* symbol(void* library, const char* name) {
    void* result = dlsym(library, name);
    if (!result) fprintf(stderr, "missing shared-library symbol %s: %s\n", name, dlerror());
    assert(result);
    return result;
}

int main(void) {
    void* library = dlopen(CXPR_SHARED_LIBRARY_PATH, RTLD_NOW | RTLD_LOCAL);
    new_fn parser_new;
    free_fn parser_free;
    parse_fn parse;
    free_fn ast_free;
    compile_fn compile;
    free_fn compiled_free;
    new_fn context_new;
    free_fn context_free;
    set_fn context_set;
    set_int64_fn context_set_int64;
    get_int64_fn context_get_int64;
    set_int64_fn context_set_param_int64;
    get_int64_fn context_get_param_int64;
    eval_number_fn eval_number;
    void* parser;
    void* ast;
    void* program;
    void* context;
    double value = 0.0;
    bool found = false;
    const int64_t exact = INT64_C(9007199254740993);
    assert(library);
#define LOAD(target, name) do { *(void**)(&target) = symbol(library, name); } while (0)
    LOAD(parser_new, "cxpr_expr_parser_new");
    LOAD(parser_free, "cxpr_expr_parser_free");
    LOAD(parse, "cxpr_expr_ast_parse");
    LOAD(ast_free, "cxpr_expr_ast_free");
    LOAD(compile, "cxpr_expr_compile");
    LOAD(compiled_free, "cxpr_expr_compiled_free");
    LOAD(context_new, "cxpr_context_new");
    LOAD(context_free, "cxpr_context_free");
    LOAD(context_set, "cxpr_context_set");
    LOAD(context_set_int64, "cxpr_context_set_int64");
    LOAD(context_get_int64, "cxpr_context_get_int64");
    LOAD(context_set_param_int64, "cxpr_context_set_param_int64");
    LOAD(context_get_param_int64, "cxpr_context_get_param_int64");
    LOAD(eval_number, "cxpr_expr_compiled_eval_number");
#undef LOAD
    parser = parser_new();
    ast = parse(parser, "price * 2 + 1", NULL);
    program = compile(ast, NULL, NULL);
    context = context_new();
    assert(parser && ast && program && context);
    context_set(context, "price", 20.5);
    assert(eval_number(program, context, NULL, &value, NULL));
    assert(value == 42.0);
    context_set_int64(context, "order_id", exact);
    assert(context_get_int64(context, "order_id", &found) == exact && found);
    context_set_param_int64(context, "step", exact + 2);
    assert(context_get_param_int64(context, "step", &found) == exact + 2 && found);
    context_free(context);
    compiled_free(program);
    ast_free(ast);
    parser_free(parser);
    assert(dlclose(library) == 0);
    puts("P/Invoke ABI smoke: number and exact int64 symbols passed");
    return 0;
}
