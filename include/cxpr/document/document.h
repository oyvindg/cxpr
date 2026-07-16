/**
 * @file document/document.h
 * @brief Public API for loading and parsing .cxpr documents.
 *
 * A cxpr document is the top-level representation of a `.cxpr` file. It is the
 * preferred host-facing API for loading `.cxpr` from disk or memory.
 *
 * The document layer is intentionally split from model execution semantics:
 * - Manifest documents contain host-defined blocks such as `project`,
 *   `tooling`, `profile` or `vsix`.
 * - Model documents opt into the model extension and may additionally contain
 *   execution-oriented syntax such as `model`, `in`, `state` and `out`.
 *
 * Hosts should normally call the named helpers
 * @ref cxpr_load_manifest_file or @ref cxpr_load_model_document_file instead
 * of passing extension flags directly.
 */

#ifndef CXPR_DOCUMENT_DOCUMENT_H
#define CXPR_DOCUMENT_DOCUMENT_H

#include <cxpr/model/model.h>
#include <cxpr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque parsed `.cxpr` document. */
typedef struct cxpr_document cxpr_document;

/**
 * @brief Optional syntax domains enabled for a generic document parse.
 *
 * Use this enum only for low-level dispatch. Most host code should prefer the
 * named wrappers:
 * - @ref cxpr_parse_manifest / @ref cxpr_load_manifest_file
 * - @ref cxpr_parse_model_document / @ref cxpr_load_model_document_file
 */
typedef enum {
    /** @brief Parse a manifest-only document with top-level host blocks. */
    CXPR_DOCUMENT_EXTENSION_NONE = 0u,

    /**
     * @brief Allow model syntax in addition to host blocks.
     *
     * Enables `model`, `use`, `in`, `$param`, ordinary bindings, `state`,
     * `out` and `fn` statements. Without this flag, model syntax is rejected so
     * root manifests cannot silently become executable model documents.
     */
    CXPR_DOCUMENT_EXTENSION_MODEL = 1u << 0u,
} cxpr_document_extension;

/**
 * @brief Parse a generic `.cxpr` document from memory.
 *
 * The base document layer accepts top-level host blocks such as `project`,
 * `tooling`, `profile` and `vsix` without requiring a `model` statement.
 *
 * Pass CXPR_DOCUMENT_EXTENSION_MODEL to also allow model statements
 * (`model`, `use`, `in`, `$param`, bindings, `state`, `out`, `fn`). This keeps
 * root manifests small while letting model documents opt into the
 * execution-oriented syntax.
 *
 * @param source NUL-terminated document source. The parser does not retain
 *        this pointer after returning.
 * @param extensions Bitmask of @ref cxpr_document_extension values.
 * @param err Optional error output. Reset to CXPR_OK on entry.
 * @return New document on success; NULL on syntax, validation-screening, or
 *         allocation failure. Free with @ref cxpr_document_free.
 */
cxpr_document* cxpr_parse_document(const char* source,
                                   unsigned extensions,
                                   cxpr_error* err);

/**
 * @brief Load a `.cxpr` document from disk and parse it.
 *
 * This is the low-level file entrypoint. Prefer
 * @ref cxpr_load_manifest_file or @ref cxpr_load_model_document_file when the
 * domain is known.
 *
 * @param path File path to read.
 * @param extensions Bitmask of @ref cxpr_document_extension values.
 * @param err Optional error output. Reset to CXPR_OK on entry.
 * @return New document on success; NULL on file, parse, or allocation failure.
 *         Free with @ref cxpr_document_free.
 */
cxpr_document* cxpr_load_document_file(const char* path,
                                       unsigned extensions,
                                       cxpr_error* err);

/**
 * @brief Load a manifest `.cxpr` file.
 *
 * Manifest files may contain host-defined top-level blocks and explicitly do
 * not allow model statements. Use this for root project manifests, profile
 * manifests, editor/tooling config, VSIX recommendations and similar
 * non-executable documents.
 */
cxpr_document* cxpr_load_manifest_file(const char* path, cxpr_error* err);

/**
 * @brief Load a model `.cxpr` file.
 *
 * Model documents allow both host blocks and model syntax. Use this for
 * strategy, indicator, preset or other executable/model-like `.cxpr` files.
 */
cxpr_document* cxpr_load_model_document_file(const char* path, cxpr_error* err);

/**
 * @brief Parse a manifest `.cxpr` document from memory.
 *
 * This is the memory-buffer counterpart to @ref cxpr_load_manifest_file.
 */
cxpr_document* cxpr_parse_manifest(const char* source, cxpr_error* err);

/**
 * @brief Parse a model `.cxpr` document from memory.
 *
 * This is the memory-buffer counterpart to @ref cxpr_load_model_document_file.
 */
cxpr_document* cxpr_parse_model_document(const char* source, cxpr_error* err);

/**
 * @brief Parse a model document and transfer ownership of its semantic model.
 *
 * This is the owning model entrypoint for callers that do not need to keep the
 * document syntax tree or host block document wrapper alive.
 */
cxpr_model* cxpr_parse_model_source(const char* source, cxpr_error* err);

/** @brief Free a document returned by any cxpr document parse/load API. */
void cxpr_document_free(cxpr_document* document);

/**
 * @brief Transfer the semantic model out of a parsed model document.
 *
 * Returns NULL for NULL, manifest-only, or already-drained documents. The
 * caller owns the returned model and must free it with @ref cxpr_model_free.
 */
cxpr_model* cxpr_document_take_model(cxpr_document* document);

/** @brief Return the number of top-level host blocks in @p document. */
size_t cxpr_document_host_block_count(const cxpr_document* document);

/**
 * @brief Return a top-level host block by index.
 *
 * The returned pointer is borrowed from @p document and remains valid until
 * @ref cxpr_document_free is called.
 */
const cxpr_model_host_block* cxpr_document_host_block_at(const cxpr_document* document,
                                                         size_t index);

/**
 * @brief Return the first top-level host block whose kind matches @p kind.
 *
 * Use @ref cxpr_document_host_block_count and
 * @ref cxpr_document_host_block_at when multiple blocks of the same kind are
 * allowed by the host schema.
 */
const cxpr_model_host_block* cxpr_document_host_block(const cxpr_document* document,
                                                      const char* kind);

/**
 * @brief Validate document host blocks against a host-provided schema registry.
 *
 * This checks only the host block layer. Model symbol validation and model
 * compilation remain separate concerns for model-document callers.
 */
bool cxpr_document_validate_host_blocks(const cxpr_document* document,
                                        const cxpr_host_block_registry* registry,
                                        cxpr_error* err);

/**
 * @brief Return the parsed model view when CXPR_DOCUMENT_EXTENSION_MODEL was used.
 *
 * The returned pointer is owned by @p document. It is NULL for manifest-only
 * documents so callers cannot accidentally treat a plain manifest as an
 * executable model.
 */
const cxpr_model* cxpr_document_model(const cxpr_document* document);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_DOCUMENT_DOCUMENT_H */
