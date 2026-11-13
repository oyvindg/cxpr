#ifndef CXPR_BACKENDS_CXCU_H
#define CXPR_BACKENDS_CXCU_H

#include <cxpr/model/optimize.h>
#include <cxpr/view.h>
#include <cxcu/cxcu.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Configuration retained by a CXCU backend for its entire lifetime. */
typedef struct cxpr_cxcu_backend_options {
    /** NVRTC include option for CXPR headers, for example "-I/path/include". */
    const char* cxpr_include_option;
    int device_ordinal;
} cxpr_cxcu_backend_options;

/**
 * Initialize the official CXCU optimizer backend.
 * The options object must remain valid while the backend is in use.
 */
void cxpr_cxcu_backend_init(
    cxpr_model_optimize_backend* out_backend,
    const cxpr_cxcu_backend_options* options);

/** Reusable device residency for one resolved CXPR view table. */
typedef struct cxpr_cxcu_view_plan {
    cxcu_device_view_plan residency;
    cxcu_device_view_descriptor descriptors[CXPR_VIEW_MAX_COUNT];
    size_t count;
    uint64_t generation;
} cxpr_cxcu_view_plan;

/** Initialize an empty CXPR-to-CXCU view plan. */
void cxpr_cxcu_view_plan_init(cxpr_cxcu_view_plan* plan, int device_ordinal);

/** Release all device generations retained by a CXPR view plan. */
void cxpr_cxcu_view_plan_free(cxpr_cxcu_view_plan* plan);

/** Validate and acquire device residency for a pre-resolved host view table. */
int cxpr_cxcu_view_plan_bind(
    cxpr_cxcu_view_plan* plan,
    const cxpr_view_table* table,
    cxpr_error* err);

#ifdef __cplusplus
}
#endif

#endif
