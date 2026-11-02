# Native static predicate audio benchmark — 2026-11-02

This check compares the `modular_synth` sequencer before and after replacing
three fixed OR chains with selector-based `any` calls. Both renders use the
same native CXPR build and render the complete 64-second, stereo, 44.1 kHz
song.

| Variant | Wall time | Peak RSS | WAV SHA-256 |
| --- | ---: | ---: | --- |
| Explicit OR chains | 12.87 s | 69,600 KiB | `fce90c0b21cba66b033deb12171a2d17e59c152fac71ee9fbe9fa1670b1a9739` |
| Native selector `any` | 13.05 s | 69,540 KiB | `fce90c0b21cba66b033deb12171a2d17e59c152fac71ee9fbe9fa1670b1a9739` |

The WAV files are byte-identical. The single-run timing difference is within
normal process noise and is not a performance claim. Peak memory is unchanged
for practical purposes. The relevant result is that the generic native form
adds no runtime collection or callback allocation and preserves the audio
exactly; IR and generated C contain statically unrolled, short-circuit boolean
branches.

Ongoing regression measurement is provided by the `static_predicates` case in
`cxpr_bench_ir`. It measures parse-independent IR compile time and repeated AST
and IR evaluation for both `any` and `all`, reports instruction/fallback counts,
and fails if native lowering regresses to `CALL_AST`:

```sh
build/benchmarks/cxpr_bench_ir --case static_predicates
```

Nine independent runs of that case, with 200,000 evaluations per backend in
each run, produced:

| Metric | Median | Slowest / largest run |
| --- | ---: | ---: |
| Tree evaluation | 4,881.55 ns/eval | 5,051.78 ns/eval |
| Native IR evaluation | 527.95 ns/eval | 566.94 ns/eval |
| Tree/IR ratio | approximately 9.25x | — |
| IR compile time | 23.396 us | 58.317 us |
| Process peak RSS | 2,688 KiB | 2,688 KiB |

Every run produced 144 IR instructions and zero AST fallbacks. Peak RSS is the
whole short-lived benchmark process, not an allocation attributed solely to
the predicate expression.

## Isolated `any`/`all` optimization baseline

Commit `4f61e69` is the fixed baseline for four isolated selector cases. Each
number below is the median of nine independent runs with 200,000 evaluations.
The optimized compiler fuses a selector's top-level `or` for `any` and `and`
for `all` into the reduction's existing short-circuit target instead of
materializing and testing an intermediate boolean.

| Case | Baseline IR | Optimized IR | Delta | Instructions |
| --- | ---: | ---: | ---: | ---: |
| `any`, early match | 61.03 ns/eval | 50.60 ns/eval | -17.1% | 120 -> 72 |
| `any`, full scan | 464.07 ns/eval | 372.94 ns/eval | -19.6% | 120 -> 72 |
| `all`, early reject | 79.73 ns/eval | 70.44 ns/eval | -11.7% | 104 -> 72 |
| `all`, full scan | 492.44 ns/eval | 374.04 ns/eval | -24.0% | 104 -> 72 |

Median compile time also improved in every case: 18.997 -> 16.133 us,
18.997 -> 15.853 us, 17.880 -> 16.064 us, and 18.229 -> 15.715 us,
respectively. All cases retained zero AST fallbacks. The complete dual-voice
robot render remained byte-identical after the compiler change.
