# Scoped Sources

This example shows the smallest useful host-facing `cxpr` setup:

- define one scope family, here `selector`
- expose one scoped source, here `close`
- let the host resolve scoped source requests through a callback

This is the low-level runtime registration path. The preferred host-facing form is usually higher-level, such as `close(selector="1d")[7]`. In that case:

- `selector="1d"` identifies one scoped series variant
- `[7]` is a normal `cxpr` lookback on that scoped series
- the bridge stays agnostic about what `"1d"` actually means

## Why This Exists

The bridge core should not know how a host stores or fetches series data. It only needs enough metadata to:

- register callable names in the `cxpr_registry`
- preserve argument shape for parsing and validation
- hand resolution back to the host at evaluation time

That is what `cxpr_scoped_source_functions_register(...)` provides.

## Example

The runnable snippet lives in [`scoped_sources.c`](scoped_sources.c). Its central setup looks like this:

```c
static const cxpr_provider_scope_spec kScope = {
    "selector",
    1,
};

static const cxpr_scoped_source_spec kSources[] = {
    {"close", 0u, 1u, &kScope},
};

const cxpr_scope_resolver resolver = {
    .resolve = example_resolve,
    .userdata = (void*)&kValues,
};

cxpr_scoped_source_functions_register(
    reg, kSources, CXPR_ARRAY_COUNT(kSources), &resolver, NULL);
```

That registration makes `close(...)` visible to `cxpr` as a runtime-resolved name. When evaluated with the argument `7`, the host callback receives:

- `handle = 7`
- `source_name = "close"`

The callback can then map that request to whatever backing store it uses.

The actual callback lives in [`scoped_sources.c`](scoped_sources.c) as `example_resolve(...)`. It is the function that turns:

- `handle = 7`
- `source_name = "close"`

into the concrete value `103.75`.

## Relationship To Higher-Level DSLs

`close(7)` is intentionally mechanical. It demonstrates the minimal runtime surface the bridge core needs in order to bind scoped sources internally.

The more natural host-facing form is often closer to:

- `close(selector="1d")[7]`
- `temperature(selector="warehouse-a")[3]`
- `requests(selector="eu-west")[10]`

Those all describe the same generic pattern: `source(selector="value")[lookback]`.
Hosts may also choose a domain-specific scope parameter name, such as
`timeframe="1d"`, when the provider metadata declares that name.

## Test Coverage

This example is kept aligned with [`../tests/scope.test.c`](../tests/scope.test.c), which expands the same pattern across multiple source names and asserts the registered arity and runtime values.
