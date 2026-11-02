# Native collection benchmark snapshot

Nine-trial median snapshot. The manual forms are the pre-feature source-level
baseline; native and manual cases run in the same optimized benchmark binary so
the delta isolates lowering shape instead of build or machine drift.

Commands:

```sh
cmake -S . -B build
cmake --build build --target cxpr_bench_ir -j1
for trial in 1 2 3 4 5 6 7 8 9; do
  for case in static_map_records_manual static_map_records \
    static_reduce_records_manual static_reduce_records static_instantiate; do
    ./build/benchmarks/cxpr_bench_ir --case "$case"
  done
done
```

All collection cases use 1,000 compile/free iterations and 200,000 evaluation
iterations. AST, IR, and generated-C totals must match, and the benchmark exits
if an IR `CALL_AST` fallback remains.

| Case | AST ns | IR ns | C ns | Compile ns | IR instructions | Fallbacks |
|---|---:|---:|---:|---:|---:|---:|
| map manual | 46.88 | 43.61 | 2.78 | 1,155 | 14 | 0 |
| map native | 7,001.91 | 64.88 | 2.95 | 3,677 | 28 | 0 |
| reduce manual | 49.49 | 45.62 | 2.91 | 1,749 | 16 | 0 |
| reduce native | 8,239.89 | 65.63 | 2.94 | 3,367 | 28 | 0 |

Native versus manual deltas from this run:

- Map: IR +21.27 ns (+48.8%), generated C +0.17 ns (+6.1%), compile
  +2,522 ns (+218.4%), and 14 additional IR instructions.
- Reduce: IR +20.01 ns (+43.9%), generated C +0.03 ns (+1.0%), compile
  +1,618 ns (+92.5%), and 12 additional IR instructions.

The native AST path intentionally demonstrates the cost avoided by native IR;
it is not the production target. Generated C is at parity with the manually
expanded baseline.

The static `instantiate` / `@label` generated-C comparison uses 5,000,000 ticks
per trial because the shorter interval was dominated by scheduler noise:

| Case | C ns/tick | State bytes | Fallbacks |
|---|---:|---:|---:|
| two explicit labeled children | 3.10 | 88 | 0 |
| static `instantiate` | 2.96 | 88 | 0 |

The median delta is -0.14 ns (-4.5%) with no state-size increase. Both
models contain two independent scoped child states; generated output is checked
for absence of `CALL_AST` before timing.

## Stack-fold IR optimization

Commit `a0924f2` was rebuilt in a separate release worktree and compared with
the stack-fold lowering on the same host. Each result is the median of nine
trials. The optimized left fold keeps its accumulator on the evaluator stack;
more complex reducers that reference the accumulator multiple times retain the
general temporary-slot lowering.

| Case | `a0924f2` IR ns | Optimized IR ns | Delta | IR instructions |
|---|---:|---:|---:|---:|
| map native | 59.03 | 45.21 | -23.4% | 28 -> 16 |
| reduce native | 59.70 | 45.67 | -23.5% | 28 -> 16 |

Relative to the manual forms measured in the same runs, native map overhead
fell from 42.7% to 2.5%; native reduce moved from 41.6% slower to 0.4% faster.
Median compile time also fell from 3,909 to 3,458 ns for map (-11.5%) and from
3,375 to 3,095 ns for reduce (-8.3%). Generated C remains the same lowering;
its measured changes (+2.5% map, -1.0% reduce) are within nanobenchmark noise,
and all cases retain zero `CALL_AST` fallback.

### Fixed cost and collection scaling

`cxpr_bench_collection_scaling` measures complete folds, not individual IR
instructions or individual elements. It uses one million evaluations per trial
and reports the median of nine trials:

```sh
cmake --build build --target cxpr_bench_collection_scaling -j1
./build/benchmarks/cxpr_bench_collection_scaling
```

| Elements | Manual ns/fold | Reduce ns/fold | Map/reduce ns/fold | Instructions |
|---:|---:|---:|---:|---:|
| 1 | 14.04 | 14.44 | 15.08 | 6 |
| 2 | 21.47 | 21.19 | 21.20 | 10 |
| 4 | 34.97 | 35.05 | 34.94 | 18 |
| 8 | 62.12 | 62.02 | 62.25 | 34 |
| 16 | 139.33 | 141.20 | 139.03 | 66 |

The three forms lower to the same instruction stream at every size. Up to eight
elements the shared evaluator cost is approximately 7 ns fixed plus 7 ns per
element on this host; the 16-element row also includes the larger dispatch and
instruction-cache footprint. There is no remaining collection-specific
per-element overhead.

The scalar executor previously cleared its entire compiler-temporary scratch
array on every call, including programs with no temporaries. Removing that
definition-dominated initialization reduced the four-element model benchmarks
again: map from 45.21 to 29.50 ns/fold (-34.7%) and reduce from 45.67 to
29.47 ns/fold (-35.5%). Relative to the original `a0924f2` baseline, the total
improvement is -50.0% for map and -50.6% for reduce. Generated C is unaffected.

## Named record map

The named-record extension is measured with four independent scoped child
models. The baseline spells out `main`, `back`, `left`, and `right` calls; the
native form maps the same names and reads the resulting named fields. Each of
nine trials executes 5,000,000 generated-C ticks.

| Case | Median C ns/tick | State bytes |
|---|---:|---:|
| Four explicit child calls | 4.71 | 168 |
| `map({main=..., back=..., left=..., right=...}, ...)` | 4.71 | 168 |

The measured delta is 0.00 ns (0.0%) with no state-size increase. Generated-C
totals match on every trial, and neither generated fixture contains `CALL_AST`.

The pure scalar extension uses the same four named record entries but maps each
entry to a number rather than a child model:

| Case | Median C ns/tick | State bytes |
|---|---:|---:|
| Four explicit scalar bindings | 2.92 | 1 |
| Pure `map({main=..., back=..., left=..., right=...}, ...)` | 2.93 | 1 |

The pure named-map delta is +0.01 ns (+0.3%), within nanobenchmark noise, with
identical output totals, no state-size increase, and no `CALL_AST`. The model
test additionally forces the scalar form through fused IR and covers nested
record results through the session runtime and generated C.

The dedicated fused-model IR benchmark runs one million ticks per trial and
1,000 compile/free cycles per trial, reporting nine-trial medians:

```sh
cmake --build build --target cxpr_bench_named_map_ir -j1
./build/benchmarks/cxpr_bench_named_map_ir
```

| Case | IR ns/tick | Compile ns | IR instructions |
|---|---:|---:|---:|
| Four explicit scalar bindings | 64.73 | 14,734 | 24 |
| Pure named map | 75.13 | 23,757 | 30 |

Named map is therefore +10.40 ns/tick (+16.1%) in the model IR session and
9,023 ns (+61.2%) to compile, with six additional instructions. This is a
real IR materialization cost even though generated C optimizes the same graph
to parity. Both cases are forced through `CXPR_MODEL_BACKEND_IR`; the benchmark
aborts if the fused fast path is unavailable or outputs differ.
