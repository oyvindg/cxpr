#ifndef CXPR_MODEL_INTERNAL_H
#define CXPR_MODEL_INTERNAL_H

#include "core.h"
#include "ir/compile/internal.h"
#include <cxpr/model/model.h>
#include <cxpr/source.h>

#if defined(__GNUC__) || defined(__clang__)
#define CXPR_MODEL_MAYBE_UNUSED __attribute__((unused))
#else
#define CXPR_MODEL_MAYBE_UNUSED
#endif

/** @brief Parsed model constant or parameter declaration. */
typedef struct {
    char* name;
    char* source;
    cxpr_expr_ast* expr;
    cxpr_source_span span;
    bool has_span;
    bool is_call_param;
} cxpr_model_constant;

/** @brief Parsed model binding with source and span metadata. */
typedef struct {
    cxpr_model_binding_kind kind;
    char* name;
    char* source;
    cxpr_expr_ast* expr;
    cxpr_source_span span;
    bool has_span;
} cxpr_model_binding;

/** @brief Anonymous model output expression awaiting a generated name. */
typedef struct {
    char* source;
    cxpr_expr_ast* expr;
} cxpr_model_anonymous_output;

/** @brief One named field in a record-returning model function. */
typedef struct {
    char* name;
    char* source;
    cxpr_expr_ast* expr;
} cxpr_model_record_field;

/** @brief Parsed model function returning a record of named fields. */
typedef struct {
    char* name;
    char** params;
    size_t param_count;
    cxpr_model_record_field* fields;
    size_t field_count;
} cxpr_model_record_function;

/** @brief Parsed metadata block associated with a model construct. */
typedef struct {
    char* name;
    char* body;
    cxpr_model_metadata_target_kind target_kind;
    char* target_name;
    cxpr_source_span span;
    bool has_span;
} cxpr_model_metadata;

/** @brief Compiled model binding and its inferred execution metadata. */
typedef struct {
    char* key;
    char* value;
} cxpr_model_host_block_field;

struct cxpr_model_host_block {
    char* kind;
    char* name;
    char* body;
    cxpr_source_span span;
    bool has_span;
    cxpr_model_host_block_field* fields;
    size_t field_count;
    cxpr_model_host_block* children;
    size_t child_count;
};

struct cxpr_model {
    char* name;
    cxpr_source_span name_span;
    bool has_name_span;
    char** uses;
    char** use_aliases;
    cxpr_source_span* use_spans;
    bool* use_has_spans;
    size_t use_count;
    char** functions;
    size_t function_count;
    cxpr_model_record_function* record_functions;
    size_t record_function_count;
    char** inputs;
    cxpr_source_span* input_spans;
    bool* input_has_spans;
    size_t input_count;
    cxpr_model_constant* constants;
    size_t constant_count;
    cxpr_model_binding* bindings;
    size_t binding_count;
    char** outputs;
    cxpr_source_span* output_spans;
    bool* output_has_spans;
    size_t output_count;
    cxpr_model_anonymous_output* anonymous_outputs;
    size_t anonymous_output_count;
    cxpr_model_metadata* metadatas;
    size_t metadata_count;
    cxpr_model_host_block* host_blocks;
    size_t host_block_count;
};

/** @brief Local name-to-expression binding used during model inlining. */
typedef struct {
    cxpr_model_binding_kind kind;
    char* name;
    char* source;
    unsigned long name_hash;
    cxpr_model_result_kind result_kind;
    cxpr_expr_ast* ast;
    double min_value;
    double max_value;
    bool has_min_value;
    bool has_max_value;
    bool is_call_param;
} cxpr_model_compiled_binding;

/** @brief Current and previous values tracked for one model output. */
typedef struct {
    char* name;
    cxpr_expr_ast* expr;
} cxpr_model_local_binding;

/** @brief Required history depth for one model expression target. */
typedef struct {
    char* name;
    double number_current;
    double number_previous;
    bool previous;
    bool current;
    bool has_number_current;
    bool has_number_previous;
    bool has_previous;
    bool has_current;
} cxpr_model_output_state;

/** @brief Reference from a compiled model value to a fused slot. */
typedef struct {
    char* name;
    cxpr_expr_ast* target;
    size_t depth;
} cxpr_model_history_spec;

/** @brief Atomic mapping from a pending update slot to a state slot. */
typedef struct {
    char* name;
    unsigned long hash;
    size_t slot;
    cxpr_model_result_kind result_kind;
} cxpr_model_slot_ref;

/** @brief Runtime history ring owned by a model session. */
typedef struct {
    size_t state_slot;
    size_t update_slot;
} cxpr_model_state_commit;

/** @brief Cached child-model instance for one producer call signature. */
typedef struct {
    char* name;
    cxpr_value* values;
    size_t capacity;
    size_t count;
    size_t next;
} cxpr_model_history_entry;

typedef struct {
    char* name;
    const cxpr_model_compiled* program;
    size_t registry_index;
    char* source_arg;
    size_t source_input_index;
} cxpr_model_child_program;

typedef enum {
    CXPR_MODEL_LIFETIME_SINGLETON = 0,
    CXPR_MODEL_LIFETIME_SCOPED = 1,
    CXPR_MODEL_LIFETIME_TRANSIENT = 2,
} cxpr_model_lifetime;

typedef struct {
    char* key;
    size_t child_index;
    cxpr_model_session* session;
} cxpr_model_child_instance;

struct cxpr_model_compiled {
    cxpr_registry* registry;
    cxpr_model_backend_kind requested_backend;
    cxpr_model_backend_kind selected_backend;
    bool compile_fuse;
    bool compile_trace;
    cxpr_ir_program fused_ir;
    bool has_fused_ir;
    bool has_fused_layout;
    const char* fused_disabled_opcode;
    char** fused_slot_names;
    unsigned long* fused_slot_hashes;
    size_t fused_slot_count;
    cxpr_model_slot_ref* fused_inputs;
    size_t fused_input_count;
    cxpr_model_slot_ref* fused_exports;
    size_t fused_export_count;
    cxpr_model_slot_ref* fused_outputs;
    size_t fused_output_count;
    cxpr_model_state_commit* fused_commits;
    size_t fused_commit_count;
    cxpr_model_compiled_binding* constants;
    size_t constant_count;
    cxpr_model_compiled_binding* state_defaults;
    size_t state_default_count;
    cxpr_model_compiled_binding* bindings;
    size_t binding_count;
    char** inputs;
    size_t input_count;
    char* source_arg;
    char* invalid_input_guard;
    cxpr_model_lifetime lifetime;
    cxpr_model_child_program* children;
    size_t child_count;
    cxpr_model_history_spec* history_specs;
    size_t history_spec_count;
    char** outputs;
    size_t output_count;
};

struct cxpr_model_session {
    const cxpr_model_compiled* program;
    cxpr_context* ctx;
    cxpr_model_output_state* outputs;
    size_t output_count;
    cxpr_model_history_entry* histories;
    size_t history_count;
    double* fused_slots;
    size_t fused_slot_count;
    cxpr_context_slot* fused_input_slots;
    bool* fused_input_slot_bound;
    size_t fused_input_slot_count;
    cxpr_context_slot* fused_export_slots;
    bool* fused_export_slot_bound;
    size_t fused_export_slot_count;
    cxpr_context_slot* fused_commit_slots;
    bool* fused_commit_slot_bound;
    size_t fused_commit_slot_count;
    double* fused_pending_values;
    bool* fused_pending_bound;
    size_t fused_pending_count;
    cxpr_model_session** child_sessions;
    size_t child_session_count;
    cxpr_model_child_instance* child_instances;
    size_t child_instance_count;
    size_t child_instance_capacity;
    cxpr_value* pending_values;
    size_t* pending_binding_indices;
    size_t pending_capacity;
    size_t pending_count;
};

void cxpr_model_set_error(cxpr_error* err, cxpr_error_code code,
                          const char* message, size_t line, size_t column);
bool cxpr_model_names_match(const char* a, const char* b);
bool cxpr_model_ast_equal(const cxpr_expr_ast* left, const cxpr_expr_ast* right);
void cxpr_model_context_set_compiled_number(cxpr_context* ctx,
                                            const cxpr_model_compiled_binding* binding,
                                            double value);
void cxpr_model_context_set_compiled_bool(cxpr_context* ctx,
                                          const cxpr_model_compiled_binding* binding,
                                          bool value);
void cxpr_model_context_set_compiled_typed(cxpr_context* ctx,
                                           const cxpr_model_compiled_binding* binding,
                                           const cxpr_value* value);
cxpr_expr_ast* cxpr_model_inline_locals(const cxpr_expr_ast* ast,
                                   const cxpr_model_local_binding* locals,
                                   size_t local_count);
bool cxpr_model_lookback_target_key(const cxpr_expr_ast* target,
                                    char** out_key,
                                    cxpr_error* err);

bool cxpr_model_lookback_bound(const cxpr_model* model,
                               const cxpr_expr_ast* index,
                               size_t* out_bound,
                               cxpr_error* err);
bool cxpr_model_collect_lookbacks(const cxpr_model* model,
                                  cxpr_model_history_spec** specs,
                                  size_t* count,
                                  cxpr_error* err);
bool cxpr_model_lookback_resolver(const cxpr_expr_ast* target,
                                  const cxpr_expr_ast* index,
                                  const cxpr_context* ctx,
                                  const cxpr_registry* reg,
                                  void* userdata,
                                  cxpr_value* out,
                                  cxpr_error* err);
bool cxpr_model_collect_required_defaults(const cxpr_model* model,
                                          char*** names,
                                          size_t* count,
                                          cxpr_error* err);
void cxpr_model_record_fields_free(cxpr_model_record_field* fields, size_t count);
void cxpr_model_record_function_clear(cxpr_model_record_function* fn);
const cxpr_expr_ast* cxpr_model_local_lookup(const cxpr_model_local_binding* locals,
                                        size_t count,
                                        const char* name);
bool cxpr_model_compiled_mark_required_bindings(const cxpr_model_compiled* program,
                                               const size_t* output_indices,
                                               size_t output_count,
                                               bool include_all_outputs,
                                               bool include_state_commits,
                                               bool include_history_captures,
                                               bool* out_required,
                                               cxpr_error* err);
size_t cxpr_model_fused_slot_find(char* const* names, size_t count, const char* name);
void cxpr_model_fused_program_clear(cxpr_model_compiled* program);
bool cxpr_model_try_compile_fused_ir(cxpr_model_compiled* program,
                                     const cxpr_model* model,
                                     const cxpr_registry* reg,
                                     cxpr_error* err);
bool cxpr_model_compiled_register_imports(cxpr_model_compiled* program,
                                         const cxpr_model* model,
                                         const cxpr_model_import* imports,
                                         size_t import_count,
                                         cxpr_error* err);
cxpr_value cxpr_model_eval_child_producer(const cxpr_expr_ast* ast,
                                          const cxpr_context* ctx,
                                          const cxpr_registry* reg,
                                          void* userdata,
                                          cxpr_error* err);
cxpr_model_session* cxpr_model_active_session(void);

#endif /* CXPR_MODEL_INTERNAL_H */
