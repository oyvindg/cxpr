# Performance guidance

- Parse and compile once; evaluate many times.
- Reuse contexts or sessions instead of rebuilding them per tick.
- Use context slots and prehashed updates when the host has a stable schema.
- Prefer `cxpr_model_session_tick_fast` for supported in-process scalar models.
- Generate C for production hot loops that need predictable low overhead.
- Specialize stable parameters and selected outputs only when the resulting
  artifact remains representative of the intended workload.
- Benchmark Release builds; debug, sanitizer, and tracing builds are not
  performance references.

Build the benchmarks:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCXPR_BUILD_BENCHMARKS=ON
cmake --build build --target cxpr_bench_ir cxpr_bench_model
./build/benchmarks/cxpr_bench_ir
./build/benchmarks/cxpr_bench_model
```

`cxpr_bench_ir` uses the models in `benchmarks/fixtures/*.cxpr` as the single
source for all three measured paths. It parses each model's `result` binding
for direct AST evaluation, compiles that AST to IR, and builds generated C from
the same file. Before reporting timings it checks result parity between the
paths. The cases cover scalar arithmetic, branching, built-in and defined
functions, deep expression graphs, context updates, typed structs, and
lookback/history access.

`benchmarks/run_all_bench.sh` builds every benchmark in Release, runs repeated
trials, and (when `nvcc` and a supported host compiler are present) the CUDA
bulk benchmark, collecting all logs under one directory:

```bash
benchmarks/run_all_bench.sh            # logs to /tmp/cxpr_bench
TRIALS=10 benchmarks/run_all_bench.sh  # more trials for stable medians
```

## Reference results

Representative figures from one Release run. Absolute numbers are
hardware-specific; the useful signal is the ratio between execution paths.

- Environment: AMD Ryzen 7 5800H, NVIDIA GeForce RTX 3070 Laptop (compute 8.6),
  GCC 13 (CUDA built with `g++-12`), Release build, 2026-09-13.
- Raw logs: `benchmarks/results/perf_baseline_2026-09-13.txt`.

### Expression evaluation — AST vs IR vs generated C (ns/eval)

| Case | AST | IR | generated C | IR/C |
| --- | ---: | ---: | ---: | ---: |
| `simple_arith` | 29.03 | 23.39 | 2.98 | 7.9x |
| `function_call` | 41.60 | 41.58 | 2.94 | 14.1x |
| `complex_signal` | 74.22 | 71.07 | 5.60 | 12.7x |
| `large_arith` | 207.54 | 201.42 | 7.06 | 28.5x |
| `context_churn` | 187.43 | 128.25 | 4.85 | 26.4x |
| `struct_struct_mul_all` (typed struct) | 1899.13 | 1888.34 | 2.91 | 648.8x |
| `lookback_leaf` | 168.78 | 140.52 | 2.82 | 49.8x |
| `lookback_mixed` | 277.90 | 216.67 | 4.89 | 44.3x |

Generated C is consistently the fastest path; IR is a modest win over AST for
scalars and lookback, and the gap widens dramatically once generated C flattens
typed-struct work.

### Index and history lookback (ns/eval)

Median of five Release runs on the same host, 2026-09-16. This focused
benchmark is built as `cxpr_bench_index_history`.

| Case | direct C | AST | IR | generated C | AST/IR |
| --- | ---: | ---: | ---: | ---: | ---: |
| array index | 2.98 | 181.23 | 95.80 | 3.45 | 1.89x |
| `close[1]` | 2.98 | 103.90 | 73.70 | 2.96 | 1.41x |
| producer field `[1]` | — | 1650.20 | 1527.82 | 3.00 | 1.08x |

The resolved IR payload removes per-evaluation target parsing and dispatch
selection. Exact numeric history lookups also bypass temporary AST, reference,
and overlay allocation. Compared with the previous focused baseline,
`close[1]` IR fell from 675.58 to 73.70 ns/eval. Producer-field history still
spends most of its time evaluating the producer in a shifted context.

### Context update paths (ns/op)

| Path | baseline `set` | fast path | speedup |
| --- | ---: | ---: | ---: |
| context slot (`mutate_slot`) | 114.64 | 9.76 | 11.7x |
| prehashed update (`mutate_prehashed`) | 114.64 | 102.82 | 1.1x |

Prefer context slots when the host has a stable schema.

### Model tick — fused IR vs generated C (ns/op)

| Path | ns/op |
| --- | ---: |
| `cxpr_rsi_state` fused IR tick | 423.78 |
| `cxpr_rsi_state` generated C tick | 5.25 |

Generating C for a fixed model removes roughly two orders of magnitude of
per-tick overhead versus the reference runtime.

### Engine overhead (ns/bar, 12.288M ticks/scenario)

| Scenario | evaluator | engine (with watches) | engine/low |
| --- | ---: | ---: | ---: |
| basic | 311.99 | 357.97 | 1.15x |
| lookback | 447.21 | 508.24 | 1.14x |

The rule-engine layer adds ~15% over raw evaluation for source hydration,
history, and watches.

### Resample (median ns/eval, one source)

| Path | scoped timeframe | `resample()` |
| --- | ---: | ---: |
| AST | 122.21 | 146.73 |
| IR | 32.07 | 29.72 |
| generated C / CUDA ABI | 0.85 | 0.85 |

### CUDA bulk (RTX 3070 Laptop)

Resident Klein-Gordon evaluator, 1,048,576 cells × 1000 steps, block 256:
median kernel time 318.6 ms → **3.29 Gcell-steps/s, 236.9 effective GB/s**,
with multi-step CPU parity confirmed. Persistent `buffer<T>` state rides in the
per-element state block and is parity-tested on the same GPU
(`cuda_bulk_buffer_runtime`).
