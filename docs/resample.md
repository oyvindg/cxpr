# Provider-backed resampling

`resample` selects a provider-backed series at another fixed temporal
resolution. It does not aggregate raw samples inside cxpr:

```cxpr
hourly_close = resample(close, every="1h")
previous_hourly_close = hourly_close[1]
path_1s_ago = resample(path, every="500ms")[2]
```

The positional form `resample(close, "1h")` is equivalent. cxpr parses the
duration at planning time and gives the host normalized nanoseconds and
canonical text. `[n]` indexes the selected series: `[1]` above is the previous
hourly close, not the previous host tick.

## Provider materialization

The provider declares whether a source is resampleable and how a host-created
variant is materialized. This policy is metadata, not an expression argument:

| Source | Example policy | Materialized sample |
|---|---|---|
| `close` | `LAST` | Last close in the provider bucket |
| `volume` | `SUM` | Sum of volume in the bucket |
| `temperature` | `MEAN` | Mean sensor value in the bucket |
| `requests` | `SUM` | Request count in the bucket |
| `path` | `LINEAR` | Provider-interpolated position at the boundary |

These are examples, not cxpr defaults. A provider declares its actual policy;
the host owns loading or constructing the arrays.

```c
static const cxpr_provider_source_spec close_source = {
    .name = "close",
    .scope = &timeframe_scope, /* Legacy migration compatibility. */
    .value_type = CXPR_VALUE_NUMBER,
    .flags = CXPR_PROVIDER_SOURCE_RESAMPLE,
    .materialization = CXPR_PROVIDER_MATERIALIZE_LAST,
};
```

## Planning and manifest inspection

Use the normalized binder for new integrations. It runs while building the
model, never in the generated-C per-tick hot path:

```c
static int bind_requirement(const cxpr_series_requirement* req,
                            uint64_t* out_handle,
                            void* userdata) {
    host_series_store* store = userdata;
    *out_handle = host_find_or_materialize(
        store, req->source_name, req->every.duration_ns,
        req->materialization, req->value_type);
    return *out_handle != 0;
}

cxpr_plan_config config = {
    .userdata = &store,
    .bind_requirement = bind_requirement,
};
cxpr_source_plan_bindings manifest = {0};

if (!cxpr_model_plan_bind_sources(model, provider, context, registry,
                                  &config, &manifest, &error)) {
    /* Report error.message to the caller. */
}

for (size_t i = 0; i < manifest.count; ++i) {
    const cxpr_series_requirement* req = &manifest.requirements[i];
    inspect(req->requirement_id, req->provider_name, req->source_name,
            req->every.canonical, req->every.duration_ns,
            req->value_type, manifest.handles[i]);
}

cxpr_free_source_plan_bindings(&manifest);
```

Identical requirements are deduplicated. `requirement_id` includes provider,
source, normalized duration, and value type, but excludes lookback. Current
value and `[1]` therefore share storage/input binding while using different
loads.

The manifest owns its copied provider/source strings until it is freed.
Provider metadata and binder arguments are borrowed during their calls. Handles
and arrays remain host-owned. The immutable manifest can be inspected
concurrently after planning; the host synchronizes its own storage and hooks.

## Alignment and fallback evaluation

Generated C consumes pre-bound arrays and alignment data. The reference
resolver exists for tests and fallback parity:

The generated-C/CUDA view ABI is versioned by
`CXPR_RESAMPLE_VIEW_ABI_VERSION`. Version 1 is deliberately scalar numeric:
each manifest slot maps to one `double` data pointer, its element count, one
primary-cursor-to-series-cursor alignment pointer, and its primary count.
`CXPR_RESAMPLE_VIEW_VALUE_TYPE` records that fixed type, and
`CXPR_RESAMPLE_ALIGNMENT_MISSING` is the missing-alignment sentinel. Record and
vector views have no version-1 representation and must be rejected before code
generation/upload. Planning handles are not transported per tick.

Call `cxpr_validate_generated_resample_bindings()` on the owned manifest before
constructing generated-C/CUDA views. It rejects `STRUCT`/`ARRAY` requirements
with an explicit scalar-only ABI error. Validate every constructed view with
`cxpr_resample_view_validate()`; its status distinguishes a null view, missing
numeric data, and a missing alignment map, and
`cxpr_resample_view_status_message()` provides stable host-facing text.

Generated C and CUDA deduplicate identical provider requirements into one view
slot. Their per-tick CSE is intentionally limited to pure `resample` loads with
the same `(slot, lookback)`; arbitrary provider calls, stateful functions, and
history-rewriting operations are not admitted to this cache.

One symbol may bind several slots at once—for example a 30m primary cursor plus
5m, 1h, and 1d resample views. Every slot owns an independent alignment map;
the generated kernel signature receives the complete ordered view array. The
host must build each map with no lookahead: a primary tick may select only the
last finalized target bucket. Current and `[1]` share the same buffer/map and
differ only by the target-series-relative offset. Indicator lowering must reuse
these manifest slots instead of adding hidden timeframe arguments or buffers.
The same ABI is domain-neutral: the CUDA contract fixture deliberately uses a
`path` series for its 1d slot alongside trading-style close series.

- timestamps are monotonic non-decreasing event time;
- alignment selects the last timestamp at or before evaluation time;
- the last value wins for duplicate timestamps;
- lookback applies after alignment and counts target-series timestamps;
- missing alignment/history produces numeric `NAN`.

Calendar intervals, timezone/DST buckets, runtime-computed intervals, and
cxpr-owned raw aggregation are outside the fixed-duration contract.

## Migration from scoped timeframes

During migration these forms produce equivalent provider requirements:

```cxpr
legacy = close(timeframe="1h")
current = resample(close, every="1h")
```

1. Add resample capability and materialization metadata to provider sources.
2. Bind and inspect normalized requirements; retain the legacy binder as a
   compatibility fallback if needed.
3. Materialize both expressions from identical host arrays.
4. Verify current value, `[1]`, warmup, gaps, bucket boundaries, indicator
   inputs, generated C, and CUDA parity.
5. Replace active model/strategy syntax only after parity succeeds.
6. Remove legacy timeframe scanning only after every consumer uses the
   normalized manifest.

Never silently use a primary/default series when an interval, type, policy, or
backend representation is unavailable. Fail planning/code generation with a
readable capability error.

## Backend and diagnostic contract

Warmup, missing history, a missing alignment entry, and an out-of-range
target-series lookback all produce numeric `NAN` in the reference tree/IR,
generated C, and CUDA paths. The focused parity suite is split across
`resample_backend_contract`, `generated_resample_parity`, and the real-device
`cuda_resample_runtime` harness.

Unsupported generated backends fail before execution:

- `cxpr_validate_generated_resample_bindings()` rejects record/vector views;
- `cxpr_resample_view_validate()` distinguishes missing values and alignment;
- code generation rejects non-runnable aggregate/dynamic-index fallbacks.

The current snapshot API captures evaluator state and retains a reference to
its compiled program; it does not serialize compiled-model metadata. Resample
requirements are therefore owned by the compiled program and remain available
after the parsed model is freed. If compiled-program serialization is added,
the ordered requirement manifest must become part of that format and its ABI
version.

## Reproducible benchmark

Run `benchmarks/run_resample_bench.sh`. It configures a Release build and emits
Markdown, JSON metadata, and raw output under `benchmarks/results/`. The fixed
workload records the legacy scoped-timeframe AST/IR baseline, resample AST/IR,
the direct generated ABI, repeated-load CSE, planning time, and artifact size.
The initial CPU regression gate is 15% against an accepted same-machine
baseline. CUDA kernel/transfer/device-memory measurements are a separate gate
and must be produced on a CUDA-capable runner; an unavailable GPU is recorded,
not treated as a successful CUDA benchmark.
`tests/cuda_resample_runtime.cu` is the checked-in CUDA benchmark harness. After
its correctness launches it reports three-view transfer time, kernel time,
launch count, and exact fixture allocation bytes separately.
