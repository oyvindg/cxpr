/**
 * @file history.h
 * @brief Host-neutral historical numeric source bindings.
 */

#ifndef CXPR_HISTORY_H
#define CXPR_HISTORY_H

#include <cxpr/registry.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CXPR_HISTORY_BOUNDS_ERROR = 0,
    CXPR_HISTORY_BOUNDS_CLAMP_FIRST = 1,
    CXPR_HISTORY_BOUNDS_LEGACY_NAN = 2
} cxpr_history_bounds_policy;

/** Borrowed immutable numeric source storage. A stride of zero means sizeof(double). */
typedef struct {
    const char* name;
    const void* base;
    size_t stride;
    size_t count;
} cxpr_history_numeric_source;

/** Resolve a currently-bound borrowed source view by exact registered name. */
typedef bool (*cxpr_history_numeric_view_fn)(
    void* userdata, const char* name,
    cxpr_history_numeric_source* out_source, int64_t* out_cursor);

/**
 * Register copied source descriptors backed by borrowed immutable storage.
 * The cursor is borrowed and read once per lookup. Independent registries may
 * safely share the source storage when the host keeps it alive.
 */
bool cxpr_register_history_numeric_sources(
    cxpr_registry* reg,
    const cxpr_history_numeric_source* sources,
    size_t source_count,
    const int64_t* cursor,
    cxpr_history_bounds_policy policy);

/** Convenience adapter for one contiguous double column. */
bool cxpr_register_history_contiguous_numbers(
    cxpr_registry* reg,
    const char* name,
    const double* values,
    size_t count,
    const int64_t* cursor,
    cxpr_history_bounds_policy policy);

/**
 * Register stable source names backed by a dynamically resolved borrowed view.
 * This supports hosts that rebind buffers while keeping offset validation,
 * bounds policy, compound evaluation, and nested composition in cxpr core.
 */
bool cxpr_register_history_numeric_provider(
    cxpr_registry* reg, const char* const* source_names, size_t source_count,
    cxpr_history_numeric_view_fn view, void* userdata,
    cxpr_userdata_free_fn free_userdata,
    cxpr_history_bounds_policy policy);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_HISTORY_H */
