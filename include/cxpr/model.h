/**
 * @file model.h
 * @brief Public API for parsed .cxpr model files.
 */

#ifndef CXPR_MODEL_H
#define CXPR_MODEL_H

#include <cxpr/ast.h>
#include <cxpr/source_plan.h>
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

/**
 * @brief Parse a complete .cxpr model.
 *
 * This API is additive to the existing expression parser. Expression right-hand
 * sides are parsed with `cxpr_parse()` and stored as AST nodes; model-level
 * syntax stays host agnostic and contains no trading/runtime concepts.
 *
 * Supported MVP statements:
 * - `name <identifier>`
 * - `name <identifier> { ...metadata... }`
 * - `use <identifier>`
 * - `in { a, b, c }`
 * - `in a, b, c`
 * - `$param = <expression>`
 * - `$param = <expression> { ...metadata... }`
 * - `<symbol> = <expression>`
 * - `<symbol> = <expression> { ...metadata... }`
 * - `state <symbol> = <initial-expression>`
 * - `out <symbol> = <expression-or-state-update>`
 * - `out <symbol>`
 * - `out <symbol> { ...metadata... }`
 * - `out a, b, c`
 * - `<host-block-kind> [name] { ...cxpr-style host-defined body... }`
 *
 * Metadata semantics are host/plugin-defined. The core parser stores the
 * metadata kind, target, and raw body, preserving newlines. Legacy
 * `meta { ... }` blocks and `@...` decorators are intentionally not supported.
 * Host blocks are stored as raw text after cxpr-level syntax screening; their
 * domain semantics are validated by the host.
 *
 * `state <symbol> = ...` declares the initial state value. `out <symbol> = ...`
 * updates an existing state symbol when `<symbol>` was declared with `state`;
 * otherwise it defines and publishes a model output expression. Use
 * `out <symbol>` or `out { ... }` to publish an existing symbol.
 *
 * Indented continuation lines are appended to the previous statement.
 *
 * @param source NUL-terminated .cxpr source text.
 * @param err Optional error output.
 * @return Parsed model on success, or NULL on parse/allocation failure.
 */
cxpr_model* cxpr_parse_model(const char* source, cxpr_error* err);

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
 * The compiled model reuses the existing AST->IR compiler for each binding and
 * stores dependency order once. It does not register model bindings in a
 * runtime evaluator, avoiding per-tick binding setup.
 */
cxpr_model_program* cxpr_compile_model(const cxpr_model* model,
                                       const cxpr_registry* reg,
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

size_t cxpr_model_program_binding_count(const cxpr_model_program* program);
const char* cxpr_model_program_binding_name(const cxpr_model_program* program, size_t index);
size_t cxpr_model_program_output_count(const cxpr_model_program* program);
const char* cxpr_model_program_output_name(const cxpr_model_program* program, size_t index);
/** @brief Return number of functions in the model-owned registry, for diagnostics/benchmarks. */
size_t cxpr_model_program_function_count(const cxpr_model_program* program);
/** @brief Return true when the compiled model uses one fused IR program for ticks. */
bool cxpr_model_program_uses_fused_ir(const cxpr_model_program* program);
/** @brief Return fused IR instruction count, or 0 when fused IR is not active. */
size_t cxpr_model_program_fused_ir_instruction_count(const cxpr_model_program* program);
/** @brief Return first opcode that disabled fused IR, or NULL when fused is active/not attempted. */
const char* cxpr_model_program_fused_ir_disabled_opcode(const cxpr_model_program* program);
/**
 * @brief Emit a `.cxpr` model-defined scalar `fn` as a standalone C function.
 *
 * This uses the compiled model's own registry and does not require host
 * registration of the function. Unsupported dynamic IR returns NULL with
 * @p err set, so callers can keep the normal IR path as fallback.
 */
char* cxpr_model_program_function_to_c_function(const cxpr_model_program* program,
                                                const char* name,
                                                const char* qualifiers,
                                                const char* return_type,
                                                const char* function_name,
                                                cxpr_error* err);
/**
 * @brief Emit a fused scalar model as a standalone C tick function.
 *
 * Generated ABI:
 * `void fn(double* slots, const double* inputs, const double* params, double* outputs)`.
 * Inputs and outputs use model declaration order. Params use model constant
 * order. `slots` is model-owned scratch/state storage with
 * `cxpr_model_program_c_slot_count(program)` entries and must be initialized by
 * the caller before the first tick.
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
 * emitted expression code does not load from it.
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
size_t cxpr_model_program_c_slot_count(const cxpr_model_program* program);
size_t cxpr_model_program_c_param_count(const cxpr_model_program* program);
const char* cxpr_model_program_c_param_name(const cxpr_model_program* program, size_t index);

/** @brief Create a mutable session for one compiled model. */
cxpr_model_session* cxpr_model_session_new(const cxpr_model_program* program,
                                           const cxpr_registry* reg,
                                           cxpr_error* err);
/** @brief Free a model session. */
void cxpr_model_session_free(cxpr_model_session* session);
/** @brief Return the session-owned context for host input writes and output reads. */
cxpr_context* cxpr_model_session_context(cxpr_model_session* session);
/**
 * @brief Evaluate one deterministic model tick and atomically commit state updates.
 *
 * Expressions in tick N read state from the start of that tick. State update
 * values are staged during evaluation and committed atomically after all
 * bindings are evaluated. Session outputs are refreshed from the post-commit
 * context.
 */
bool cxpr_model_session_tick(const cxpr_model_program* program,
                             cxpr_model_session* session,
                             const cxpr_registry* reg,
                             cxpr_error* err);
/**
 * @brief Evaluate one fused model tick without materializing state/outputs into the context.
 *
 * This is a hot-path API for hosts that write inputs through the session
 * context and read results through `cxpr_model_session_output_*`. It preserves
 * atomic state commits in fused slots, but skips per-tick context writes and
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

size_t cxpr_model_use_count(const cxpr_model* model);
const char* cxpr_model_use(const cxpr_model* model, size_t index);

size_t cxpr_model_input_count(const cxpr_model* model);
const char* cxpr_model_input(const cxpr_model* model, size_t index);

size_t cxpr_model_constant_count(const cxpr_model* model);
const char* cxpr_model_constant_name(const cxpr_model* model, size_t index);
const cxpr_ast* cxpr_model_constant_expr(const cxpr_model* model, size_t index);

size_t cxpr_model_binding_count(const cxpr_model* model);
cxpr_model_binding_kind cxpr_model_binding_kind_at(const cxpr_model* model, size_t index);
const char* cxpr_model_binding_name(const cxpr_model* model, size_t index);
const cxpr_ast* cxpr_model_binding_expr(const cxpr_model* model, size_t index);

size_t cxpr_model_output_count(const cxpr_model* model);
const char* cxpr_model_output(const cxpr_model* model, size_t index);

size_t cxpr_model_metadata_count(const cxpr_model* model);
const char* cxpr_model_metadata_name(const cxpr_model* model, size_t index);
const char* cxpr_model_metadata_body(const cxpr_model* model, size_t index);
cxpr_model_metadata_target_kind cxpr_model_metadata_target_kind_at(
    const cxpr_model* model,
    size_t index);
const char* cxpr_model_metadata_target_name(const cxpr_model* model, size_t index);

size_t cxpr_model_host_block_count(const cxpr_model* model);
const char* cxpr_model_host_block_kind(const cxpr_model* model, size_t index);
const char* cxpr_model_host_block_name(const cxpr_model* model, size_t index);
const char* cxpr_model_host_block_body(const cxpr_model* model, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_MODEL_H */
