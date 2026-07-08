/**
 * @file plugin.h
 * @brief Host-agnostic plugin contract for cxpr model artifacts.
 *
 * This API lets hosts attach artifact generators to compiled `.cxpr` models
 * without making the core model compiler depend on any host runtime, storage
 * layout, accelerator, or domain concept. A plugin receives a parsed/compiled
 * model event and writes one or more artifacts through callbacks supplied by
 * the host.
 *
 * The host owns artifact routing. It may write artifacts to files, memory,
 * build-system generated sources, package manifests, or any other sink. The
 * plugin owns only artifact content and must describe the artifact through
 * generic metadata such as `kind`, `name`, and `path_hint`.
 */

#ifndef CXPR_PLUGIN_H
#define CXPR_PLUGIN_H

#include <cxpr/types.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Immutable input passed to a plugin for one compiled model.
 *
 * The pointers are borrowed from the caller and remain valid only for the
 * duration of the plugin call. Plugins must not store them unless the host
 * explicitly provides a longer-lived ownership contract outside this API.
 */
typedef struct cxpr_plugin_model_event {
    /** Optional source path for diagnostics and artifact naming. */
    const char* model_path;
    /** Borrowed parsed model. */
    const cxpr_model* model;
    /** Borrowed compiled model program. */
    const cxpr_model_program* program;
} cxpr_plugin_model_event;

/**
 * @brief Metadata for one artifact emitted by a plugin.
 *
 * These fields are descriptive only. The host decides how to interpret them
 * and where the artifact is stored.
 */
typedef struct cxpr_plugin_artifact_event {
    /** Stable artifact name, for example `macd.cuda.source`. */
    const char* name;
    /** Stable kind identifier, for example `cxpr.cuda.source.v1`. */
    const char* kind;
    /** Optional host-facing filename/path suggestion. */
    const char* path_hint;
} cxpr_plugin_artifact_event;

/**
 * @brief Start writing one artifact.
 *
 * @return Non-zero on success, zero on failure with @p err optionally filled.
 */
typedef int (*cxpr_plugin_begin_artifact_fn)(
    void* user,
    const cxpr_plugin_artifact_event* artifact,
    cxpr_error* err);

/**
 * @brief Append bytes to the currently open artifact.
 *
 * Plugins may call this multiple times between begin/end. The byte stream is
 * opaque to cxpr; text artifacts should include their own encoding/format
 * convention in the artifact `kind`.
 *
 * @return Non-zero on success, zero on failure with @p err optionally filled.
 */
typedef int (*cxpr_plugin_write_artifact_fn)(
    void* user,
    const void* data,
    size_t size,
    cxpr_error* err);

/**
 * @brief Finish the currently open artifact.
 *
 * @return Non-zero on success, zero on failure with @p err optionally filled.
 */
typedef int (*cxpr_plugin_end_artifact_fn)(
    void* user,
    cxpr_error* err);

/**
 * @brief Host callback table used by plugins to emit artifacts.
 *
 * `user` is passed unchanged to every callback and is owned by the host.
 */
typedef struct cxpr_plugin_host {
    /** Host-owned callback context. */
    void* user;
    /** Called once before writing an artifact. */
    cxpr_plugin_begin_artifact_fn begin_artifact;
    /** Called one or more times with artifact bytes. */
    cxpr_plugin_write_artifact_fn write_artifact;
    /** Called once after all bytes for an artifact have been written. */
    cxpr_plugin_end_artifact_fn end_artifact;
} cxpr_plugin_host;

/**
 * @brief Generic plugin entry point signature.
 *
 * Plugins should return non-zero on success. On failure they may populate
 * @p err with a diagnostic suitable for build-tool or host logs.
 */
typedef int (*cxpr_plugin_generate_fn)(
    const cxpr_plugin_model_event* event,
    const cxpr_plugin_host* host,
    cxpr_error* err);

/**
 * @brief Generic plugin entry point with backend-specific options.
 *
 * @p options is intentionally opaque to cxpr core. Each plugin documents the
 * concrete options struct it accepts, while generic hosts can still pass the
 * pointer through without linking to accelerator or domain concepts.
 */
typedef int (*cxpr_plugin_generate_with_options_fn)(
    const cxpr_plugin_model_event* event,
    const void* options,
    const cxpr_plugin_host* host,
    cxpr_error* err);

/**
 * @brief One host-agnostic artifact backend.
 *
 * Core and hosts can pass this descriptor around without knowing whether the
 * backend emits C, CUDA, metadata, graph JSON, or another artifact kind.
 */
typedef struct cxpr_plugin_backend {
    /** Stable backend id, e.g. `cxpr.c.source` or `cxpr.cuda.source`. */
    const char* id;
    /** Backend-specific artifact emitter. */
    cxpr_plugin_generate_with_options_fn generate;
} cxpr_plugin_backend;

/**
 * @brief Run one backend for a compiled model event.
 *
 * This is a tiny dispatch helper; it does not interpret @p options or artifact
 * content. Those remain owned by the plugin and host.
 */
int cxpr_plugin_run_model_backend(
    const cxpr_plugin_model_event* event,
    const cxpr_plugin_backend* backend,
    const void* options,
    const cxpr_plugin_host* host,
    cxpr_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_PLUGIN_H */
