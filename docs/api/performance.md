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
