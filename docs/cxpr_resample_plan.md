# Provider-backed `resample` plan

## Goal

Add a domain-neutral temporal series selector to `.cxpr`:

```cxpr
hourly_close = resample(close, every="1h")
previous_hourly_close = resample(close, every="1h")[1]
path_500ms_ago = resample(path, every="500ms")[1]
```

The same model must retain its semantics in AST evaluation, compiled IR,
generated C, and CUDA.

Generated C is the production CPU runtime. AST evaluation and compiled IR are
build-time/parity references and fallback diagnostics, not the intended hot
path for deployed models.

## Decided contract

- `resample(source, every)` is the canonical name and call shape.
- Positional and named interval forms are equivalent:
  - `resample(close, "1h")`
  - `resample(close, every="1h")`
- `every` is a compile-time duration literal. cxpr parses and normalizes it;
  providers do not parse the source text.
- The first implementation supports fixed-duration units: `ns`, `us`, `ms`,
  `s`, `m`, `h`, and `d`.
- Postfix indexing is relative to the selected materialized series:
  `resample(path, "500ms")[1]` selects the previous 500 ms sample, while
  `[2]` is approximately one second earlier on aligned buckets.
- `resample` selects a provider-backed, already materialized series. cxpr does
  not aggregate raw samples into buckets.
- No expression-level `method` argument is included. Materialization semantics
  such as last, sum, mean, or interpolation belong to provider source metadata
  and configuration.
- cxpr discovers, normalizes, deduplicates, and binds series requirements at
  build/planning time.
- Production CPU execution uses generated C with pre-bound series data and
  alignment inputs; it does not call provider callbacks in the hot path.
- The host resolves planning handles to bound arrays and alignment metadata
  before execution. Generated C and CUDA do not use provider callbacks in their
  hot paths.

## Responsibility boundary

### cxpr

- Parse and validate `resample` calls and duration literals.
- Produce normalized and deduplicated series requirements.
- Preserve series-relative lookbacks.
- Bind requirements to opaque provider handles or generated input slots.
- Lower identical semantics to interpreter, IR, generated C, and CUDA.
- Report unsupported intervals, sources, target features, and missing history
  with useful source locations.

### Provider

- Declare which sources can be materialized at other intervals.
- Declare each source's materialization semantics where relevant.
- Bind a normalized requirement to a stable handle.
- Expose value type and capabilities, including record/vector support.

### Host

- Load, obtain, or materialize the concrete series requested by the provider.
- Own series storage and lifetime.
- Align evaluation timestamp/cursor to the selected series cursor.
- Resolve a handle plus series-relative lookback to a value.
- Upload arrays and cursor/alignment maps before CUDA execution.

## Proposed requirement API

Exact public names may change during implementation, but the boundary should
carry structured values rather than interval strings:

```c
typedef struct cxpr_resample_interval {
    int64_t duration_ns;
    const char* canonical;
} cxpr_resample_interval;

typedef struct cxpr_series_requirement {
    uint64_t requirement_id;
    const char* source_name;
    cxpr_resample_interval every;
    cxpr_value_type value_type;
} cxpr_series_requirement;
```

Planning/binding:

```c
bool bind_series(const cxpr_series_requirement* requirement,
                 uint64_t* out_handle,
                 void* userdata);
```

Reference evaluator/fallback resolution, conceptually:

```c
bool resolve_series_value(uint64_t handle,
                          int64_t evaluation_time_ns,
                          size_t lookback,
                          cxpr_value* out,
                          void* userdata);
```

This callback is useful for parity tests and fallback evaluation, not the
production generated-C hot path. The existing cursor-based view API may be
extended for that role if it can preserve series-relative indexing and
timestamp alignment without host-specific logic in cxpr.

### Reference resolver contract

- Series timestamps supplied by a host are monotonic non-decreasing event time.
- Alignment selects the greatest materialized timestamp less than or equal to
  the evaluation timestamp; it never selects a future sample.
- When timestamps are duplicated, the last sample at that timestamp wins.
- Lookback is applied after alignment and counts distinct target-series
  timestamps, not host evaluation rows.
- Missing alignment or insufficient lookback history returns numeric `NAN` from
  the cxpr reference helper.
- Provider metadata and callback arguments are borrowed for the duration of a
  planning/resolver call. Returned manifests own immutable copies of their
  strings and may be inspected concurrently after planning.
- Hosts own materialized storage and handles, and must synchronize callback and
  storage access when reference evaluation is used concurrently.
- Generated C remains the production runtime and consumes pre-bound arrays and
  alignment data rather than invoking the resolver callback per tick.

## Implementation checklist

### 1. Syntax contract and fixtures

- [x] Add a multi-domain fixture covering trading, telemetry, operations, and
  gaming.
- [x] Cover `resample(source, interval)[n]` in the AST fixture.
- [x] Add named-form fixture coverage for `every="1h"`.
- [x] Add invalid fixtures: missing source, non-literal interval, zero/negative
  interval, unknown unit, overflow, and unsupported calendar interval.
- [x] Decide whether interval strings are case-sensitive; keep emitted canonical
  forms lower-case.

### 2. Duration parsing

- [x] Add a standalone fixed-duration parser in `libs/cxpr`.
- [x] Normalize valid literals to `duration_ns` plus canonical text.
- [x] Detect arithmetic overflow and reject fractional values initially.
- [x] Add unit tests for all supported units and boundary values.
- [x] Keep calendar intervals and timezone/DST semantics explicitly unsupported
  in the first version.

### 3. AST and semantic validation

- [x] Register `resample` as a cxpr-owned series transformation rather than an
  ordinary scalar runtime function.
- [x] Bind positional and named arguments to the same semantic fields.
- [x] Require a provider/source-capable first argument syntactically; provider
  capability validation remains part of provider binding.
- [x] Require `every` to be a compile-time string literal.
- [x] Infer the result element type from provider source metadata.
- [x] Preserve the enclosing model binding source span for resample diagnostics.
- [x] Add a general expression-AST source-span contract and distinct spans for
  the resample source and interval literal, including span-based diagnostics.
- [x] Ensure expression aliases retain the transformation.
- [x] Ensure model imports retain the transformation without registering
  `resample` as an ordinary runtime function.

### 4. Source plan and requirement manifest

- [x] Add a source-plan node or equivalent normalized representation for
  provider-backed resampling.
- [x] Store normalized interval metadata, not merely the original string.
- [x] Include source identity, interval, value type, and provider identity in
  the stable requirement id.
- [x] Deduplicate identical requirements across bindings and outputs.
- [x] Ensure different intervals never collide.
- [x] Emit an inspectable requirement manifest for tooling and hosts.
- [x] Lower the existing scoped form `close(timeframe="1h")` and the new form
  `resample(close, every="1h")` to equivalent requirements during migration.

### 5. Provider and host APIs

- [x] Add provider capability metadata for resampleable sources.
- [x] Represent provider materialization policy without making it part of the
  expression call.
- [x] Add a binder hook receiving the normalized requirement.
- [x] Add or extend a resolver hook for timestamp/cursor plus lookback.
- [x] Define missing-history behavior (`NAN`) for reference evaluation.
- [x] Define monotonicity, duplicate timestamp, and alignment expectations.
- [x] Document ownership and thread-safety of handles and materialized storage.
- [x] Expose a typed provider-backed `bars` source for whole-record OHLCV
  requirements used by multi-source indicators.
- [x] Add Dynasty binding of normalized requirements to existing materialized
  timeframe handles without expression scanning.
- [x] Run the defined Dynasty ATR/ADX parity test for
  `resample(bars, every="1h")` after the shared build is available.

### 6. Lookback semantics

- [x] Make `[n]` index the selected series, not the primary host tick stream.
- [x] Support warmup/missing history consistently across all backends.
- [x] Verify nested use such as `rsi(resample(close, "1h"), 14)`.
- [x] Verify explicit lookbacks such as `resample(close, "1h")[1]`.
- [x] Verify lookbacks on derived EMA bindings that reference a resampled series.
- [x] Reject negative, fractional, non-finite, and overflowing indexes using the
  existing neutral index contract.

### 7. Evaluator and IR

- [x] Add a reference evaluator path that resolves bound series handles.
- [x] Keep IR as a parity/reference backend using the bound call plus native
  lookback resolver. Do not add a dedicated opcode while generated C/CUDA are
  the production runtimes; their requirement-slot loads remain fully static.
- [x] Encode the bound requirement slot through normalized manifest lookup and
  preserve series-relative lookback in the existing IR resolver contract.
- [x] Include resample requirements in compiled-program ownership; generated
  artifacts retain them after the parsed model is freed.
- [x] Include resample requirements if compiled-model snapshot serialization is
  designed. N/A for the current snapshot API, which captures evaluator state
  and references its compiled program rather than serializing program metadata.
- [x] Add tree evaluator versus IR parity tests over aligned and missing data,
  including multiple requirement intervals.

### 8. Generated C

- [x] Define generated function inputs for materialized series and alignment
  metadata.
- [x] Generate bounds-safe series-relative loads.
- [x] Make generated C the production CPU runtime for resample-enabled models.
- [x] Keep generated C independent of provider callbacks; all series must be
  bound ahead of execution.
- [x] Keep planning handles out of the per-tick ABI where direct generated input
  slots/pointers suffice.
- [x] Verify the generated artifact can be compiled and loaded through the
  existing generated-model ABI.
- [x] Count repeated pure resample loads by `(requirement slot, lookback)` and
  emit one per-tick local when usage is at least two.
- [x] Reuse one aligned target-series cursor per repeated requirement/lookback
  key within a tick.
- [x] Share the same cache/CSE plan with CUDA while avoiding runtime hash maps.
- [x] Keep non-pure, stateful, context-dependent, and history-rewriting calls
  outside the generic cache unless capability metadata explicitly permits it.
- [x] Emit readable errors when a value type or alignment mode is unsupported.
- [x] Add AST/tree, IR, and generated-C parity tests.

### 9. CUDA

- [x] Define a stable CUDA ABI for each bound series: data pointer, count, value
  type, and evaluation-to-series alignment map or equivalent cursor data.
- [x] Generate device-side bounds-safe series-relative loads.
- [x] Deduplicate device buffers for repeated identical requirements.
- [x] Cover a manifest-driven CUDA ABI contract with independent 30m, 5m, 1h,
  and 1d requirement slots, pointer-table buffers, alignment maps, counts, and
  an order-sensitive cache key.
- [x] Wire the manifest ABI into the emitted kernel signature and launch path.
  The production batch path now uploads flat value/map/count tables and passes
  them to both generated kernels; legacy input fields remain adapters only.
- [x] Require host alignment maps to expose only finalized target buckets, with
  no lookahead from the primary cursor.
- [x] Support scalar numeric series first.
- [x] Explicitly reject record/vector series until their CUDA ABI is defined.
- [x] Add CPU versus CUDA parity for current value, `[1]`, warmup, gaps, and
  multiple intervals.
- [x] Remove Dynasty's current one-extra-timeframe CUDA limitation only after
  the generic manifest supports multiple requirements.
- [x] Materialize one CUDA input buffer per unique `(symbol, requirement id)` so
  a strategy can consume primary bars plus 5m, 1h, 1d, or other intervals in
  the same launch without a fixed extra-timeframe slot.
- [x] Provide a separate primary-to-target cursor/alignment map for every
  resampled requirement; never reuse the 5m mapping for an HTF series.
- [x] Lower indicators whose source is `resample(...)` to their bound CUDA
  requirement slot, including scalar OHLCV sources and typed `bars` inputs.
  Scalar EMA is implemented with target-series-relative cursor, warmup, and
  `[1]`; typed `bars` indicators and device parity remain open.
- [x] Define and test the no-lookahead contract: an HTF cursor may address only
  the latest completed target bar at the primary event timestamp unless the
  provider explicitly declares a different safe policy.
- [x] Include the ordered requirement manifest (ids, value types, buffers, and
  alignment maps) in CUDA cache keys and generated kernel signatures.
- [x] Add a Dynasty CUDA fixture with 30m primary, 5m execution, 1h trend, and
  1d regime inputs, including boundary ticks around HTF completion. A runnable
  NVRTC/GPU focus target exists as `test_dyn_cuda_resample_runtime`, but this
  item remains open until its device launch succeeds on a CUDA host.

### 10. Dynasty parity and migration

- [x] Reuse Dynasty's existing symbol `.cxpr` and feed builder to materialize
  requested series.
- [x] Adapt Dynasty's provider to bind normalized cxpr requirements instead of
  scanning expressions for timeframe strings.
- [x] Compare `close(timeframe="1h")` with
  `resample(close, every="1h")` on identical bars.
- [x] Cover current value, `[1]`, indicator input, gaps, warmup, and bucket
  boundaries.
- [x] Run parity through CPU evaluator, IR, generated C, and CUDA.
- [x] Migrate strategy fixtures only after parity succeeds. The first active
  migration is `bollinger_kst_adx_confirmed_breakaway_5m.yaml`: direct OHLCV
  requests plus Bollinger/KST/EMA source arguments now use `resample(...)`.
- [x] Make an explicit release decision for host-style timeframe arguments.
  Decision (2026-08-02): retain compatibility and do not emit deprecation
  warnings in this release. An active-consumer audit still finds 28 strategy
  files (197 expression lines) using legacy positional or named timeframe
  forms, chiefly implicit multi-field `adx`/`atr` calls and related BKADX
  fixtures. Deprecation requires a source-aware replacement for those
  multi-field indicators and a zero-consumer audit; model/symbol-level
  `timeframe` metadata is not part of this syntax deprecation.

### 11. Benchmarks

- [x] Establish baselines for today's scoped timeframe implementation before
  replacing any strategy syntax.
- [x] Measure build/planning time for requirement discovery, normalization,
  deduplication, and provider binding.
- [x] Measure CPU evaluator and compiled-IR throughput for current values and
  series-relative lookbacks.
- [x] Measure generated-C throughput and code size against the existing
  timeframe path.
- [x] Measure CUDA kernel throughput, upload/alignment overhead, and device
  memory for one and multiple resampled series.
- [x] Include at least these workloads: one interval/current value, one
  interval with `[1]`, nested indicator input, repeated deduplicated requests,
  multiple intervals, warmup/gaps, and a non-trading source.
- [x] Report median and tail latency where relevant, throughput, allocations,
  peak memory, generated artifact size, and CUDA transfer time separately from
  kernel time.
- [x] Run benchmarks in Release builds with fixed datasets and record compiler,
  CPU/GPU, dataset size, warmup, and repetition count.
- [x] Add a checked-in benchmark runner and a concise Markdown/JSON result
  format suitable for before/after comparison.
- [x] Set an initial regression gate: no material CPU/C/CUDA throughput
  regression versus scoped timeframe without an explicitly documented reason.

### 12. Documentation and release guardrails

- [x] Document that `resample` selects rather than aggregates a series.
- [x] Document provider materialization policy with examples for close, volume,
  temperature, requests, and path.
- [x] Document `[n]` as target-series-relative.
- [x] Add an inspection example showing generated requirements and handles.
- [x] Version any generated-model, plugin, snapshot, and CUDA ABI changes.
- [x] Keep clear fallback errors until every execution backend is supported.

## Initial acceptance criteria

- Both positional and named syntax parse and normalize identically.
- Production CPU execution uses generated C with no provider callback in the
  evaluation hot path.
- Existing scoped-timeframe and `resample` forms produce equivalent bound
  requirements for Dynasty trading sources.
- `[1]` resolves the previous target-series sample on CPU and CUDA.
- `rsi(resample(close, every="1h"), 14)` has parity with today's pre-aggregated
  Dynasty timeframe implementation.
- At least one non-trading scalar fixture has evaluator/IR/C/CUDA parity.
- Unsupported record/vector resampling fails explicitly rather than silently
  changing semantics.
- Release benchmark results cover planning, CPU/IR, generated C, and CUDA, and
  satisfy the documented regression gate.

## Deferred work

- Calendar months, quarters, years, timezone alignment, and DST.
- Runtime-computed intervals.
- cxpr-owned bucket aggregation of raw samples.
- Dynamic creation or mutation of provider series during evaluation.
- Record/vector CUDA ABI beyond an explicitly designed first version.
