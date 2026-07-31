/**
 * @file debug_map.h
 * @brief Versioned, host-neutral metadata for generated C models.
 */

#ifndef CXPR_DEBUG_MAP_H
#define CXPR_DEBUG_MAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CXPR_DEBUG_MAP_ABI_VERSION 1u
#define CXPR_DEBUG_TRACE_SLOT_NONE UINT32_MAX

typedef uint64_t cxpr_debug_node_id;
typedef uint64_t cxpr_debug_output_id;

typedef enum cxpr_debug_node_kind {
    CXPR_DEBUG_NODE_INPUT = 1,
    CXPR_DEBUG_NODE_PARAM = 2,
    CXPR_DEBUG_NODE_EXPRESSION = 3,
    CXPR_DEBUG_NODE_STATE = 4,
    CXPR_DEBUG_NODE_STATE_UPDATE = 5
} cxpr_debug_node_kind;

typedef enum cxpr_debug_result_type {
    CXPR_DEBUG_RESULT_UNKNOWN = 0,
    CXPR_DEBUG_RESULT_NUMBER = 1,
    CXPR_DEBUG_RESULT_BOOL = 2
} cxpr_debug_result_type;

/** Half-open source span. Lines are one-based; offsets and columns are zero-based. */
typedef struct cxpr_debug_source_span {
    size_t start_offset;
    size_t start_line;
    size_t start_column;
    size_t end_offset;
    size_t end_line;
    size_t end_column;
} cxpr_debug_source_span;

typedef struct cxpr_debug_node {
    cxpr_debug_node_id id;
    const char* name;
    cxpr_debug_node_kind kind;
    cxpr_debug_result_type result_type;
    const char* source_path;
    cxpr_debug_source_span source_span;
    int has_source_span;
    const char* canonical_source;
    const cxpr_debug_node_id* dependencies;
    size_t dependency_count;
    uint32_t trace_slot;
} cxpr_debug_node;

typedef struct cxpr_debug_output {
    cxpr_debug_output_id id;
    const char* name;
    cxpr_debug_node_id node_id;
    cxpr_debug_result_type result_type;
} cxpr_debug_output;

typedef struct cxpr_debug_map {
    uint32_t abi_version;
    const char* model_name;
    const cxpr_debug_node* nodes;
    size_t node_count;
    const cxpr_debug_output* outputs;
    size_t output_count;
} cxpr_debug_map;

/**
 * Validate ABI version, stable IDs, dependency edges, and output mappings.
 * Returns non-zero for a valid map.
 */
int cxpr_debug_map_validate(const cxpr_debug_map* map);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_DEBUG_MAP_H */
