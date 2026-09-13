# Host-neutral scale fixture

`large_host_neutral.cxpr` is a standalone CXPR graph extracted from the graph
shapes exercised by a production host, without host headers, callbacks, or domain
vocabulary. It intentionally provides:

- more than 50 named expressions;
- multiple imported helper modules and nested imported-function calls;
- state updates, input and named-result history, and multiple window sites;
- numeric, boolean, and structured output;
- named parameters with deterministic defaults.

The fixture is the shared input for standalone engine/generated-C parity and
benchmark coverage. Keep domain integration assertions in the embedding host instead of
adding host concepts here.

## Reproduce parity and metrics

Run from the CXPR repository root. This build is standalone and deliberately
does not reference a parent repository:

```sh
cmake -S . -B build-scale \
  -DCMAKE_BUILD_TYPE=Release \
  -DCXPR_BUILD_BENCHMARKS=OFF
cmake --build build-scale \
  --target test_scale_fixture test_scale_parity_benchmark -j2
ctest --test-dir build-scale \
  -R '^(scale_fixture|scale_parity_benchmark)$' \
  --output-on-failure
./build-scale/tests/test_scale_parity_benchmark
```

The last command emits one parseable record:

```text
cxpr_scale ticks=<n> reference_ns_per_tick=<ns> generated_c_ns_per_tick=<ns> speedup=<ratio>x sink=<checksum>
```

Correctness is enforced per tick after the fixture's 12-tick window warmup.
Timing is informational because CPU frequency, compiler, thermal state, and
background load affect it. The checksum must remain `8568.927508` for the
current deterministic 8,192-tick input.

## Published baseline

Measured 2026-07-31 at CXPR commit `a570d8f`, five consecutive Release runs:

| Environment | Reference | Generated C | Median speedup |
| --- | ---: | ---: | ---: |
| AMD Ryzen 7 5800H, Linux x86-64, GCC 13.3.0, CMake 3.28.3 | 30.2–32.2 us/tick | 70.9–72.9 ns/tick | 433.6x |

All five runs produced the same checksum. These values are a reproducibility
baseline, not a CI threshold.
