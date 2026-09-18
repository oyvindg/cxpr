#ifndef CXPR_BACKENDS_CXCU_H
#define CXPR_BACKENDS_CXCU_H

#include <cxpr/model/optimize.h>

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

#ifdef __cplusplus
}
#endif

#endif
