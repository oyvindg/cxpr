# Scoped Sources

Host integrations should normally bind scoped sources through
`cxpr_plan_bind_sources(...)` and `cxpr_plan_config`. That API owns the planning
workflow:

- walk the parsed expression AST
- parse provider source-plan subtrees
- evaluate numeric bound arguments
- call the host binder for each materializable source leaf
- register provider-declared scoped source functions with the host resolver

The preferred host-facing form is usually higher-level, such as
`close(timeframe="1d")[7]`. In that case:

- `timeframe="1d"` identifies one scoped series variant
- `[7]` is a normal `cxpr` lookback on that scoped series
- the bridge stays agnostic about what `"1d"` actually means

## Why This Exists

The bridge core should not know how a host stores or fetches series data. It only
needs enough metadata to:

- register callable names in the `cxpr_registry`
- preserve argument shape for parsing and validation
- hand resolution back to the host at evaluation time

`cxpr_plan_bind_sources(...)` is the public integration path for that. It lets
the host map parsed source-plan nodes to stable handles at plan time, then uses a
resolver callback to turn those handles into current numeric values at evaluation
time.

## Example

The normal setup wires one plan-time binder and one eval-time resolver:

```c
static int my_bind_source(
    const cxpr_source_plan_node* node,
    const double* args,
    size_t arg_count,
    uint64_t* out_handle,
    void* userdata) {
    my_series_registry* registry = (my_series_registry*)userdata;

    return my_series_registry_bind(
        registry,
        node->name,
        node->scope_value,
        args,
        arg_count,
        out_handle);
}

static int my_resolve_source(
    uint64_t handle,
    const char* source_name,
    double* out_value,
    void* userdata) {
    my_series_registry* registry = (my_series_registry*)userdata;

    return my_series_registry_current_value(
        registry, handle, source_name, out_value);
}

cxpr_plan_config config = {
    .bind = my_bind_source,
    .resolve = my_resolve_source,
    .userdata = my_series_registry,
};

cxpr_source_plan_bindings bindings = {0};
cxpr_plan_bind_sources(
    &provider, expr_ast, ctx, reg, &config, &bindings, &err);
```

The runnable snippet in [`scoped_sources.c`](scoped_sources.c) demonstrates the
legacy low-level runtime registration that `cxpr_plan_bind_sources(...)` now
drives internally. Its central setup looks like this:

```c
static const cxpr_provider_scope_spec scope = {
    "timeframe",
    1,
};

static const cxpr_scoped_source_spec sources[] = {
    {"close", 0u, 1u, &scope},
};

const cxpr_scope_resolver resolver = {
    .resolve = example_resolve,
    .userdata = (void*)&values,
};

cxpr_scoped_source_functions_register(
    reg, sources, CXPR_ARRAY_COUNT(sources), &resolver, NULL);
```

That registration makes `close(...)` visible to `cxpr` as a runtime-resolved
name. When evaluated with the argument `7`, the host callback receives:

- `handle = 7`
- `source_name = "close"`

The callback can then map that request to whatever backing store it uses.

The actual callback lives in [`scoped_sources.c`](scoped_sources.c) as
`example_resolve(...)`. It is the function that turns:

- `handle = 7`
- `source_name = "close"`

into the concrete value `103.75`.

## Relationship To Higher-Level DSLs

`close(7)` is intentionally mechanical. It demonstrates the minimal runtime
surface the bridge core needs after planning has mapped host-facing scoped
sources to handles.

The more natural host-facing form is often closer to:

- `close(timeframe="1d")[7]`
- `temperature(warehouse="warehouse-a")[3]`
- `requests(region="eu-west")[10]`

Those all describe the same generic pattern:
`source(scope_name="value")[lookback]`. Hosts choose the domain-specific scope
parameter name through provider metadata.

## Test Coverage

The low-level runnable example is kept aligned with
[`../tests/scope.test.c`](../tests/scope.test.c), which expands the same runtime
registration pattern across multiple source names and asserts the registered
arity and runtime values. The higher-level planning path is covered by
[`../tests/source_plan_bind.test.c`](../tests/source_plan_bind.test.c).
