/**
 * @file snapshot.h
 * @brief Single-evaluation AST diagnostics for cxpr.
 */

#ifndef CXPR_EVAL_SNAPSHOT_H
#define CXPR_EVAL_SNAPSHOT_H

#include <cxpr/types.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CXPR_SNAPSHOT_STATE_UNKNOWN = 0,
    CXPR_SNAPSHOT_STATE_TRUE,
    CXPR_SNAPSHOT_STATE_FALSE,
    CXPR_SNAPSHOT_STATE_NUMBER,
    CXPR_SNAPSHOT_STATE_VALUE,
    CXPR_SNAPSHOT_STATE_SKIPPED,
    CXPR_SNAPSHOT_STATE_ERROR
} cxpr_snapshot_state;

/** @brief One evaluated AST node captured in a diagnostic snapshot. */
typedef struct {
    size_t id;
    size_t parent_id;
    int has_parent;
    char* role;
    char* kind;
    char* label;
    char* display_label;
    char* source;
    char* resolved;
    char* value_text;
    cxpr_value value;
    int has_value;
    int active;
    cxpr_snapshot_state state;
} cxpr_snapshot_node;

/** @brief Complete diagnostic snapshot for one evaluated expression. */
typedef struct {
    char* expression;
    char* resolved;
    cxpr_value result;
    int has_result;
    cxpr_snapshot_state state;
    cxpr_snapshot_node* nodes;
    size_t node_count;
    size_t node_capacity;
} cxpr_eval_snapshot;

/** @brief One expression-level node in an evaluator flow snapshot. */
typedef struct {
    char* name;
    char* kind;
    char* display_label;
    char* value_text;
    cxpr_value value;
    int has_value;
    cxpr_snapshot_state state;
    cxpr_eval_snapshot ast;
} cxpr_eval_snapshot_flow_node;

/** @brief Directed dependency edge between two flow snapshot nodes. */
typedef struct {
    size_t source_index;
    size_t target_index;
    char* source_name;
    char* target_name;
} cxpr_eval_snapshot_flow_edge;

/** @brief Complete evaluator flow graph with owned nodes and edges. */
typedef struct {
    cxpr_eval_snapshot_flow_node* nodes;
    size_t node_count;
    size_t node_capacity;
    cxpr_eval_snapshot_flow_edge* edges;
    size_t edge_count;
    size_t edge_capacity;
} cxpr_eval_snapshot_flow;

typedef bool (*cxpr_snapshot_flow_node_host_json_fn)(
    FILE* out,
    const cxpr_eval_snapshot_flow* flow,
    size_t node_index,
    void* userdata);

typedef bool (*cxpr_snapshot_ast_node_host_json_fn)(
    FILE* out,
    const cxpr_eval_snapshot* snapshot,
    size_t node_index,
    void* userdata);

/** @brief Optional host-specific JSON extensions for snapshot serialization. */
typedef struct {
    const char* host_name;
    const char* host_schema;
    cxpr_snapshot_flow_node_host_json_fn write_flow_node_host_json;
    cxpr_snapshot_ast_node_host_json_fn write_ast_node_host_json;
    void* userdata;
} cxpr_snapshot_json_hooks;

/**
 * @brief Build a single-context diagnostic snapshot for an AST.
 *
 * The snapshot records the AST tree, which nodes were active for this
 * evaluation, each active node's value when available, and skipped branches for
 * short-circuit boolean operators and ternaries.
 */
bool cxpr_eval_snapshot_build(const cxpr_expr_ast* ast,
                              const cxpr_context* ctx,
                              const cxpr_registry* reg,
                              cxpr_eval_snapshot* out_snapshot,
                              cxpr_error* err);

/**
 * @brief Build a snapshot for every named expression in evaluator order.
 *
 * The returned flow contains one node per named expression plus dependency
 * edges from dependency to dependent expression. Each flow node also owns an
 * AST snapshot for drilldown.
 */
bool cxpr_eval_snapshot_build_flow(const cxpr_evaluator* evaluator,
                                   cxpr_context* ctx,
                                   const cxpr_registry* reg,
                                   cxpr_eval_snapshot_flow* out_flow,
                                   cxpr_error* err);

/** @brief Release all storage owned by a snapshot. */
void cxpr_eval_snapshot_free(cxpr_eval_snapshot* snapshot);

/** @brief Release all storage owned by a flow snapshot. */
void cxpr_eval_snapshot_flow_free(cxpr_eval_snapshot_flow* flow);

/** @brief Return a readable name for a snapshot state. */
const char* cxpr_snapshot_state_name(cxpr_snapshot_state state);

/**
 * @brief Write a generic JSON representation suitable for Cytoscape mapping.
 */
bool cxpr_eval_snapshot_write_json(const cxpr_eval_snapshot* snapshot, FILE* out);

/**
 * @brief Write a snapshot JSON representation with optional host metadata.
 *
 * Host callbacks must write a complete JSON object value, for example
 * `{ "role": "entry" }`. cxpr treats the object as opaque host-owned data.
 */
bool cxpr_eval_snapshot_write_json_ex(const cxpr_eval_snapshot* snapshot,
                                      const cxpr_snapshot_json_hooks* hooks,
                                      FILE* out);

/** @brief Write flow-level JSON with expression graph and AST drilldowns. */
bool cxpr_eval_snapshot_flow_write_json(const cxpr_eval_snapshot_flow* flow, FILE* out);

/**
 * @brief Write flow-level JSON with optional host metadata.
 *
 * Host callbacks must write complete JSON object values. The host data is
 * emitted under `host` on matching flow/AST nodes and is otherwise ignored by
 * cxpr.
 */
bool cxpr_eval_snapshot_flow_write_json_ex(const cxpr_eval_snapshot_flow* flow,
                                           const cxpr_snapshot_json_hooks* hooks,
                                           FILE* out);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_EVAL_SNAPSHOT_H */
