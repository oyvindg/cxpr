# cxpr resample benchmark

- Timestamp: 2026-08-02T13:29:30Z
- Build: Release
- Compiler: cc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- CPU: AMD Ryzen 7 5800H with Radeon Graphics
- GPU/driver: CUDA-capable runner (model/driver not recorded)
- Dataset: 4096 samples; 500000 timed evaluations; 1000 planning repetitions
- Gate: median CPU metrics must remain within 15% of the accepted baseline; GPU metrics require a CUDA-capable runner and are gated separately.

```text
resample backend benchmark (500000 evaluations, ns/eval)
  scoped timeframe AST     117.84
  resample AST             140.01
  scoped timeframe IR       33.60
  resample IR               28.88
  generated C/CUDA ABI       0.86
repeated pre-bound load workload (volatile memory contract, ns/eval)
  uncached 2x current+2x [1]     1.66
  CSE locals current/[1]         1.31
planning parse+compile+generate  49480.58 ns/model
generated C artifact size           2860 bytes
```

## CUDA three-view fixture

- Result: parity passed for current value, `[1]`, warmup, gap sentinel,
  finalized-bucket/no-lookahead, multiple intervals, and non-trading `path`.
- Views: independent 1h close, 5m close, and 1d path buffers/alignment maps.
- Repetitions: 10000 kernel launches and 10000 three-view transfer sets.
- Transfer: 39530.39 ns/three-view-set.
- Kernel: 3555.03 ns/evaluation.
- Device memory: 313 bytes.

GPU model and driver metadata were not captured by the confirming runner, so
these figures are retained as a functional baseline rather than a portable
hardware comparison.

## CPU distribution summary

Nine Release trials, 500000 evaluations per trial:

```text
scoped timeframe AST median_ns=116.50 p95_ns=121.36 throughput_per_s=8583690.99
resample AST median_ns=138.66 p95_ns=147.17 throughput_per_s=7211885.19
scoped timeframe IR median_ns=32.31 p95_ns=34.14 throughput_per_s=30950170.23
resample IR median_ns=29.06 p95_ns=31.20 throughput_per_s=34411562.28
generated C/CUDA ABI median_ns=0.86 p95_ns=0.92 throughput_per_s=1162790697.67
uncached 2x current+2x [1] median_ns=1.66 p95_ns=1.70 throughput_per_s=602409638.55
CSE locals current/[1] median_ns=1.34 p95_ns=1.35 throughput_per_s=746268656.72
```

- Workloads exercised: one interval/current, one interval/`[1]`, nested
  indicator input, repeated deduplication, multiple intervals, warmup/gap, and
  non-trading `path`.
- Host allocations and peak host memory: not measured by this harness.
- Generated artifact size: 2860 bytes.
- CUDA transfer, kernel, and device memory are reported separately above.
