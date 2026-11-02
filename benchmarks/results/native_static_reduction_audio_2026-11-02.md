# Native static reduction audio benchmark — 2026-11-02

## Scope

Compare parser-time AST expansion against preserved native array/selector
reductions using the `robot_voice` generated-C audio graph. Both variants use
the same model, local `cxpr_model_codegen`, compiler flags, and 4096-frame
render.

Commands were repeated five times for code generation and C compilation:

```sh
build/cxpr_model_codegen \
  --model examples/audio/songs/robot_voice/models/song.cxpr \
  --import-dir examples/audio \
  --output "$bench_dir/song.gen.c"

cc -std=c11 -O1 -I include -I "$bench_dir" \
  examples/audio/renderer/render_native.c -lm -o "$bench_dir/renderer"
```

## Superseded comparison

The original comparison below was collected while `robot_voice` also changed
from one full-list selector `sum` to a split `sum` plus `max`. It therefore does
not isolate the compiler representation change and must not be used as proof of
an improvement. The audio topology was restored after an audible regression
was reported.

## Original results (not directly comparable)

| Metric | Before: parser lowering | After: native representation | Change |
|---|---:|---:|---:|
| Codegen elapsed | 0.01–0.02 s | 0.01–0.02 s | Neutral at timer resolution |
| Codegen peak RSS | 5,248–5,376 KB | 5,248–5,376 KB | Neutral |
| C compile elapsed | 0.27–0.32 s | 0.25–0.26 s | Median 0.28 → 0.26 s (-7.1%) |
| C compile peak RSS | 45,556–46,160 KB | 44,568–45,008 KB | Median 45,956 → 44,848 KB (-2.4%) |
| Generated C size | 94,312 bytes | 89,942 bytes | -4,370 bytes (-4.6%) |
| Generated state | 67,056 bytes | 67,056 bytes | No change |

After restoring the original full-list selector `sum`, the native build
produced 89,793 bytes of C, used 5,376 KB peak RSS in codegen and 45,224 KB peak
RSS in one C compile, and retained 67,056 bytes of generated state. A new
controlled benchmark using identical source revisions is required before
claiming a performance delta. Correctness and preserved audio behavior remain
the acceptance criteria for the native representation.
