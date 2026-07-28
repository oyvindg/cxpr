/**
 * @file model/model.h
 * @brief Public API for parsed .cxpr model files.
 */

#ifndef CXPR_MODEL_H
#define CXPR_MODEL_H

#include <cxpr/ast/expression.h>
#include <cxpr/source.h>
#include <cxpr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CXPR_MODEL_BINDING_EXPR = 0,
    CXPR_MODEL_BINDING_STATE = 1,
    CXPR_MODEL_BINDING_STATE_UPDATE = 2,
    CXPR_MODEL_BINDING_LOCAL = 3,
    CXPR_MODEL_BINDING_STATE_OUT = 2 /* Deprecated alias for v1 draft compatibility. */
} cxpr_model_binding_kind;

typedef enum {
    CXPR_MODEL_METADATA_TARGET_MODEL = 0,
    CXPR_MODEL_METADATA_TARGET_USE = 1,
    CXPR_MODEL_METADATA_TARGET_INPUT = 2,
    CXPR_MODEL_METADATA_TARGET_PARAM = 3,
    CXPR_MODEL_METADATA_TARGET_BINDING = 4,
    CXPR_MODEL_METADATA_TARGET_STATE = 5,
    CXPR_MODEL_METADATA_TARGET_FUNCTION = 6,
    CXPR_MODEL_METADATA_TARGET_OUTPUT = 7,
} cxpr_model_metadata_target_kind;

typedef enum {
    CXPR_MODEL_RESULT_UNKNOWN = 0,
    CXPR_MODEL_RESULT_NUMBER = 1,
    CXPR_MODEL_RESULT_BOOL = 2,
} cxpr_model_result_kind;

typedef enum {
    CXPR_MODEL_BACKEND_AUTO = 0,
    CXPR_MODEL_BACKEND_IR = 1,
    CXPR_MODEL_BACKEND_C = 2,
} cxpr_model_backend_kind;

/** @brief Options controlling model compilation and backend selection. */
typedef struct {
    cxpr_model_backend_kind backend;
    bool fuse;
    bool enable_trace;
} cxpr_model_compile_options;

/** @brief One precompiled model supplied as a direct import. */
typedef struct {
    const char* name;
    const cxpr_model_program* program;
} cxpr_model_import;

/** @brief Result of resolving one model `use` declaration. */
typedef struct cxpr_model_use_resolution {
    const char* namespace_name;
    int handled;
} cxpr_model_use_resolution;

typedef int (*cxpr_model_resolve_use_fn)(const cxpr_model* model,
                                         size_t index,
                                         const char* path,
                                         const char* alias,
                                         cxpr_model_use_resolution* out_resolution,
                                         void* userdata,
                                         cxpr_error* err);

/** @brief Callback bundle used to resolve model `use` declarations. */
typedef struct cxpr_model_use_resolver {
    cxpr_model_resolve_use_fn resolve;
    void* userdata;
} cxpr_model_use_resolver;

/** @brief Opaque parsed host block node. */
typedef struct cxpr_model_host_block cxpr_model_host_block;

/** @brief Opaque registry for host-defined .cxpr model block kinds. */
typedef struct cxpr_host_block_registry cxpr_host_block_registry;

/**
 * @brief Host-owned validation callback for one parsed host block.
 *
 * The callback receives borrowed strings owned by @p model. Return non-zero on
 * success. On failure, set @p err when a host-specific diagnostic is useful.
 */
typedef int (*cxpr_host_block_validate_fn)(const char* kind,
                                           const char* name,
                                           const char* body,
                                           void* userdata,
                                           cxpr_error* err);

/**
 * @brief Host-owned validation callback for one parsed host block tree.
 *
 * This is preferred for nested host block schemas. The callback receives a
 * borrowed block node owned by @p model. Return non-zero on success.
 */
typedef int (*cxpr_host_block_validate_block_fn)(const cxpr_model_host_block* block,
                                                 void* userdata,
                                                 cxpr_error* err);

/**
 * @brief Host declaration for one model-level block kind.
 *
 * cxpr owns syntax and lifecycle; the host owns schema and semantics. The core
 * validator only enforces whether a parsed block kind is known, whether it may
 * be named, whether it may occur more than once, and then delegates body
 * semantics to @p validate when supplied.
 */
typedef struct cxpr_host_block_spec {
    const char* kind;
    int allow_named;
    int allow_multiple;
    cxpr_host_block_validate_fn validate;
    cxpr_host_block_validate_block_fn validate_block;
    void* userdata;
} cxpr_host_block_spec;

/**
 * @brief Parse a .cxpr model document and return an owned semantic model.
 *
 * This is the model-owning entrypoint backed by the document AST parser and
 * lowerer. Free the returned model with @ref cxpr_model_free.
 */
cxpr_model* cxpr_parse_model_source(const char* source, cxpr_error* err);

/** @brief Free a parsed .cxpr model. */
void cxpr_model_free(cxpr_model* model);

/**
 * @brief Validate model-level symbols and references.
 *
 * This performs host-agnostic semantic checks only:
 * - required model name
 * - duplicate inputs/constants/bindings/outputs
 * - outputs reference existing public symbols
 * - `$param` references resolve to model constants
 * - runtime references resolve to inputs or named bindings
 *
 * Function, plugin and host-source resolution intentionally live outside this
 * API so the core model layer remains domain independent.
 *
 * @return True when the model is semantically valid at the core level.
 */
bool cxpr_model_validate(const cxpr_model* model, cxpr_error* err);

/**
 * @brief Validate model symbols while allowing host-provided external roots.
 *
 * External refs are root names such as preset or imported record names. Runtime
 * references matching `root` or `root.field` are accepted as host-resolved
 * values, while constants remain local-only.
 */
bool cxpr_model_validate_with_external_refs(const cxpr_model* model,
                                            char* const* external_refs,
                                            size_t external_ref_count,
                                            cxpr_error* err);

bool cxpr_model_resolve_uses(const cxpr_model* model,
                             const cxpr_model_use_resolver* resolver,
                             cxpr_error* err);

/**
 * @brief Validate that every model `use` path resolves to an existing .cxpr file.
 *
 * Paths are resolved relative to @p model_path. `indicators/...` imports also
 * probe ancestor `libs/dyn/cxpr/...` directories to match cxpr codegen tooling.
 */
bool cxpr_model_validate_use_files(const cxpr_model* model,
                                   const char* model_path,
                                   cxpr_error* err);

/**
 * @brief Create a registry for host-defined model block kinds.
 *
 * The registry copies specs by value and borrows spec strings/userdata.
 */
cxpr_host_block_registry* cxpr_host_block_registry_new(void);

/** @brief Free a host block registry. */
void cxpr_host_block_registry_free(cxpr_host_block_registry* registry);

/**
 * @brief Register one host block kind.
 *
 * @return True on success, false when arguments are invalid, the kind is
 * already registered, or allocation fails.
 */
bool cxpr_host_block_registry_register(cxpr_host_block_registry* registry,
                                       const cxpr_host_block_spec* spec);

/**
 * @brief Validate all parsed host blocks against a host registry.
 *
 * Unknown block kinds are rejected. A NULL registry is accepted only when the
 * model contains no host blocks.
 */
bool cxpr_model_validate_host_blocks(const cxpr_model* model,
                                     const cxpr_host_block_registry* registry,
                                     cxpr_error* err);

/**
 * @brief Return binding indices in dependency-safe evaluation order.
 *
 * This builds on the existing expression-set analysis/toposort. The returned
 * indices refer to `cxpr_model_binding_*` accessors.
 *
 * @param model Parsed and validated model.
 * @param out_order Output array with capacity for `cxpr_model_binding_count(model)` entries.
 * @param max_order Capacity of `out_order`.
 * @param err Optional error output.
 * @return True on success; false on cycles, invalid arguments or allocation failure.
 */
bool cxpr_model_eval_order(const cxpr_model* model, size_t* out_order,
                           size_t max_order, cxpr_error* err);

/**
 * @brief Discover and bind provider-scoped source needs used by a .cxpr model.
 *
 * This is the model-level counterpart to @ref cxpr_plan_bind_sources. It keeps
 * syntax such as `close("1d")`, `close(timeframe="1d")` and
 * `ema(source=close, period=14, timeframe="1d")` host agnostic: cxpr parses and
 * reports structured source-plan nodes, while the host decides how those scopes
 * map to concrete data, resampling, replay, caches, or generated inputs.
 *
 * Call this after parsing and before compiling when the model uses host-backed
 * scoped sources. If @p config has a resolver and @p reg is mutable, scoped
 * source functions declared by provider metadata are registered into @p reg.
 */
bool cxpr_model_plan_bind_sources(const cxpr_model* model,
                                  const cxpr_provider* provider,
                                  const cxpr_context* ctx,
                                  cxpr_registry* reg,
                                  const cxpr_plan_config* config,
                                  cxpr_source_plan_bindings* out,
                                  cxpr_error* err);

/**
 * @brief Compile a parsed .cxpr model into immutable executable programs.
 *
 * The compiled model stores dependency order once and may select an optimized
 * scalar backend when the model shape supports it. It does not register model
 * bindings in a runtime evaluator, avoiding per-tick binding setup.
 */
cxpr_model_program* cxpr_compile_model(const cxpr_model* model,
                                       const cxpr_registry* reg,
                                       cxpr_error* err);

cxpr_model_program* cxpr_compile_model_with_imports(const cxpr_model* model,
                                                    const cxpr_registry* reg,
                                                    const cxpr_model_import* imports,
                                                    size_t import_count,
                                                    cxpr_error* err);

/**
 * @brief Compile a parsed .cxpr model with explicit backend options.
 *
 * NULL options use `{ CXPR_MODEL_BACKEND_AUTO, true, false }`.
 */
cxpr_model_program* cxpr_compile_model_with_options(
    const cxpr_model* model,
    const cxpr_registry* reg,
    const cxpr_model_compile_options* options,
    cxpr_error* err);

cxpr_model_program* cxpr_compile_model_with_imports_and_options(
    const cxpr_model* model,
    const cxpr_registry* reg,
    const cxpr_model_import* imports,
    size_t import_count,
    const cxpr_model_compile_options* options,
    cxpr_error* err);

/** @brief Free a compiled .cxpr model program. */
void cxpr_model_program_free(cxpr_model_program* program);

/**
 * @brief Seed model `$` constants into a context.
 *
 * Call once when creating a session/context, then override params on that
 * context as needed. Evaluation does not re-seed constants per tick.
 */
bool cxpr_model_program_seed_defaults(const cxpr_model_program* program,
                                      cxpr_context* ctx,
                                      const cxpr_registry* reg,
                                      cxpr_error* err);

/**
 * @brief Evaluate all model bindings in dependency order.
 *
 * Results are written back into @p ctx under their binding names. Inputs and
 * `$` params are read from @p ctx; the host remains responsible only for
 * supplying inputs and reading outputs.
 */
bool cxpr_eval_model_program(const cxpr_model_program* program,
                             cxpr_context* ctx,
                             const cxpr_registry* reg,
                             cxpr_error* err);

/** @brief Return the number of dependency-ordered executable bindings. */
size_t cxpr_model_program_binding_count(const cxpr_model_program* program);

/** @brief Return the name of executable binding @p index, or NULL when out of range. */
const char* cxpr_model_program_binding_name(const cxpr_model_program* program, size_t index);

/** @brief Return the semantic binding kind for executable binding @p index. */
cxpr_model_binding_kind cxpr_model_program_binding_kind(const cxpr_model_program* program,
                                                        size_t index);

/** @brief Return the inferred scalar result kind for executable binding @p index. */
cxpr_model_result_kind cxpr_model_program_binding_result_kind(
    const cxpr_model_program* program,
    size_t index);

/** @brief Return the number of compiled parameter/default expressions. */
size_t cxpr_model_program_constant_count(const cxpr_model_program* program);

/** @brief Return the parameter/default name at @p index, or NULL when out of range. */
const char* cxpr_model_program_constant_name(const cxpr_model_program* program, size_t index);

/** @brief Return the inferred scalar result kind for parameter/default @p index. */
cxpr_model_result_kind cxpr_model_program_constant_result_kind(
    const cxpr_model_program* program,
    size_t index);

/** @brief Return the number of compiled state initializer expressions. */
size_t cxpr_model_program_state_default_count(const cxpr_model_program* program);

/** @brief Return the state initializer name at @p index, or NULL when out of range. */
const char* cxpr_model_program_state_default_name(const cxpr_model_program* program,
                                                  size_t index);

/** @brief Return the inferred scalar result kind for state initializer @p index. */
cxpr_model_result_kind cxpr_model_program_state_default_result_kind(
    const cxpr_model_program* program,
    size_t index);

/** @brief Return the number of exported model outputs. */
size_t cxpr_model_program_output_count(const cxpr_model_program* program);

/** @brief Return output name @p index, or NULL when out of range. */
const char* cxpr_model_program_output_name(const cxpr_model_program* program, size_t index);

/** @brief Return the number of declared model inputs. */
size_t cxpr_model_program_input_count(const cxpr_model_program* program);

/** @brief Return input name @p index, or NULL when out of range. */
const char* cxpr_model_program_input_name(const cxpr_model_program* program, size_t index);

/** @brief Return the number of resolved imported child model programs. */
size_t cxpr_model_program_child_count(const cxpr_model_program* program);

/** @brief Return resolved child model alias/name @p index, or NULL when out of range. */
const char* cxpr_model_program_child_name(const cxpr_model_program* program, size_t index);

/** @brief Return the input name used as the child model source argument, if any. */
const char* cxpr_model_program_child_source_arg(const cxpr_model_program* program, size_t index);

/** @brief Return the number of planned history/lookback buffers. */
size_t cxpr_model_program_history_spec_count(const cxpr_model_program* program);

/** @brief Return planned history buffer name @p index, or NULL when out of range. */
const char* cxpr_model_program_history_spec_name(const cxpr_model_program* program, size_t index);

/** @brief Return planned history depth for history buffer @p index. */
size_t cxpr_model_program_history_spec_depth(const cxpr_model_program* program, size_t index);

/** @brief Return number of functions in the model-owned registry, for diagnostics/benchmarks. */
size_t cxpr_model_program_function_count(const cxpr_model_program* program);

/** @brief Return the backend requested at compile time. */
cxpr_model_backend_kind cxpr_model_program_requested_backend(const cxpr_model_program* program);

/** @brief Return the backend selected by compilation. AUTO means evaluator path. */
cxpr_model_backend_kind cxpr_model_program_selected_backend(const cxpr_model_program* program);

/** @brief Return whether compile options allowed backend fusion. */
bool cxpr_model_program_compile_fuse_enabled(const cxpr_model_program* program);

/** @brief Return whether compile options requested trace-friendly evaluation. */
bool cxpr_model_program_compile_trace_enabled(const cxpr_model_program* program);

/** @brief Return true when the compiled model uses the scalar fast-path backend for ticks. */
bool cxpr_model_program_uses_fast_path(const cxpr_model_program* program);

/** @brief Return fast-path backend instruction count, or 0 when the fast path is not active. */
size_t cxpr_model_program_fast_path_instruction_count(const cxpr_model_program* program);

/** @brief Return why fast-path compilation was skipped, or NULL when active/not attempted. */
const char* cxpr_model_program_fast_path_disabled_reason(const cxpr_model_program* program);

/** @brief Return the number of resolved fast-path runtime slots. */
size_t cxpr_model_program_fast_path_slot_count(const cxpr_model_program* program);

/** @brief Return fast-path slot name @p index, or NULL when out of range. */
const char* cxpr_model_program_fast_path_slot_name(const cxpr_model_program* program, size_t index);

/** @brief Return the number of fast-path input slot references. */
size_t cxpr_model_program_fast_path_input_count(const cxpr_model_program* program);

/** @brief Return fast-path input reference name @p index, or NULL when out of range. */
const char* cxpr_model_program_fast_path_input_name(const cxpr_model_program* program, size_t index);

/** @brief Return fast-path slot index read by input reference @p index, or SIZE_MAX when invalid. */
size_t cxpr_model_program_fast_path_input_slot(const cxpr_model_program* program, size_t index);

/** @brief Return inferred scalar result kind for fast-path input reference @p index. */
cxpr_model_result_kind cxpr_model_program_fast_path_input_result_kind(
    const cxpr_model_program* program,
    size_t index);

/** @brief Return the number of fast-path context export references. */
size_t cxpr_model_program_fast_path_export_count(const cxpr_model_program* program);

/** @brief Return fast-path export reference name @p index, or NULL when out of range. */
const char* cxpr_model_program_fast_path_export_name(const cxpr_model_program* program, size_t index);

/** @brief Return fast-path slot index written by export reference @p index, or SIZE_MAX when invalid. */
size_t cxpr_model_program_fast_path_export_slot(const cxpr_model_program* program, size_t index);

/** @brief Return inferred scalar result kind for fast-path export reference @p index. */
cxpr_model_result_kind cxpr_model_program_fast_path_export_result_kind(
    const cxpr_model_program* program,
    size_t index);

/** @brief Return the number of fast-path output references. */
size_t cxpr_model_program_fast_path_output_count(const cxpr_model_program* program);

/** @brief Return fast-path output reference name @p index, or NULL when out of range. */
const char* cxpr_model_program_fast_path_output_name(const cxpr_model_program* program, size_t index);

/** @brief Return fast-path slot index read by output reference @p index, or SIZE_MAX when invalid. */
size_t cxpr_model_program_fast_path_output_slot(const cxpr_model_program* program, size_t index);

/** @brief Return inferred scalar result kind for fast-path output reference @p index. */
cxpr_model_result_kind cxpr_model_program_fast_path_output_result_kind(
    const cxpr_model_program* program,
    size_t index);

/** @brief Return the number of fast-path atomic state commit mappings. */
size_t cxpr_model_program_fast_path_commit_count(const cxpr_model_program* program);

/** @brief Return destination state slot for fast-path commit @p index, or SIZE_MAX when invalid. */
size_t cxpr_model_program_fast_path_commit_state_slot(const cxpr_model_program* program, size_t index);

/** @brief Return pending update slot for fast-path commit @p index, or SIZE_MAX when invalid. */
size_t cxpr_model_program_fast_path_commit_update_slot(const cxpr_model_program* program, size_t index);

/**
 * @brief Emit a `.cxpr` model-defined scalar `fn` as a standalone C function.
 *
 * This uses the compiled model's own registry and does not require host
 * registration of the function. Unsupported dynamic backend codegen returns
 * NULL with @p err set, so callers can keep the normal evaluator as fallback.
 */
char* cxpr_model_program_function_to_c_function(const cxpr_model_program* program,
                                                const char* name,
                                                const char* qualifiers,
                                                const char* return_type,
                                                const char* function_name,
                                                cxpr_error* err);
/**
 * @brief Emit a fast-path scalar model as a standalone C tick function.
 *
 * Generated ABI:
 * `typedef struct fn_state fn_state;`
 * `void fn(fn_state* state, const double* inputs, const double* params, double* outputs)`.
 * When model state needs eager setup, the generated source also includes
 * `void fn_init_state(fn_state* state)`. The tick function keeps a lazy
 * first-use init guard. Callers must zero the complete state object before its
 * first tick or call the generated init function explicitly.
 * Inputs and outputs use model declaration order. Params use model constant
 * order. The generated `state` object owns model scratch/state storage.
 */
char* cxpr_model_program_to_c_tick_function(const cxpr_model_program* program,
                                            const char* qualifiers,
                                            const char* function_name,
                                            cxpr_error* err);
/**
 * @brief Emit a C tick function that writes only selected model outputs.
 *
 * The generated ABI is unchanged, but `_cx_outputs` is compact: entry `i`
 * receives model output `output_indices[i]`. State updates and history capture
 * are still evaluated normally.
 */
char* cxpr_model_program_to_c_tick_function_select_outputs(
    const cxpr_model_program* program,
    const char* qualifiers,
    const char* function_name,
    const size_t* output_indices,
    size_t output_count,
    cxpr_error* err);
/**
 * @brief Emit a C tick function with model `$` params baked in as numeric literals.
 *
 * @p param_values must contain one value for each
 * `cxpr_model_program_c_param_name(program, i)` entry. The generated function
 * keeps the normal params pointer in its ABI for caller compatibility, but the
 * emitted expression code does not load from it. Window periods that resolve to
 * these literals are emitted as fixed hot-path state updates; dynamic-period
 * fallback code is omitted for those windows.
 */
char* cxpr_model_program_to_c_tick_function_with_params(const cxpr_model_program* program,
                                                        const char* qualifiers,
                                                        const char* function_name,
                                                        const double* param_values,
                                                        size_t param_count,
                                                        cxpr_error* err);
/**
 * @brief Emit a specialized C tick function that writes only selected outputs.
 *
 * This combines @ref cxpr_model_program_to_c_tick_function_with_params and
 * @ref cxpr_model_program_to_c_tick_function_select_outputs.
 */
char* cxpr_model_program_to_c_tick_function_with_params_select_outputs(
    const cxpr_model_program* program,
    const char* qualifiers,
    const char* function_name,
    const double* param_values,
    size_t param_count,
    const size_t* output_indices,
    size_t output_count,
    cxpr_error* err);
/** @brief Legacy backend slot count. The generated state ABI does not expose slots. */
size_t cxpr_model_program_c_slot_count(const cxpr_model_program* program);
size_t cxpr_model_program_c_param_count(const cxpr_model_program* program);
const char* cxpr_model_program_c_param_name(const cxpr_model_program* program, size_t index);
size_t cxpr_model_program_call_param_count(const cxpr_model_program* program);
const char* cxpr_model_program_call_param_name(const cxpr_model_program* program, size_t index);

/** @brief Create a mutable session for one compiled model. */
cxpr_model_session* cxpr_model_session_new(const cxpr_model_program* program,
                                           const cxpr_registry* reg,
                                           cxpr_error* err);
/** @brief Free a model session. */
void cxpr_model_session_free(cxpr_model_session* session);
/** @brief Return the session-owned context for host input writes and output reads. */
cxpr_context* cxpr_model_session_context(cxpr_model_session* session);
/**
 * @brief Evaluate one deterministic model tick with the reference/tooling runtime.
 *
 * This interpreter-style session API exists for diagnostics, editor/tooling,
 * parity tests, and explicit fallback paths. Production/backtest/optimizer
 * per-bar loops should use generated C from `cxpr_model_program_to_c_tick_function*`
 * when the caller requires hot-path execution.
 *
 * Expressions in tick N read state from the start of that tick. State update
 * values are staged during evaluation and committed at the start of the next
 * tick. Direct state outputs may expose the staged next value for the current
 * tick; other expressions still observe the current state.
 */
bool cxpr_model_session_tick(const cxpr_model_program* program,
                             cxpr_model_session* session,
                             const cxpr_registry* reg,
                             cxpr_error* err);
/**
 * @brief Evaluate one fast-path model tick without materializing state/outputs into the context.
 *
 * This is a hot-path API for hosts that write inputs through the session
 * context and read results through `cxpr_model_session_output_*`. It preserves
 * atomic state commits in backend slots, but skips per-tick context writes and
 * history capture when the model does not require lookback history. Unsupported
 * models fall back to `cxpr_model_session_tick`.
 */
bool cxpr_model_session_tick_fast(const cxpr_model_program* program,
                                  cxpr_model_session* session,
                                  const cxpr_registry* reg,
                                  cxpr_error* err);
bool cxpr_model_session_output_bool(const cxpr_model_session* session,
                                    const char* name,
                                    bool* out_value);
bool cxpr_model_session_output_number(const cxpr_model_session* session,
                                      const char* name,
                                      double* out_value);
bool cxpr_model_session_output_rising(const cxpr_model_session* session, const char* name);
bool cxpr_model_session_output_falling(const cxpr_model_session* session, const char* name);
bool cxpr_model_session_output_changed(const cxpr_model_session* session, const char* name);

const char* cxpr_model_name(const cxpr_model* model);
bool cxpr_model_name_source_span(const cxpr_model* model, cxpr_source_span* out_span);

size_t cxpr_model_use_count(const cxpr_model* model);
const char* cxpr_model_use(const cxpr_model* model, size_t index);
const char* cxpr_model_use_alias(const cxpr_model* model, size_t index);
bool cxpr_model_use_source_span(const cxpr_model* model,
                                size_t index,
                                cxpr_source_span* out_span);

size_t cxpr_model_function_count(const cxpr_model* model);
const char* cxpr_model_function_source(const cxpr_model* model, size_t index);
bool cxpr_model_function_declaration_source(const cxpr_model* model,
                                            size_t index,
                                            char** out_source);

size_t cxpr_model_input_count(const cxpr_model* model);
const char* cxpr_model_input(const cxpr_model* model, size_t index);
bool cxpr_model_input_source_span(const cxpr_model* model,
                                  size_t index,
                                  cxpr_source_span* out_span);

size_t cxpr_model_constant_count(const cxpr_model* model);
const char* cxpr_model_constant_name(const cxpr_model* model, size_t index);
const cxpr_expr_ast* cxpr_model_constant_expr(const cxpr_model* model, size_t index);
/** @brief Return whether a model parameter is explicitly exported in its `in` signature. */
bool cxpr_model_constant_is_call_param(const cxpr_model* model, size_t index);
/** @brief Return the number of explicitly exported call parameters. */
size_t cxpr_model_call_param_count(const cxpr_model* model);
bool cxpr_model_constant_source_span(const cxpr_model* model,
                                     size_t index,
                                     cxpr_source_span* out_span);

size_t cxpr_model_binding_count(const cxpr_model* model);
cxpr_model_binding_kind cxpr_model_binding_kind_at(const cxpr_model* model, size_t index);
const char* cxpr_model_binding_name(const cxpr_model* model, size_t index);
const cxpr_expr_ast* cxpr_model_binding_expr(const cxpr_model* model, size_t index);
bool cxpr_model_binding_source_span(const cxpr_model* model,
                                    size_t index,
                                    cxpr_source_span* out_span);

size_t cxpr_model_output_count(const cxpr_model* model);
const char* cxpr_model_output(const cxpr_model* model, size_t index);
bool cxpr_model_output_source_span(const cxpr_model* model,
                                   size_t index,
                                   cxpr_source_span* out_span);

size_t cxpr_model_metadata_count(const cxpr_model* model);
const char* cxpr_model_metadata_name(const cxpr_model* model, size_t index);
const char* cxpr_model_metadata_body(const cxpr_model* model, size_t index);
cxpr_model_metadata_target_kind cxpr_model_metadata_target_kind_at(
    const cxpr_model* model,
    size_t index);
const char* cxpr_model_metadata_target_name(const cxpr_model* model, size_t index);
bool cxpr_model_metadata_source_span(const cxpr_model* model,
                                     size_t index,
                                     cxpr_source_span* out_span);
const char* cxpr_model_metadata_field_value(const cxpr_model* model,
                                            size_t index,
                                            const char* key);
bool cxpr_model_metadata_field_number(const cxpr_model* model,
                                      size_t index,
                                      const char* key,
                                      double* out_value);
bool cxpr_model_metadata_field_number_list(const cxpr_model* model,
                                           size_t index,
                                           const char* key,
                                           double** out_values,
                                           size_t* out_count);

size_t cxpr_model_host_block_count(const cxpr_model* model);
const char* cxpr_model_host_block_kind(const cxpr_model* model, size_t index);
const char* cxpr_model_host_block_name(const cxpr_model* model, size_t index);
const char* cxpr_model_host_block_body(const cxpr_model* model, size_t index);
const cxpr_model_host_block* cxpr_model_host_block_at(const cxpr_model* model, size_t index);
const cxpr_model_host_block* cxpr_model_host_block_by_kind(const cxpr_model* model,
                                                           const char* kind);
bool cxpr_model_host_block_source_span(const cxpr_model* model,
                                       size_t index,
                                       cxpr_source_span* out_span);

const char* cxpr_host_block_kind(const cxpr_model_host_block* block);
const char* cxpr_host_block_name(const cxpr_model_host_block* block);
const char* cxpr_host_block_body(const cxpr_model_host_block* block);
bool cxpr_host_block_source_span(const cxpr_model_host_block* block,
                                 cxpr_source_span* out_span);
size_t cxpr_host_block_field_count(const cxpr_model_host_block* block);
const char* cxpr_host_block_field_key(const cxpr_model_host_block* block, size_t index);
const char* cxpr_host_block_field_value(const cxpr_model_host_block* block, size_t index);
const char* cxpr_host_block_field_value_by_key(const cxpr_model_host_block* block,
                                               const char* key);
bool cxpr_host_block_field_is_bare_flag(const cxpr_model_host_block* block,
                                        size_t index);
bool cxpr_host_block_field_string_by_key(const cxpr_model_host_block* block,
                                         const char* key,
                                         char** out_value);
bool cxpr_host_block_field_string_list_by_key(const cxpr_model_host_block* block,
                                              const char* key,
                                              char*** out_values,
                                              size_t* out_count);
size_t cxpr_host_block_child_count(const cxpr_model_host_block* block);
const cxpr_model_host_block* cxpr_host_block_child(const cxpr_model_host_block* block,
                                                   size_t index);
const cxpr_model_host_block* cxpr_host_block_child_by_kind(
    const cxpr_model_host_block* block,
    const char* kind);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_MODEL_H */
