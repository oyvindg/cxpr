# Iteration 1 raw results

Identical Release builds used the slot-based lookback input update path. Values are IR
nanoseconds per evaluation from nine trials.

| Trial | Leaf before | Leaf after | Mixed before | Mixed after |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 142.40 | 14.45 | 244.70 | 17.71 |
| 2 | 141.19 | 13.96 | 246.19 | 18.54 |
| 3 | 148.43 | 14.52 | 246.37 | 17.61 |
| 4 | 142.57 | 15.27 | 249.31 | 18.58 |
| 5 | 153.25 | 14.15 | 251.42 | 18.60 |
| 6 | 141.69 | 14.30 | 252.25 | 17.39 |
| 7 | 149.54 | 14.73 | 246.42 | 17.64 |
| 8 | 141.66 | 13.92 | 267.50 | 17.14 |
| 9 | 144.74 | 25.85 | 247.46 | 18.01 |

The leaf after-series contains one scheduler outlier. It is retained in p95 rather than
discarded. `perf` counters are unavailable because `perf` is not installed in this environment.

