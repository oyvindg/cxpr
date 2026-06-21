/**
 * @file thread.h
 * @brief Public thread lifecycle API for cxpr.
 */

#ifndef CXPR_THREAD_H
#define CXPR_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Release the calling thread's internal per-thread caches.
 *
 * cxpr maintains a thread-local empty-overlay reuse cache that is populated
 * implicitly during evaluation. A long-lived process that spawns and joins
 * many worker threads should call this from each worker just before it exits
 * to avoid retaining that thread's cache until process exit. It is optional,
 * idempotent, and never required for correctness -- only for promptly
 * reclaiming memory on threads that will not run further cxpr evaluations.
 */
void cxpr_thread_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_THREAD_H */
