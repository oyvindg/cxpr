/**
 * @file context.h
 * @brief Public context API for cxpr.
 */

#ifndef CXPR_CONTEXT_H
#define CXPR_CONTEXT_H

#include <cxpr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a new empty context.
 * @return Newly allocated context, or NULL on allocation failure.
 */
cxpr_context* cxpr_context_new(void);
/**
 * @brief Free a context.
 * @param ctx Context to free. May be NULL.
 */
void cxpr_context_free(cxpr_context* ctx);
/**
 * @brief Clone a context.
 * @param ctx Source context to copy.
 * @return Deep copy of `ctx`, or NULL on allocation failure.
 */
cxpr_context* cxpr_context_clone(const cxpr_context* ctx);
/**
 * @brief Create an overlay context with a parent fallback.
 * @param parent Parent context used for misses.
 * @return Newly allocated overlay context, or NULL on allocation failure.
 */
cxpr_context* cxpr_context_overlay_new(const cxpr_context* parent);

/**
 * @brief Set a numeric runtime variable.
 * @param ctx Destination context.
 * @param name Variable name.
 * @param value Numeric value to store.
 */
void cxpr_context_set(cxpr_context* ctx, const char* name, double value);
/** @brief One numeric runtime-variable assignment entry. */
typedef struct {
    const char* name;
    double value;
} cxpr_context_entry;
/**
 * @brief Set multiple numeric runtime variables from a NULL-terminated array.
 * @param ctx Destination context.
 * @param entries Array of `{ name, value }` pairs terminated by `{ NULL, 0 }`.
 */
void cxpr_context_set_array(cxpr_context* ctx, const cxpr_context_entry* entries);
/**
 * @brief Set a numeric runtime variable using a precomputed hash.
 * @param ctx Destination context.
 * @param name Variable name.
 * @param hash Hash previously computed from `name`.
 * @param value Numeric value to store.
 */
void cxpr_context_set_prehashed(cxpr_context* ctx, const char* name,
                                unsigned long hash, double value);
/**
 * @brief Look up a numeric runtime variable.
 * @param ctx Context to query.
 * @param name Variable name.
 * @param found Optional success flag output.
 * @return Variable value, or `0.0` on miss.
 */
double cxpr_context_get(const cxpr_context* ctx, const char* name, bool* found);

/**
 * @brief Set a numeric `$param`.
 * @param ctx Destination context.
 * @param name Parameter name without `$`.
 * @param value Numeric value to store.
 */
void cxpr_context_set_param(cxpr_context* ctx, const char* name, double value);
/**
 * @brief Set multiple numeric `$param`s from a NULL-terminated array.
 * @param ctx Destination context.
 * @param entries Array of `{ name, value }` pairs terminated by `{ NULL, 0 }`.
 */
void cxpr_context_set_param_array(cxpr_context* ctx, const cxpr_context_entry* entries);
/**
 * @brief Set a numeric `$param` using a precomputed hash.
 * @param ctx Destination context.
 * @param name Parameter name without `$`.
 * @param hash Hash previously computed from `name`.
 * @param value Numeric value to store.
 */
void cxpr_context_set_param_prehashed(cxpr_context* ctx, const char* name,
                                      unsigned long hash, double value);

/** @brief Pre-bound variable slot for hot-loop writes. */
typedef struct {
    double* _ptr;
    void* _base;
} cxpr_context_slot;

/**
 * @brief Bind a slot to an existing variable.
 * @param ctx Context that owns the variable.
 * @param name Variable name.
 * @param slot Output slot handle.
 * @return True on success, false if the variable was not found.
 */
bool cxpr_context_slot_bind(cxpr_context* ctx, const char* name, cxpr_context_slot* slot);
/**
 * @brief Check whether a previously bound slot is still valid.
 * @param ctx Context the slot belongs to.
 * @param slot Slot to validate.
 * @return True when the slot is still safe to use.
 */
bool cxpr_context_slot_valid(const cxpr_context* ctx, const cxpr_context_slot* slot);
/**
 * @brief Write a value through a bound slot.
 * @param slot Bound slot handle.
 * @param value Value to write.
 */
void cxpr_context_slot_set(cxpr_context_slot* slot, double value);
/**
 * @brief Read a value through a bound slot.
 * @param slot Bound slot handle.
 * @return Current numeric slot value.
 */
double cxpr_context_slot_get(const cxpr_context_slot* slot);

/**
 * @brief Look up a numeric `$param`.
 * @param ctx Context to query.
 * @param name Parameter name without `$`.
 * @param found Optional success flag output.
 * @return Parameter value, or `0.0` on miss.
 */
double cxpr_context_get_param(const cxpr_context* ctx, const char* name, bool* found);
/**
 * @brief Look up one binding as a typed cxpr value.
 * @param ctx Context to query.
 * @param name Binding name.
 * @param found Optional success flag output.
 * @return Number, bool, or struct value on hit; zero-like value on miss.
 */
cxpr_value cxpr_context_get_typed(const cxpr_context* ctx, const char* name, bool* found);
/**
 * @brief Clear all variables, params, structs, and caches from a context.
 * @param ctx Context to clear.
 */
void cxpr_context_clear(cxpr_context* ctx);
/**
 * @brief Clear cached producer structs while keeping explicit struct bindings.
 * @param ctx Context to clear cached structs from.
 */
void cxpr_context_clear_cached_structs(cxpr_context* ctx);
/**
 * @brief Store a cached producer struct result on a context.
 *
 * Cached structs are cleared by cxpr_context_clear_cached_structs() (which
 * the evaluator calls at the start of each evaluation pass).
 *
 * @param ctx Destination context.
 * @param name Cache key (typically "indicator(arg1,arg2,...)").
 * @param value Struct value to deep-copy into the cache.
 */
void cxpr_context_set_cached_struct(cxpr_context* ctx, const char* name,
                                    const cxpr_struct_value* value);
/**
 * @brief Look up a cached producer struct result from a context.
 * @param ctx Context to query.
 * @param name Cache key.
 * @return Borrowed struct pointer, or NULL on miss.
 */
const cxpr_struct_value* cxpr_context_get_cached_struct(const cxpr_context* ctx,
                                                        const char* name);

/**
 * @brief Store a named struct value in the context.
 * @param ctx Destination context.
 * @param name Binding name.
 * @param value Struct value to deep-copy.
 */
void cxpr_context_set_struct(cxpr_context* ctx, const char* name,
                             const cxpr_struct_value* value);
/**
 * @brief Look up a named struct binding.
 * @param ctx Context to query.
 * @param name Binding name.
 * @return Borrowed struct pointer, or NULL on miss.
 */
const cxpr_struct_value* cxpr_context_get_struct(const cxpr_context* ctx,
                                                 const char* name);
/**
 * @brief Look up one field from a named struct binding.
 * @param ctx Context to query.
 * @param name Struct binding name.
 * @param field Field name.
 * @param found Optional success flag output.
 * @return Borrowed typed field value, or zero-like value on miss.
 */
cxpr_value cxpr_context_get_field(const cxpr_context* ctx, const char* name,
                                  const char* field, bool* found);

/**
 * @brief Set multiple numeric fields under one prefix.
 * @param ctx Destination context.
 * @param prefix Shared key prefix.
 * @param fields Field-name array.
 * @param values Value array parallel to `fields`.
 * @param count Number of fields to write.
 */
void cxpr_context_set_fields(cxpr_context* ctx, const char* prefix,
                             const char* const* fields, const double* values,
                             size_t count);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_CONTEXT_H */
