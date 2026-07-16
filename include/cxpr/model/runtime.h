#ifndef CXPR_MODEL_RUNTIME_H
#define CXPR_MODEL_RUNTIME_H

#include <math.h>
#include <stddef.h>

#ifndef CXPR_MODEL_RUNTIME_LINKAGE
#define CXPR_MODEL_RUNTIME_LINKAGE static inline
#endif

CXPR_MODEL_RUNTIME_LINKAGE double cxpr_model_window_eval_c(const double* values,
                                                           size_t count,
                                                           int period,
                                                           int op) {
    double sum = 0.0;
    double sumsq = 0.0;
    double extreme = 0.0;
    size_t valid_count = 0u;
    size_t limit = period < 1 ? 1u : (size_t)period;
    if (!values || count == 0u) return 0.0;
    if (limit > count) limit = count;
    for (size_t i = 0u; i < limit; ++i) {
        double x = values[i];
        if (isnan(x)) continue;
        if (valid_count == 0u) extreme = x;
        if (op == 2 && x > extreme) extreme = x;
        if (op == 3 && x < extreme) extreme = x;
        sum += x;
        sumsq += x * x;
        valid_count++;
    }
    if (valid_count == 0u) return 0.0;
    if (op == 2 || op == 3) return extreme;
    if (op == 1) return sum / (double)valid_count;
    if (op == 4) {
        double mean = sum / (double)valid_count;
        double variance = (sumsq / (double)valid_count) - mean * mean;
        return sqrt(variance > 0.0 ? variance : 0.0);
    }
    return sum;
}

CXPR_MODEL_RUNTIME_LINKAGE double cxpr_model_window_roc_c(const double* values,
                                                          size_t count,
                                                          int period) {
    size_t index = period < 1 ? 1u : (size_t)period;
    double now;
    double prev;
    if (!values || count == 0u) return 0.0;
    if (index >= count) index = count - 1u;
    now = values[0];
    prev = values[index];
    if (isnan(now)) return NAN;
    if (isnan(prev) || fabs(prev) <= 1e-12) return 0.0;
    return ((now - prev) / prev) * 100.0;
}

CXPR_MODEL_RUNTIME_LINKAGE double cxpr_model_window_mean_roc_c(const double* values,
                                                               size_t count,
                                                               int roc_period,
                                                               int mean_period) {
    size_t rp = roc_period < 1 ? 1u : (size_t)roc_period;
    size_t mp = mean_period < 1 ? 1u : (size_t)mean_period;
    double sum = 0.0;
    size_t valid_count = 0u;
    if (!values || count == 0u) return 0.0;
    if (mp > count) mp = count;
    for (size_t i = 0u; i < mp; ++i) {
        double now = values[i];
        double prev = (i + rp < count) ? values[i + rp] : NAN;
        double roc;
        if (isnan(now)) continue;
        roc = (isnan(prev) || fabs(prev) <= 1e-12)
                  ? 0.0
                  : ((now - prev) / prev) * 100.0;
        sum += roc;
        valid_count++;
    }
    return valid_count == 0u ? 0.0 : sum / (double)valid_count;
}

CXPR_MODEL_RUNTIME_LINKAGE double cxpr_model_window_midpoint_c(const double* highs,
                                                               const double* lows,
                                                               size_t count,
                                                               int period) {
    size_t limit = period < 1 ? 1u : (size_t)period;
    double highest = 0.0;
    double lowest = 0.0;
    size_t valid_count = 0u;
    if (!highs || !lows || count == 0u) return 0.0;
    if (limit > count) limit = count;
    for (size_t i = 0u; i < limit; ++i) {
        double hi = highs[i];
        double lo = lows[i];
        if (isnan(hi) || isnan(lo)) continue;
        if (valid_count == 0u) {
            highest = hi;
            lowest = lo;
        }
        if (hi > highest) highest = hi;
        if (lo < lowest) lowest = lo;
        valid_count++;
    }
    return valid_count == 0u ? 0.0 : (highest + lowest) * 0.5;
}

#endif
