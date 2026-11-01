# IR optimization gating iteration 1

- Iteration: 1
- Hypothesis: compile-time binding of the built-in column resolver plus a guarded numeric
  scalar operation will remove callback, AST-index construction, typed-stack, and name-lookup
  costs from constant numeric lookbacks.
- Changed: `src/registry/lookback.c`, `src/registry/internal.h`, `src/ir/compile/node.c`,
  `src/ir/internal.h`, `src/ir/exec/{api,fast,internal,typed}.c`, `src/ir/{compile,program,validate}.c`,
  `tests/column_lookback.test.c`, and `benchmarks/ir_bench.c`.
- Benchmark command: `for i in 1 2 3 4 5 6 7 8 9; do
  ./build/benchmarks/cxpr_bench_ir --case <lookback_leaf|lookback_mixed>; done`.
- Environment: baseline `76d477b`, after based on `76d477b` plus this uncommitted iteration;
  GCC 13.3.0, Release, AMD Ryzen 7 5800H, 16 logical CPUs. Frequency governor was not pinned.
- Before: leaf median 142.57 ns, p95 153.25 ns; mixed median 247.46 ns, p95 267.50 ns.
- After: leaf median 14.30 ns, p95 25.85 ns; mixed median 17.71 ns, p95 18.60 ns.
- Delta: leaf -128.27 ns (-89.97%), 9.97x; mixed -229.75 ns (-92.84%), 13.97x.
  Throughput improves from 7.01 to 69.93 million leaf eval/s (+62.92 million, +897.1%) and
  from 4.04 to 56.47 million mixed eval/s (+52.43 million, +1297.8%).
- Correctness: 138/138 Release CTest entries passed (one documented skip); focused
  ASan/UBSan tests `column_lookback`, `history_index`, `ir`, and `ir_exec_fast` passed with
  leak detection disabled because LeakSanitizer cannot run under the environment's ptrace
  wrapper. Array override, registry replacement, warmup, and out-of-range guards pass.
- Assessment: keep. Both representative cases exceed the Phase 4 5x target.
- Next hypothesis: resolved read slots can remove repeated hash/cache validation from scalar
  programs while keeping mutable state in an evaluator/session rather than the compiled program.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `lookback_leaf` | 142.57 ns | 14.30 ns | -128.27 ns | -89.97% | 9.97x | keep |
| `lookback_mixed` | 247.46 ns | 17.71 ns | -229.75 ns | -92.84% | 13.97x | keep |

## Broader benchmark smoke

- `cxpr_bench_model`: completed; RSI fused IR 738.14 ns/op and MACD record 1994.55 ns/op.
- `cxpr_bench_index_history`: completed; `close[1]` IR 73.71 ns/eval and producer field
  1258.01 ns/eval. This harness uses a separate path and remains a Phase 3/4 follow-up.
- `cxpr_bench_resample`: completed; scoped IR 51.47 ns/eval, resample IR 48.00 ns/eval.
- `cxpr_bench_engine`: exceeded the 30-second gating timeout before emitting buffered results.
- Hardware counters: unavailable (`perf: command not found`).

The benchmark harness now binds current-value writes through context slots, so the reported
lookback latency excludes unrelated hash-based input mutation and is directly comparable to
the generated-C timing loop.
