#ifndef CXPR_MODEL_INTERNAL_H
#define CXPR_MODEL_INTERNAL_H

#include "core.h"
#include "ir/compile/internal.h"
#include <cxpr/model.h>
#include <cxpr/source_plan.h>

typedef struct {
    char* name;
    char* source;
    cxpr_ast* expr;
} cxpr_model_constant;

typedef struct {
    cxpr_model_binding_kind kind;
    char* name;
    char* source;
    cxpr_ast* expr;
} cxpr_model_binding;

typedef struct {
    char* name;
    char* source;
    cxpr_ast* expr;
} cxpr_model_record_field;

typedef struct {
    char* name;
    char** params;
    size_t param_count;
    cxpr_model_record_field* fields;
    size_t field_count;
} cxpr_model_record_function;

typedef struct {
    char* name;
    char* body;
    cxpr_model_metadata_target_kind target_kind;
    char* target_name;
} cxpr_model_metadata;

typedef struct {
    char* key;
    char* value;
} cxpr_model_host_block_field;

struct cxpr_model_host_block {
    char* kind;
    char* name;
    char* body;
    cxpr_model_host_block_field* fields;
    size_t field_count;
    cxpr_model_host_block* children;
    size_t child_count;
};

struct cxpr_model {
    char* name;
    char** uses;
    char** use_aliases;
    size_t use_count;
    char** functions;
    size_t function_count;
    cxpr_model_record_function* record_functions;
    size_t record_function_count;
    char** inputs;
    size_t input_count;
    cxpr_model_constant* constants;
    size_t constant_count;
    cxpr_model_binding* bindings;
    size_t binding_count;
    char** outputs;
    size_t output_count;
    cxpr_model_metadata* metadatas;
    size_t metadata_count;
    cxpr_model_host_block* host_blocks;
    size_t host_block_count;
};

typedef struct {
    cxpr_model_binding_kind kind;
    char* name;
    unsigned long name_hash;
    cxpr_ir_view_result_kind result_kind;
    cxpr_ast* ast;
} cxpr_model_compiled_binding;

typedef struct {
    char* name;
    cxpr_ast* expr;
} cxpr_model_local_binding;

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

typedef struct {
    char* name;
    cxpr_ast* target;
    size_t depth;
} cxpr_model_history_spec;

typedef struct {
    char* name;
    unsigned long hash;
    size_t slot;
    cxpr_ir_view_result_kind result_kind;
} cxpr_model_slot_ref;

typedef struct {
    size_t state_slot;
    size_t update_slot;
} cxpr_model_state_commit;

typedef struct {
    char* name;
    cxpr_value* values;
    size_t capacity;
    size_t count;
    size_t next;
} cxpr_model_history_entry;

typedef struct {
    char* name;
    const cxpr_model_program* program;
    size_t registry_index;
    char* source_arg;
    size_t source_input_index;
} cxpr_model_child_program;

struct cxpr_model_program {
    cxpr_registry* registry;
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
    cxpr_model_child_program* children;
    size_t child_count;
    cxpr_model_history_spec* history_specs;
    size_t history_spec_count;
    char** outputs;
    size_t output_count;
};

struct cxpr_model_session {
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
    cxpr_value* pending_values;
    size_t* pending_binding_indices;
    size_t pending_capacity;
    size_t pending_count;
};

void cxpr_model_set_error(cxpr_error* err, cxpr_error_code code,
                          const char* message, size_t line, size_t column);
bool cxpr_model_names_match(const char* a, const char* b);
bool cxpr_model_ast_equal(const cxpr_ast* left, const cxpr_ast* right);
void cxpr_model_context_set_compiled_number(cxpr_context* ctx,
                                            const cxpr_model_compiled_binding* binding,
                                            double value);
void cxpr_model_context_set_compiled_bool(cxpr_context* ctx,
                                          const cxpr_model_compiled_binding* binding,
                                          bool value);
void cxpr_model_context_set_compiled_typed(cxpr_context* ctx,
                                           const cxpr_model_compiled_binding* binding,
                                           const cxpr_value* value);
cxpr_ast* cxpr_model_inline_locals(const cxpr_ast* ast,
                                   const cxpr_model_local_binding* locals,
                                   size_t local_count);
bool cxpr_model_lookback_target_key(const cxpr_ast* target,
                                    char** out_key,
                                    cxpr_error* err);
bool cxpr_model_lookback_resolver(const cxpr_ast* target,
                                  const cxpr_ast* index,
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
const cxpr_ast* cxpr_model_local_lookup(const cxpr_model_local_binding* locals,
                                        size_t count,
                                        const char* name);
bool cxpr_model_program_mark_required_bindings(const cxpr_model_program* program,
                                               const size_t* output_indices,
                                               size_t output_count,
                                               bool include_all_outputs,
                                               bool include_state_commits,
                                               bool include_history_captures,
                                               bool* out_required,
                                               cxpr_error* err);
size_t cxpr_model_fused_slot_find(char* const* names, size_t count, const char* name);
void cxpr_model_fused_program_clear(cxpr_model_program* program);
bool cxpr_model_try_compile_fused_ir(cxpr_model_program* program,
                                     const cxpr_model* model,
                                     const cxpr_registry* reg,
                                     cxpr_error* err);
bool cxpr_model_program_register_imports(cxpr_model_program* program,
                                         const cxpr_model* model,
                                         const cxpr_model_import* imports,
                                         size_t import_count,
                                         cxpr_error* err);
cxpr_value cxpr_model_eval_child_producer(const cxpr_ast* ast,
                                          const cxpr_context* ctx,
                                          const cxpr_registry* reg,
                                          void* userdata,
                                          cxpr_error* err);
cxpr_model_session* cxpr_model_active_session(void);

#endif /* CXPR_MODEL_INTERNAL_H */
