# Iteration 02 raw benchmark summary

All values are nanoseconds per bar/op. Each row contains nine trials. The p95 in the
gate report uses linear interpolation over the sorted samples.

## Engine baseline (`4a0852b`)

| Case | Trials |
| --- | --- |
| basic raw programs | 438.64, 427.74, 425.93, 419.14, 415.87, 427.09, 418.16, 417.24, 416.29 |
| basic evaluator | 610.54, 607.90, 607.40, 604.42, 586.49, 595.11, 595.90, 595.04, 592.34 |
| basic engine watch | 367.68, 361.00, 368.90, 366.24, 355.65, 365.04, 363.62, 362.98, 363.24 |
| lookback raw programs | 299.51, 299.63, 292.77, 302.45, 302.45, 294.51, 296.29, 295.48, 292.87 |
| lookback evaluator | 457.55, 442.38, 446.41, 467.92, 443.04, 443.45, 444.38, 442.87, 446.47 |
| lookback engine watch | 502.66, 501.15, 507.71, 533.77, 533.77, 501.44, 498.30, 498.80, 499.55 |

The duplicated high lookback sample is conservative: one interleaved baseline record was
not retained after terminal output rotation, so the adjacent observed high sample is used
instead of reconstructing a lower value.

## Engine candidate

| Case | Trials |
| --- | --- |
| basic raw programs | 435.83, 437.24, 433.34, 432.66, 437.16, 434.57, 433.57, 432.49, 433.96 |
| basic evaluator | 453.82, 450.98, 450.33, 454.05, 454.78, 449.12, 449.39, 451.00, 450.02 |
| basic engine watch | 220.52, 221.64, 220.55, 219.61, 223.00, 219.53, 223.83, 219.58, 219.69 |
| lookback raw programs | 293.43, 291.44, 289.64, 290.79, 290.78, 290.50, 288.65, 289.34, 289.34 |
| lookback evaluator | 358.84, 354.87, 357.11, 356.09, 358.96, 357.59, 358.91, 355.22, 355.22 |
| lookback engine watch | 428.80, 427.04, 432.55, 427.27, 435.78, 427.94, 424.76, 426.70, 426.70 |

## Model baseline (`4a0852b`)

| Case | Trials |
| --- | --- |
| strategy fixture | 5096.31, 4996.08, 5042.51, 5001.81, 5021.27, 4979.69, 5118.30, 4957.45, 5095.70 |
| RSI fused IR | 717.89, 708.13, 718.28, 749.96, 714.02, 712.39, 716.42, 709.86, 699.97 |
| MACD record | 1986.46, 1984.13, 1981.15, 2053.12, 1981.54, 2028.50, 1995.11, 2014.64, 1976.16 |

## Model candidate

| Case | Trials |
| --- | --- |
| strategy fixture | 4895.54, 5063.27, 4917.11, 4954.04, 4909.46, 4956.50, 4982.28, 5059.00, 4965.96 |
| RSI fused IR | 711.35, 724.47, 706.70, 722.30, 720.98, 742.71, 708.70, 726.19, 720.76 |
| MACD record | 2037.99, 2038.08, 2007.99, 2074.47, 2025.47, 2039.82, 2012.51, 2021.49, 2031.80 |

