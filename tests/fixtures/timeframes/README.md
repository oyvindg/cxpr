# Timeframe syntax contract

The canonical syntax is:

```cxpr
hourly_value = resample(value, "1h")
previous_hourly_value = resample(value, "1h")[1]
```

`resample` is a domain-neutral temporal series selector. The first argument is
a series expression and the second is a compile-time interval literal.
Lower-case fixed-duration units are canonical:
`ns`, `us`, `ms`, `s`, `m`, `h`, and `d`.
Unit spelling is case-sensitive. Calendar units and timezone/DST alignment are
intentionally unsupported by the initial fixed-duration contract.

The host/provider owns acquisition and optional pre-aggregation of concrete
series. cxpr owns parsing, source planning, dependency wiring, and build-time
lowering to a bound series input. This boundary lets interpreter, IR, generated
C, and CUDA consume identical materialized values without embedding an
aggregator in each backend.

A postfix lookback applies after series selection. Consequently,
`resample(value, "1h")[1]` means the previous element of the materialized
hourly series, not the value from one host tick ago. Lowering must preserve
that series-relative index for AST evaluation, IR, generated C, and CUDA.

The same rule applies outside trading. `resample(path, "500ms")[1]` selects
the previous sample in the 500 ms path series. On aligned buckets `[2]`, not
`[1]`, is approximately one second earlier.

Until lowering is implemented, `multi_domain.cxpr` is an AST syntax fixture.
The contract test intentionally verifies parsing and expression shape without
claiming runtime/codegen support prematurely.

Planned parity coverage will bind the same pre-aggregated series through both:

- the existing scoped form, for example `close(timeframe="1h")`; and
- the canonical composable form, `resample(close, "1h")`.

The two forms must produce identical source plans and values before the scoped
form can be deprecated.

The scoped form is therefore still supported and is not deprecated yet. Hosts
should report an unbound requirement explicitly in reference evaluation, and
generated targets validate missing value/alignment buffers before execution.
Strategy migration remains gated on Dynasty CUDA manifest parity; CPU parity
alone is not sufficient to replace active strategy syntax.
