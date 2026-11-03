# Native borrowed-view audio benchmark — 2026-11-03

## Scope

Compare repeated scalar host calls with one rank-one borrowed-view call. Both
paths evaluate the same stateful one-pole filter and preserve one atomic CXPR
state transition per element. The view path changes transport, not DSP
semantics.

## C reference runtime

Command:

```sh
cmake --build build --target cxpr_bench_view_batch
./build/benchmarks/cxpr_bench_view_batch
```

Result on the development host, 100 trials of 4,096 frames:

| Path | ns/frame |
|---|---:|
| Repeated scalar C calls | 60.82 |
| One C view call per block | 62.37 |

The +2.5% difference is validation and block-loop overhead in the reference
runtime. There is no claimed C evaluator speedup; the contract primarily
removes foreign-function transitions and prepares a stable borrowed-memory ABI.

## cxpr-node boundary

Command:

```sh
cd ../cxpr-node
npm run bench:views
```

Result on the same host, 25 trials of 4,096 frames:

| Path | ns/frame | Relative |
|---|---:|---:|
| One JS/native call per scalar tick | 767.19 | 1.00x |
| One `tickViews` call per block | 69.79 | 10.99x |

The benchmark consumes the returned output so neither path can be optimized
away. View storage is supplied as `Float64Array`; CXPR borrows it only for the
duration of the call.
