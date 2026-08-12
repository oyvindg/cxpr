#ifndef CXPR_MODEL_RUNTIME_H
#define CXPR_MODEL_RUNTIME_H

#if defined(CXPR_CUDA_SOURCE_COMPOSED)
typedef unsigned long size_t;
#ifndef NAN
#define NAN (0.0 / 0.0)
#endif
#define cxpr_model_runtime_isnan(x) ((x) != (x))
#else
#include <math.h>
#include <stddef.h>
#define cxpr_model_runtime_isnan(x) isnan(x)
#endif

#ifndef CXPR_UNLIKELY
#if defined(__GNUC__) || defined(__clang__)
#define CXPR_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define CXPR_UNLIKELY(x) (x)
#endif
#endif

#ifndef CXPR_MODEL_RUNTIME_LINKAGE
#if defined(CXPR_CUDA_SOURCE_COMPOSED)
#define CXPR_MODEL_RUNTIME_LINKAGE static __device__ inline
#else
#define CXPR_MODEL_RUNTIME_LINKAGE static inline
#endif
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
        if (cxpr_model_runtime_isnan(x)) continue;
        if (valid_count == 0u) extreme = x;
        if (op == 2 && x > extreme) extreme = x;
        if (op == 3 && x < extreme) extreme = x;
        sum += x;
        sumsq += x * x;
        valid_count++;
    }
    if (valid_count == 0u) return 0.0;
    if (op == 5) {
        double weighted_sum = 0.0;
        double weight_sum = 0.0;
        size_t weight = 1u;
        for (size_t i = limit; i > 0u; --i, ++weight) {
            double x = values[i - 1u];
            if (cxpr_model_runtime_isnan(x)) continue;
            weighted_sum += x * (double)weight;
            weight_sum += (double)weight;
        }
        return weight_sum > 0.0 ? weighted_sum / weight_sum : 0.0;
    }
    if (op == 2 || op == 3) return extreme;
    if (op == 1) return sum / (double)valid_count;
    if (op == 4) {
        double mean = sum / (double)valid_count;
        double variance = (sumsq / (double)valid_count) - mean * mean;
        return sqrt(variance > 0.0 ? variance : 0.0);
    }
    return sum;
}

CXPR_MODEL_RUNTIME_LINKAGE double cxpr_model_window_wma_c(const double* values,
                                                          size_t count,
                                                          int period) {
    size_t limit = period < 1 ? 1u : (size_t)period;
    double weighted_sum = 0.0;
    double weight_sum = 0.0;
    size_t weight = 1u;
    if (!values || count == 0u) return 0.0;
    if (limit > count) limit = count;
    for (size_t i = limit; i > 0u; --i, ++weight) {
        double value = values[i - 1u];
        if (cxpr_model_runtime_isnan(value)) continue;
        weighted_sum += value * (double)weight;
        weight_sum += (double)weight;
    }
    return weight_sum > 0.0 ? weighted_sum / weight_sum : 0.0;
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
    if (cxpr_model_runtime_isnan(now)) return NAN;
    if (cxpr_model_runtime_isnan(prev) || fabs(prev) <= 1e-12) return 0.0;
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
        if (cxpr_model_runtime_isnan(now)) continue;
        roc = (cxpr_model_runtime_isnan(prev) || fabs(prev) <= 1e-12)
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
        if (cxpr_model_runtime_isnan(hi) || cxpr_model_runtime_isnan(lo)) continue;
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

CXPR_MODEL_RUNTIME_LINKAGE double cxpr_model_bars_since_extreme_c(const double* values,
                                                                  size_t count,
                                                                  int samples,
                                                                  double mode) {
    size_t limit = samples < 1 ? 1u : (size_t)samples;
    double extreme = 0.0;
    size_t extreme_index = 0u;
    size_t valid_count = 0u;
    if (!values || count == 0u) return 0.0;
    if (limit > count) limit = count;
    for (size_t i = 0u; i < limit; ++i) {
        double value = values[i];
        if (cxpr_model_runtime_isnan(value)) continue;
        if (valid_count == 0u ||
            (mode >= 0.0 && value > extreme) ||
            (mode < 0.0 && value < extreme)) {
            extreme = value;
            extreme_index = i;
        }
        valid_count++;
    }
    return valid_count == 0u ? 0.0 : (double)extreme_index;
}

CXPR_MODEL_RUNTIME_LINKAGE double cxpr_model_window_mean_absdev_c(const double* values,
                                                                  size_t count,
                                                                  int samples,
                                                                  double center) {
    size_t limit = samples < 1 ? 1u : (size_t)samples;
    double sum = 0.0;
    size_t valid_count = 0u;
    if (!values || count == 0u || cxpr_model_runtime_isnan(center)) return 0.0;
    if (limit > count) limit = count;
    for (size_t i = 0u; i < limit; ++i) {
        double value = values[i];
        if (cxpr_model_runtime_isnan(value)) continue;
        sum += fabs(value - center);
        valid_count++;
    }
    return valid_count == 0u ? 0.0 : sum / (double)valid_count;
}

#endif
