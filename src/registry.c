/**
 * @file registry.c
 * @brief Function registry for cxpr.
 *
 * Manages registered synchronous functions with arity validation.
 * Used at build-time by codegen for expression validation.
 * Includes 30+ built-in math functions.
 */

#include "internal.h"
#include <math.h>

/* Local scalar helpers so cxpr stays standalone and does not depend on cxmath. */
static double cxpr_clamp(double x, double lo, double hi) {
    if (lo > hi) {
        const double t = lo;
        lo = hi;
        hi = t;
    }
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static double cxpr_sign(double x) {
    return (x > 0.0) ? 1.0 : ((x < 0.0) ? -1.0 : 0.0);
}

static double cxpr_min_n(const double* args, size_t argc) {
    if (!args || argc == 0) return 0.0;
    double out = args[0];
    for (size_t i = 1; i < argc; ++i) {
        if (args[i] < out) out = args[i];
    }
    return out;
}

static double cxpr_max_n(const double* args, size_t argc) {
    if (!args || argc == 0) return 0.0;
    double out = args[0];
    for (size_t i = 1; i < argc; ++i) {
        if (args[i] > out) out = args[i];
    }
    return out;
}

static double cxpr_lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

static double cxpr_smoothstep(double x, double e0, double e1) {
    if (e0 == e1) return (x >= e1) ? 1.0 : 0.0;
    const double t = cxpr_clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

static double cxpr_sigmoid(double x, double center, double steepness) {
    return 1.0 / (1.0 + exp(-steepness * (x - center)));
}

static double cxpr_pi(void) {
    return 3.14159265358979323846;
}

static double cxpr_e(void) {
    return 2.71828182845904523536;
}

static double cxpr_nan(void) {
    return NAN;
}

static double cxpr_inf(void) {
    return INFINITY;
}

static double cxpr_if(double cond, double a, double b) {
    return (cond != 0.0) ? a : b;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Registry internal helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

/** @brief Double registry capacity. Returns false on allocation failure. */
static bool cxpr_registry_grow(cxpr_registry* reg) {
    if (reg->capacity > SIZE_MAX / 2) return false;
    size_t new_capacity = reg->capacity * 2;
    cxpr_func_entry* new_entries = (cxpr_func_entry*)calloc(new_capacity, sizeof(cxpr_func_entry));
    if (!new_entries) return false;
    memcpy(new_entries, reg->entries, reg->count * sizeof(cxpr_func_entry));
    free(reg->entries);
    reg->entries = new_entries;
    reg->capacity = new_capacity;
    return true;
}

/**
 * @brief Find a function entry by name.
 * @param[in] reg Registry to search
 * @param[in] name Function name to look up
 * @return Pointer to function entry, or NULL if not found
 */
cxpr_func_entry* cxpr_registry_find(const cxpr_registry* reg, const char* name) {
    if (!reg || !name) return NULL;
    for (size_t i = 0; i < reg->count; i++) {
        if (reg->entries[i].name && strcmp(reg->entries[i].name, name) == 0) {
            return &((cxpr_registry*)reg)->entries[i];
        }
    }
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Registry API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Create a new empty registry.
 * @return New registry, or NULL on allocation failure
 */
cxpr_registry* cxpr_registry_new(void) {
    cxpr_registry* reg = (cxpr_registry*)calloc(1, sizeof(cxpr_registry));
    if (!reg) return NULL;
    reg->capacity = CXPR_REGISTRY_INITIAL_CAPACITY;
    reg->count = 0;
    reg->version = 1;
    reg->entries = (cxpr_func_entry*)calloc(reg->capacity, sizeof(cxpr_func_entry));
    if (!reg->entries) { free(reg); return NULL; }
    return reg;
}

/** @brief Free struct_fields from an entry (safe to call on non-struct entries). */
static void free_struct_fields(cxpr_func_entry* entry) {
    if (!entry->struct_fields) return;
    for (size_t f = 0; f < entry->fields_per_arg; f++) {
        free(entry->struct_fields[f]);
    }
    free(entry->struct_fields);
    entry->struct_fields = NULL;
    entry->fields_per_arg = 0;
    entry->struct_argc = 0;
}

static void free_arg_types(cxpr_func_entry* entry) {
    free(entry->arg_types);
    entry->arg_types = NULL;
    entry->arg_type_count = 0;
    entry->has_return_type = false;
    entry->return_type = CXPR_VALUE_NUMBER;
}

static cxpr_value_type* clone_arg_types(const cxpr_value_type* arg_types, size_t arg_count) {
    cxpr_value_type* out;
    if (!arg_types || arg_count == 0) return NULL;
    out = (cxpr_value_type*)malloc(sizeof(cxpr_value_type) * arg_count);
    if (!out) return NULL;
    memcpy(out, arg_types, sizeof(cxpr_value_type) * arg_count);
    return out;
}

/** @brief Free defined-function data from an entry (safe to call on C-function entries). */
static void free_defined_fn(cxpr_func_entry* entry) {
    if (!entry->defined_body) return;
    cxpr_program_free(entry->defined_program);
    entry->defined_program = NULL;
    entry->defined_program_failed = false;
    cxpr_ast_free(entry->defined_body);
    entry->defined_body = NULL;
    if (entry->defined_param_fields) {
        for (size_t i = 0; i < entry->defined_param_count; i++) {
            if (entry->defined_param_fields[i]) {
                for (size_t f = 0; f < entry->defined_param_field_counts[i]; f++) {
                    free(entry->defined_param_fields[i][f]);
                }
                free(entry->defined_param_fields[i]);
            }
        }
        free(entry->defined_param_fields);
        entry->defined_param_fields = NULL;
    }
    if (entry->defined_param_names) {
        for (size_t i = 0; i < entry->defined_param_count; i++) {
            free(entry->defined_param_names[i]);
        }
        free(entry->defined_param_names);
        entry->defined_param_names = NULL;
    }
    free(entry->defined_param_field_counts);
    entry->defined_param_field_counts = NULL;
    entry->defined_param_count = 0;
}

/**
 * @brief Free registry and all its entries.
 * @param[in] reg Registry to free (no-op if NULL)
 */
void cxpr_registry_free(cxpr_registry* reg) {
    if (!reg) return;
    if (reg->free_lookback_userdata && reg->lookback_userdata) {
        reg->free_lookback_userdata(reg->lookback_userdata);
    }
    for (size_t i = 0; i < reg->count; i++) {
        if (reg->entries[i].userdata_free) {
            reg->entries[i].userdata_free(reg->entries[i].userdata);
        }
        if (reg->entries[i].ast_func_overlay_userdata_free) {
            reg->entries[i].ast_func_overlay_userdata_free(
                reg->entries[i].ast_func_overlay_userdata);
        }
        free(reg->entries[i].name);
        free_struct_fields(&reg->entries[i]);
        free_arg_types(&reg->entries[i]);
        free_defined_fn(&reg->entries[i]);
    }
    free(reg->entries);
    free(reg);
}

void cxpr_registry_set_lookback_resolver(cxpr_registry* reg,
                                         cxpr_lookback_resolver_ptr resolver,
                                         void* userdata,
                                         cxpr_userdata_free_fn free_userdata) {
    if (!reg) return;
    if (reg->free_lookback_userdata && reg->lookback_userdata &&
        reg->lookback_userdata != userdata) {
        reg->free_lookback_userdata(reg->lookback_userdata);
    }
    reg->lookback_resolver = resolver;
    reg->lookback_userdata = userdata;
    reg->free_lookback_userdata = free_userdata;
}

/**
 * @brief Register a synchronous function with arity constraints.
 * @param[in] reg Registry to add to
 * @param[in] name Function name (overwrites if exists)
 * @param[in] func Sync function pointer
 * @param[in] min_args Minimum allowed arguments
 * @param[in] max_args Maximum allowed arguments
 * @param[in] userdata User data passed to function
 * @param[in] free_userdata Optional userdata cleanup callback
 */
void cxpr_registry_add(cxpr_registry* reg, const char* name,
                       cxpr_func_ptr func, size_t min_args, size_t max_args,
                       void* userdata, cxpr_userdata_free_fn free_userdata) {
    if (!reg || !name || !func) return;

    /* Overwrite if exists */
    cxpr_func_entry* existing = cxpr_registry_find(reg, name);
    if (existing) {
        if (existing->userdata_free) {
            existing->userdata_free(existing->userdata);
        }
        free_struct_fields(existing);
        free_defined_fn(existing);
        free_arg_types(existing);
        existing->sync_func = func;
        existing->value_func = NULL;
        existing->typed_func = NULL;
        existing->ast_func = NULL;
        existing->struct_producer = NULL;
        existing->native_kind = CXPR_NATIVE_KIND_NONE;
        memset(&existing->native_scalar, 0, sizeof(existing->native_scalar));
        existing->min_args = min_args;
        existing->max_args = max_args;
        existing->return_type = CXPR_VALUE_NUMBER;
        existing->has_return_type = true;
        existing->userdata = userdata;
        existing->userdata_free = free_userdata;
        reg->version++;
        return;
    }

    if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) return;
    cxpr_func_entry* entry = &reg->entries[reg->count++];
    entry->name = cxpr_strdup(name);
    entry->sync_func = func;
    entry->value_func = NULL;
    entry->typed_func = NULL;
    entry->ast_func = NULL;
    entry->struct_producer = NULL;
    entry->native_kind = CXPR_NATIVE_KIND_NONE;
    memset(&entry->native_scalar, 0, sizeof(entry->native_scalar));
    entry->min_args = min_args;
    entry->max_args = max_args;
    entry->arg_types = NULL;
    entry->arg_type_count = 0;
    entry->return_type = CXPR_VALUE_NUMBER;
    entry->has_return_type = true;
    entry->userdata = userdata;
    entry->userdata_free = free_userdata;
    reg->version++;
}

void cxpr_registry_add_value(cxpr_registry* reg, const char* name,
                             cxpr_value_func_ptr func, size_t min_args, size_t max_args,
                             void* userdata, cxpr_userdata_free_fn free_userdata) {
    if (!reg || !name || !func) return;

    cxpr_func_entry* existing = cxpr_registry_find(reg, name);
    if (existing) {
        if (existing->userdata_free) {
            existing->userdata_free(existing->userdata);
        }
        free_struct_fields(existing);
        free_defined_fn(existing);
        free_arg_types(existing);
        existing->sync_func = NULL;
        existing->value_func = func;
        existing->typed_func = NULL;
        existing->ast_func = NULL;
        existing->struct_producer = NULL;
        existing->native_kind = CXPR_NATIVE_KIND_NONE;
        memset(&existing->native_scalar, 0, sizeof(existing->native_scalar));
        existing->min_args = min_args;
        existing->max_args = max_args;
        existing->userdata = userdata;
        existing->userdata_free = free_userdata;
        reg->version++;
        return;
    }

    if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) return;
    cxpr_func_entry* entry = &reg->entries[reg->count++];
    entry->name = cxpr_strdup(name);
    entry->sync_func = NULL;
    entry->value_func = func;
    entry->typed_func = NULL;
    entry->ast_func = NULL;
    entry->struct_producer = NULL;
    entry->native_kind = CXPR_NATIVE_KIND_NONE;
    memset(&entry->native_scalar, 0, sizeof(entry->native_scalar));
    entry->min_args = min_args;
    entry->max_args = max_args;
    entry->arg_types = NULL;
    entry->arg_type_count = 0;
    entry->return_type = CXPR_VALUE_NUMBER;
    entry->has_return_type = false;
    entry->userdata = userdata;
    entry->userdata_free = free_userdata;
    reg->version++;
}

void cxpr_registry_add_typed(cxpr_registry* reg, const char* name,
                             cxpr_typed_func_ptr func, size_t min_args, size_t max_args,
                             const cxpr_value_type* arg_types, cxpr_value_type return_type,
                             void* userdata, cxpr_userdata_free_fn free_userdata) {
    cxpr_value_type* owned_arg_types;
    if (!reg || !name || !func) return;
    owned_arg_types = clone_arg_types(arg_types, max_args);
    if (arg_types && max_args > 0 && !owned_arg_types) return;

    cxpr_func_entry* existing = cxpr_registry_find(reg, name);
    if (existing) {
        if (existing->userdata_free) {
            existing->userdata_free(existing->userdata);
        }
        free_struct_fields(existing);
        free_arg_types(existing);
        free_defined_fn(existing);
        existing->sync_func = NULL;
        existing->value_func = NULL;
        existing->typed_func = func;
        existing->ast_func = NULL;
        existing->struct_producer = NULL;
        existing->native_kind = CXPR_NATIVE_KIND_NONE;
        memset(&existing->native_scalar, 0, sizeof(existing->native_scalar));
        existing->min_args = min_args;
        existing->max_args = max_args;
        existing->arg_types = owned_arg_types;
        existing->arg_type_count = max_args;
        existing->return_type = return_type;
        existing->has_return_type = true;
        existing->userdata = userdata;
        existing->userdata_free = free_userdata;
        reg->version++;
        return;
    }

    if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) return;
    cxpr_func_entry* entry = &reg->entries[reg->count++];
    entry->name = cxpr_strdup(name);
    entry->sync_func = NULL;
    entry->value_func = NULL;
    entry->typed_func = func;
    entry->ast_func = NULL;
    entry->struct_producer = NULL;
    entry->native_kind = CXPR_NATIVE_KIND_NONE;
    memset(&entry->native_scalar, 0, sizeof(entry->native_scalar));
    entry->min_args = min_args;
    entry->max_args = max_args;
    entry->arg_types = owned_arg_types;
    entry->arg_type_count = max_args;
    entry->return_type = return_type;
    entry->has_return_type = true;
    entry->userdata = userdata;
    entry->userdata_free = free_userdata;
    reg->version++;
}

void cxpr_registry_add_ast(cxpr_registry* reg, const char* name,
                           cxpr_ast_func_ptr func, size_t min_args, size_t max_args,
                           cxpr_value_type return_type,
                           void* userdata, cxpr_userdata_free_fn free_userdata) {
    if (!reg || !name || !func) return;

    cxpr_func_entry* existing = cxpr_registry_find(reg, name);
    if (existing) {
        if (existing->userdata_free) {
            existing->userdata_free(existing->userdata);
        }
        if (existing->ast_func_overlay_userdata_free) {
            existing->ast_func_overlay_userdata_free(existing->ast_func_overlay_userdata);
        }
        free_struct_fields(existing);
        free_arg_types(existing);
        free_defined_fn(existing);
        existing->sync_func = NULL;
        existing->value_func = NULL;
        existing->typed_func = NULL;
        existing->ast_func = func;
        existing->ast_func_overlay = NULL;
        existing->ast_func_overlay_userdata = NULL;
        existing->ast_func_overlay_userdata_free = NULL;
        existing->struct_producer = NULL;
        existing->native_kind = CXPR_NATIVE_KIND_NONE;
        memset(&existing->native_scalar, 0, sizeof(existing->native_scalar));
        existing->min_args = min_args;
        existing->max_args = max_args;
        existing->return_type = return_type;
        existing->has_return_type = true;
        existing->userdata = userdata;
        existing->userdata_free = free_userdata;
        reg->version++;
        return;
    }

    if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) return;
    cxpr_func_entry* entry = &reg->entries[reg->count++];
    entry->name = cxpr_strdup(name);
    entry->sync_func = NULL;
    entry->value_func = NULL;
    entry->typed_func = NULL;
    entry->ast_func = func;
    entry->ast_func_overlay = NULL;
    entry->ast_func_overlay_userdata = NULL;
    entry->ast_func_overlay_userdata_free = NULL;
    entry->struct_producer = NULL;
    entry->native_kind = CXPR_NATIVE_KIND_NONE;
    memset(&entry->native_scalar, 0, sizeof(entry->native_scalar));
    entry->min_args = min_args;
    entry->max_args = max_args;
    entry->arg_types = NULL;
    entry->arg_type_count = 0;
    entry->return_type = return_type;
    entry->has_return_type = true;
    entry->userdata = userdata;
    entry->userdata_free = free_userdata;
    reg->version++;
}

void cxpr_registry_add_ast_overlay(cxpr_registry* reg, const char* name,
                                    cxpr_ast_func_ptr func,
                                    size_t min_args, size_t max_args,
                                    void* userdata,
                                    cxpr_userdata_free_fn free_userdata) {
    if (!reg || !name || !func) return;

    cxpr_func_entry* entry = cxpr_registry_find(reg, name);
    if (entry) {
        /* Free existing overlay if any */
        if (entry->ast_func_overlay_userdata_free) {
            entry->ast_func_overlay_userdata_free(entry->ast_func_overlay_userdata);
        }
        entry->ast_func_overlay = func;
        entry->ast_func_overlay_userdata = userdata;
        entry->ast_func_overlay_userdata_free = free_userdata;
        /* Extend arity range to accommodate both base and overlay call forms */
        if (min_args < entry->min_args) entry->min_args = min_args;
        if (max_args > entry->max_args) entry->max_args = max_args;
        reg->version++;
        return;
    }

    /* No existing entry — create one with only the overlay (no sync/struct) */
    if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) return;
    entry = &reg->entries[reg->count++];
    entry->name = cxpr_strdup(name);
    entry->sync_func = NULL;
    entry->value_func = NULL;
    entry->typed_func = NULL;
    entry->ast_func = NULL;
    entry->ast_func_overlay = func;
    entry->ast_func_overlay_userdata = userdata;
    entry->ast_func_overlay_userdata_free = free_userdata;
    entry->struct_producer = NULL;
    entry->native_kind = CXPR_NATIVE_KIND_NONE;
    memset(&entry->native_scalar, 0, sizeof(entry->native_scalar));
    entry->min_args = min_args;
    entry->max_args = max_args;
    entry->arg_types = NULL;
    entry->arg_type_count = 0;
    entry->return_type = CXPR_VALUE_NUMBER;
    entry->has_return_type = false;
    entry->userdata = NULL;
    entry->userdata_free = NULL;
    entry->struct_fields = NULL;
    entry->fields_per_arg = 0;
    entry->struct_argc = 0;
    entry->defined_body = NULL;
    entry->defined_program = NULL;
    entry->defined_program_failed = false;
    entry->defined_param_names = NULL;
    entry->defined_param_count = 0;
    entry->defined_param_fields = NULL;
    entry->defined_param_field_counts = NULL;
    reg->version++;
}

void cxpr_registry_add_timeseries(cxpr_registry* reg, const char* name,
                                  cxpr_timeseries_func_ptr func,
                                  size_t min_args, size_t max_args,
                                  cxpr_value_type return_type,
                                  void* userdata,
                                  cxpr_userdata_free_fn free_userdata) {
    cxpr_registry_add_ast(
        reg,
        name,
        (cxpr_ast_func_ptr)func,
        min_args,
        max_args,
        return_type,
        userdata,
        free_userdata);
}

/** @brief Heap payload for unary adapters. */
typedef struct {
    double (*fn)(double);
} cxpr_unary_userdata;

/** @brief Heap payload for binary adapters. */
typedef struct {
    double (*fn)(double, double);
} cxpr_binary_userdata;

/** @brief Heap payload for nullary adapters. */
typedef struct {
    double (*fn)(void);
} cxpr_nullary_userdata;

/** @brief Heap payload for ternary adapters. */
typedef struct {
    double (*fn)(double, double, double);
} cxpr_ternary_userdata;

/** @brief Adapter: cxpr ABI -> unary scalar function. */
static double cxpr_unary_adapter(const double* args, size_t argc, void* userdata) {
    (void)argc;
    const cxpr_unary_userdata* ud = (const cxpr_unary_userdata*)userdata;
    return ud->fn(args[0]);
}

/** @brief Adapter: cxpr ABI -> binary scalar function. */
static double cxpr_binary_adapter(const double* args, size_t argc, void* userdata) {
    (void)argc;
    const cxpr_binary_userdata* ud = (const cxpr_binary_userdata*)userdata;
    return ud->fn(args[0], args[1]);
}

/** @brief Adapter: cxpr ABI -> nullary scalar function. */
static double cxpr_nullary_adapter(const double* args, size_t argc, void* userdata) {
    (void)args;
    (void)argc;
    const cxpr_nullary_userdata* ud = (const cxpr_nullary_userdata*)userdata;
    return ud->fn();
}

/** @brief Adapter: cxpr ABI -> ternary scalar function. */
static double cxpr_ternary_adapter(const double* args, size_t argc, void* userdata) {
    (void)argc;
    const cxpr_ternary_userdata* ud = (const cxpr_ternary_userdata*)userdata;
    return ud->fn(args[0], args[1], args[2]);
}

/** @brief Variadic min over [1,8] args. */
static double cxpr_min(const double* args, size_t argc, void* userdata) {
    (void)userdata;
    return cxpr_min_n(args, argc);
}

/** @brief Variadic max over [1,8] args. */
static double cxpr_max(const double* args, size_t argc, void* userdata) {
    (void)userdata;
    return cxpr_max_n(args, argc);
}

/** @brief Register a unary scalar function using an internal adapter. */
void cxpr_registry_add_unary(cxpr_registry* reg, const char* name,
                             double (*func)(double)) {
    cxpr_func_entry* entry;
    if (!reg || !name || !func) return;
    cxpr_unary_userdata* ud = (cxpr_unary_userdata*)malloc(sizeof(cxpr_unary_userdata));
    if (!ud) return;
    ud->fn = func;
    cxpr_registry_add(reg, name, cxpr_unary_adapter, 1, 1, ud, free);
    entry = cxpr_registry_find(reg, name);
    if (entry) {
        entry->native_kind = CXPR_NATIVE_KIND_UNARY;
        entry->native_scalar.unary = func;
    }
}

/** @brief Register a binary scalar function using an internal adapter. */
void cxpr_registry_add_binary(cxpr_registry* reg, const char* name,
                              double (*func)(double, double)) {
    cxpr_func_entry* entry;
    if (!reg || !name || !func) return;
    cxpr_binary_userdata* ud = (cxpr_binary_userdata*)malloc(sizeof(cxpr_binary_userdata));
    if (!ud) return;
    ud->fn = func;
    cxpr_registry_add(reg, name, cxpr_binary_adapter, 2, 2, ud, free);
    entry = cxpr_registry_find(reg, name);
    if (entry) {
        entry->native_kind = CXPR_NATIVE_KIND_BINARY;
        entry->native_scalar.binary = func;
    }
}

/** @brief Register a nullary scalar function using an internal adapter. */
void cxpr_registry_add_nullary(cxpr_registry* reg, const char* name,
                               double (*func)(void)) {
    cxpr_func_entry* entry;
    if (!reg || !name || !func) return;
    cxpr_nullary_userdata* ud = (cxpr_nullary_userdata*)malloc(sizeof(cxpr_nullary_userdata));
    if (!ud) return;
    ud->fn = func;
    cxpr_registry_add(reg, name, cxpr_nullary_adapter, 0, 0, ud, free);
    entry = cxpr_registry_find(reg, name);
    if (entry) {
        entry->native_kind = CXPR_NATIVE_KIND_NULLARY;
        entry->native_scalar.nullary = func;
    }
}

/** @brief Register a ternary scalar function using an internal adapter. */
void cxpr_registry_add_ternary(cxpr_registry* reg, const char* name,
                               double (*func)(double, double, double)) {
    cxpr_func_entry* entry;
    if (!reg || !name || !func) return;
    cxpr_ternary_userdata* ud = (cxpr_ternary_userdata*)malloc(sizeof(cxpr_ternary_userdata));
    if (!ud) return;
    ud->fn = func;
    cxpr_registry_add(reg, name, cxpr_ternary_adapter, 3, 3, ud, free);
    entry = cxpr_registry_find(reg, name);
    if (entry) {
        entry->native_kind = CXPR_NATIVE_KIND_TERNARY;
        entry->native_scalar.ternary = func;
    }
}

/**
 * @brief Look up a function's arity constraints by name.
 * @param[in] reg Registry to query
 * @param[in] name Function name to look up
 * @param[out] min_args Receives minimum args (may be NULL)
 * @param[out] max_args Receives maximum args (may be NULL)
 * @return True if function found, false otherwise
 */
bool cxpr_registry_lookup(const cxpr_registry* reg, const char* name,
                        size_t* min_args, size_t* max_args) {
    cxpr_func_entry* entry = cxpr_registry_find(reg, name);
    if (!entry) return false;
    if (min_args) *min_args = entry->min_args;
    if (max_args) *max_args = entry->max_args;
    return true;
}

/**
 * @brief Register a struct-aware function that expands identifier arguments
 *        into their component fields before calling the function.
 */
void cxpr_registry_add_fn(cxpr_registry* reg, const char* name,
                                  cxpr_func_ptr func,
                                  const char* const* fields, size_t fields_per_arg,
                                  size_t struct_argc,
                                  void* userdata, cxpr_userdata_free_fn free_userdata) {
    if (!reg || !name || !func || !fields || fields_per_arg == 0 || struct_argc == 0) return;

    /* Copy field names */
    char** owned_fields = (char**)calloc(fields_per_arg, sizeof(char*));
    if (!owned_fields) return;
    for (size_t f = 0; f < fields_per_arg; f++) {
        owned_fields[f] = cxpr_strdup(fields[f]);
        if (!owned_fields[f]) {
            for (size_t k = 0; k < f; k++) free(owned_fields[k]);
            free(owned_fields);
            return;
        }
    }

    /* Overwrite if exists */
    cxpr_func_entry* existing = cxpr_registry_find(reg, name);
    if (existing) {
        if (existing->userdata_free) existing->userdata_free(existing->userdata);
        free_struct_fields(existing);
        free_defined_fn(existing);
        free_arg_types(existing);
        existing->sync_func = func;
        existing->value_func = NULL;
        existing->typed_func = NULL;
        existing->ast_func = NULL;
        existing->struct_producer = NULL;
        existing->native_kind = CXPR_NATIVE_KIND_NONE;
        memset(&existing->native_scalar, 0, sizeof(existing->native_scalar));
        existing->min_args = struct_argc;
        existing->max_args = struct_argc;
        existing->arg_types = NULL;
        existing->arg_type_count = 0;
        existing->return_type = CXPR_VALUE_NUMBER;
        existing->has_return_type = true;
        existing->userdata = userdata;
        existing->userdata_free = free_userdata;
        existing->struct_fields = owned_fields;
        existing->fields_per_arg = fields_per_arg;
        existing->struct_argc = struct_argc;
        reg->version++;
        return;
    }

    if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) return;
    cxpr_func_entry* entry = &reg->entries[reg->count++];
    entry->name = cxpr_strdup(name);
    entry->sync_func = func;
    entry->value_func = NULL;
    entry->typed_func = NULL;
    entry->ast_func = NULL;
    entry->struct_producer = NULL;
    entry->native_kind = CXPR_NATIVE_KIND_NONE;
    memset(&entry->native_scalar, 0, sizeof(entry->native_scalar));
    entry->min_args = struct_argc;
    entry->max_args = struct_argc;
    entry->arg_types = NULL;
    entry->arg_type_count = 0;
    entry->return_type = CXPR_VALUE_NUMBER;
    entry->has_return_type = true;
    entry->userdata = userdata;
    entry->userdata_free = free_userdata;
    entry->struct_fields = owned_fields;
    entry->fields_per_arg = fields_per_arg;
    entry->struct_argc = struct_argc;
    reg->version++;
}

void cxpr_registry_add_struct(cxpr_registry* reg, const char* name,
                                       cxpr_struct_producer_ptr func,
                                       size_t min_args, size_t max_args,
                                       const char* const* fields, size_t field_count,
                                       void* userdata,
                                       cxpr_userdata_free_fn free_userdata) {
    char** owned_fields;
    cxpr_func_entry* entry;

    if (!reg || !name || !func || !fields || field_count == 0) return;

    owned_fields = (char**)calloc(field_count, sizeof(char*));
    if (!owned_fields) return;
    for (size_t i = 0; i < field_count; i++) {
        owned_fields[i] = cxpr_strdup(fields[i]);
        if (!owned_fields[i]) {
            for (size_t j = 0; j < i; j++) free(owned_fields[j]);
            free(owned_fields);
            return;
        }
    }

    entry = cxpr_registry_find(reg, name);
    if (entry) {
        if (entry->userdata_free) entry->userdata_free(entry->userdata);
        free_struct_fields(entry);
        free_arg_types(entry);
        free_defined_fn(entry);
        /* Preserve any existing scalar callback so one entry can serve both
         * plain `name(...)` and `name(...).field`. */
        entry->struct_producer = func;
        entry->ast_func = NULL;
        entry->native_kind = CXPR_NATIVE_KIND_NONE;
        memset(&entry->native_scalar, 0, sizeof(entry->native_scalar));
        entry->min_args = min_args;
        entry->max_args = max_args;
        entry->arg_types = NULL;
        entry->arg_type_count = 0;
        entry->return_type = CXPR_VALUE_STRUCT;
        entry->has_return_type = true;
        entry->userdata = userdata;
        entry->userdata_free = free_userdata;
        entry->struct_fields = owned_fields;
        entry->fields_per_arg = field_count;
        entry->struct_argc = 0;
        reg->version++;
        return;
    }

    if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) return;
    entry = &reg->entries[reg->count++];
    entry->name = cxpr_strdup(name);
    entry->sync_func = NULL;
    entry->value_func = NULL;
    entry->typed_func = NULL;
    entry->ast_func = NULL;
    entry->struct_producer = func;
    entry->native_kind = CXPR_NATIVE_KIND_NONE;
    memset(&entry->native_scalar, 0, sizeof(entry->native_scalar));
    entry->min_args = min_args;
    entry->max_args = max_args;
    entry->arg_types = NULL;
    entry->arg_type_count = 0;
    entry->return_type = CXPR_VALUE_STRUCT;
    entry->has_return_type = true;
    entry->userdata = userdata;
    entry->userdata_free = free_userdata;
    entry->struct_fields = owned_fields;
    entry->fields_per_arg = field_count;
    entry->struct_argc = 0;
    reg->version++;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Register all built-in math functions
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Register all built-in math functions into the registry.
 * @param[in] reg Registry to populate (no-op if NULL)
 */
void cxpr_register_builtins(cxpr_registry* reg) {
    if (!reg) return;

    /* min/max: arity 1..8. argc=1 allows unary min/max; argc>=2 is the
     * standard variadic math form. */
    cxpr_registry_add(reg, "min", cxpr_min, 1, 8, NULL, NULL);
    cxpr_registry_add(reg, "max", cxpr_max, 1, 8, NULL, NULL);
    cxpr_registry_add_ternary(reg, "clamp", cxpr_clamp);
    cxpr_registry_add_unary(reg, "sign", cxpr_sign);
    cxpr_registry_add_ternary(reg, "lerp", cxpr_lerp);
    cxpr_registry_add_ternary(reg, "smoothstep", cxpr_smoothstep);
    cxpr_registry_add_ternary(reg, "sigmoid", cxpr_sigmoid);
    
    cxpr_registry_add_unary(reg, "abs", fabs);
    cxpr_registry_add_unary(reg, "floor", floor);
    cxpr_registry_add_unary(reg, "ceil", ceil);
    cxpr_registry_add_unary(reg, "round", round);
    cxpr_registry_add_unary(reg, "trunc", trunc);

    cxpr_registry_add_unary(reg, "sqrt", sqrt);
    cxpr_registry_add_unary(reg, "cbrt", cbrt);
    cxpr_registry_add_binary(reg, "pow", pow);
    cxpr_registry_add_unary(reg, "exp", exp);
    cxpr_registry_add_unary(reg, "exp2", exp2);

    cxpr_registry_add_unary(reg, "log", log);
    cxpr_registry_add_unary(reg, "log10", log10);
    cxpr_registry_add_unary(reg, "log2", log2);

    cxpr_registry_add_unary(reg, "sin", sin);
    cxpr_registry_add_unary(reg, "cos", cos);
    cxpr_registry_add_unary(reg, "tan", tan);
    cxpr_registry_add_unary(reg, "asin", asin);
    cxpr_registry_add_unary(reg, "acos", acos);
    cxpr_registry_add_unary(reg, "atan", atan);
    cxpr_registry_add_binary(reg, "atan2", atan2);

    cxpr_registry_add_unary(reg, "sinh", sinh);
    cxpr_registry_add_unary(reg, "cosh", cosh);
    cxpr_registry_add_unary(reg, "tanh", tanh);

    cxpr_registry_add_nullary(reg, "pi", cxpr_pi);
    cxpr_registry_add_nullary(reg, "e", cxpr_e);
    cxpr_registry_add_nullary(reg, "nan", cxpr_nan);
    cxpr_registry_add_nullary(reg, "inf", cxpr_inf);

    cxpr_registry_add_ternary(reg, "if", cxpr_if);
    cxpr_register_timeseries_builtins(reg);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * cxpr_registry_define_fn — expression-based function definitions
 * ═══════════════════════════════════════════════════════════════════════════ */

#define CXPR_DEF_MAX_PARAMS 16
#define CXPR_DEF_MAX_FIELDS 32

/** @brief Per-parameter inferred field set (temporary, used only during define). */
typedef struct {
    const char* fields[CXPR_DEF_MAX_FIELDS]; /**< Borrowed pointers into body AST strings */
    size_t count;
} cxpr_def_field_set;

static void cxpr_def_field_set_add(cxpr_def_field_set* set, const char* field) {
    bool dup = false;

    if (!set || !field) return;

    for (size_t i = 0; i < set->count; i++) {
        if (strcmp(set->fields[i], field) == 0) {
            dup = true;
            break;
        }
    }

    if (!dup && set->count < CXPR_DEF_MAX_FIELDS) {
        set->fields[set->count++] = field;
    }
}

/** @brief Walk body AST collecting param.field accesses for each parameter. */
static void collect_fields_in_ast(
    const cxpr_ast* node,
    const char* const* param_names, size_t param_count,
    cxpr_def_field_set* sets
) {
    if (!node) return;

    if (node->type == CXPR_NODE_FIELD_ACCESS) {
        const char* obj = node->data.field_access.object;
        const char* fld = node->data.field_access.field;
        for (size_t i = 0; i < param_count; i++) {
            if (strcmp(obj, param_names[i]) != 0) continue;
            cxpr_def_field_set_add(&sets[i], fld);
            break;
        }
        return;
    }

    switch (node->type) {
    case CXPR_NODE_BINARY_OP:
        collect_fields_in_ast(node->data.binary_op.left,  param_names, param_count, sets);
        collect_fields_in_ast(node->data.binary_op.right, param_names, param_count, sets);
        break;
    case CXPR_NODE_UNARY_OP:
        collect_fields_in_ast(node->data.unary_op.operand, param_names, param_count, sets);
        break;
    case CXPR_NODE_LOOKBACK:
        collect_fields_in_ast(node->data.lookback.target, param_names, param_count, sets);
        collect_fields_in_ast(node->data.lookback.index, param_names, param_count, sets);
        break;
    case CXPR_NODE_FUNCTION_CALL:
        for (size_t i = 0; i < node->data.function_call.argc; i++) {
            collect_fields_in_ast(node->data.function_call.args[i], param_names, param_count, sets);
        }
        break;
    case CXPR_NODE_TERNARY:
        collect_fields_in_ast(node->data.ternary.condition,    param_names, param_count, sets);
        collect_fields_in_ast(node->data.ternary.true_branch,  param_names, param_count, sets);
        collect_fields_in_ast(node->data.ternary.false_branch, param_names, param_count, sets);
        break;
    default:
        break;
    }
}

static void collect_transitive_fields_in_ast(const cxpr_ast* node, const cxpr_registry* reg,
                                             const char* const* param_names, size_t param_count,
                                             cxpr_def_field_set* sets) {
    if (!node || !reg) return;

    switch (node->type) {
    case CXPR_NODE_FUNCTION_CALL: {
        cxpr_func_entry* entry = cxpr_registry_find(reg, node->data.function_call.name);
        if (entry && entry->defined_param_fields &&
            entry->defined_param_count == node->data.function_call.argc) {
            for (size_t arg_index = 0; arg_index < node->data.function_call.argc; arg_index++) {
                const cxpr_ast* arg = node->data.function_call.args[arg_index];

                if (arg->type != CXPR_NODE_IDENTIFIER) continue;

                for (size_t param_index = 0; param_index < param_count; param_index++) {
                    if (strcmp(arg->data.identifier.name, param_names[param_index]) != 0) continue;

                    for (size_t field_index = 0;
                         field_index < entry->defined_param_field_counts[arg_index];
                         field_index++) {
                        cxpr_def_field_set_add(&sets[param_index],
                                               entry->defined_param_fields[arg_index][field_index]);
                    }
                    break;
                }
            }
        }

        for (size_t i = 0; i < node->data.function_call.argc; i++) {
            collect_transitive_fields_in_ast(node->data.function_call.args[i], reg,
                                             param_names, param_count, sets);
        }
        break;
    }
    case CXPR_NODE_BINARY_OP:
        collect_transitive_fields_in_ast(node->data.binary_op.left, reg,
                                         param_names, param_count, sets);
        collect_transitive_fields_in_ast(node->data.binary_op.right, reg,
                                         param_names, param_count, sets);
        break;
    case CXPR_NODE_UNARY_OP:
        collect_transitive_fields_in_ast(node->data.unary_op.operand, reg,
                                         param_names, param_count, sets);
        break;
    case CXPR_NODE_LOOKBACK:
        collect_transitive_fields_in_ast(node->data.lookback.target, reg,
                                         param_names, param_count, sets);
        collect_transitive_fields_in_ast(node->data.lookback.index, reg,
                                         param_names, param_count, sets);
        break;
    case CXPR_NODE_TERNARY:
        collect_transitive_fields_in_ast(node->data.ternary.condition, reg,
                                         param_names, param_count, sets);
        collect_transitive_fields_in_ast(node->data.ternary.true_branch, reg,
                                         param_names, param_count, sets);
        collect_transitive_fields_in_ast(node->data.ternary.false_branch, reg,
                                         param_names, param_count, sets);
        break;
    default:
        break;
    }
}

/** @brief Helper: return true if c is a valid identifier character. */
static bool is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/**
 * @brief Register an expression-based function from a definition string.
 *
 * Format: `name(param1, param2, ...) => body_expression`
 *
 * Parameters that appear as `param.field` in the body are treated as struct
 * arguments — the caller passes an identifier and the fields are read from
 * the context.  Parameters with no dot-access are treated as plain scalars.
 *
 * Examples:
 * @code
 * cxpr_registry_define_fn(reg,
 *     "distance3(goal, pose) => "
 *     "sqrt((goal.x-pose.x)^2 + (goal.y-pose.y)^2 + (goal.z-pose.z)^2)");
 * cxpr_registry_define_fn(reg, "sum(a, b) => a + b");
 * cxpr_registry_define_fn(reg, "dot2(u, v) => u.x*v.x + u.y*v.y");
 * cxpr_registry_define_fn(reg, "clamp_val(p) => clamp(p.value, $min, $max)");
 * @endcode
 *
 * @param reg  Registry to add to
 * @param def  Null-terminated definition string
 * @return     cxpr_error with code CXPR_OK on success, or an error code
 */
cxpr_error cxpr_registry_define_fn(cxpr_registry* reg, const char* def) {
    cxpr_error err = {0};
    if (!reg || !def) {
        err.code = CXPR_ERR_SYNTAX;
        err.message = "NULL registry or definition";
        return err;
    }

    const char* p = def;
    while (*p == ' ' || *p == '\t') p++;

    /* --- Parse function name --- */
    const char* name_start = p;
    while (is_ident_char(*p)) p++;
    size_t name_len = (size_t)(p - name_start);
    if (name_len == 0) {
        err.code = CXPR_ERR_SYNTAX; err.message = "Expected function name";
        return err;
    }
    char fname[256];
    if (name_len >= sizeof(fname)) {
        err.code = CXPR_ERR_SYNTAX; err.message = "Function name too long";
        return err;
    }
    memcpy(fname, name_start, name_len);
    fname[name_len] = '\0';

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '(') {
        err.code = CXPR_ERR_SYNTAX; err.message = "Expected '(' after function name";
        return err;
    }
    p++;

    /* --- Parse parameter list --- */
    char param_buf[CXPR_DEF_MAX_PARAMS][64];
    size_t param_count = 0;

    while (true) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ')') { p++; break; }
        if (param_count > 0) {
            if (*p != ',') {
                err.code = CXPR_ERR_SYNTAX; err.message = "Expected ',' or ')' in parameter list";
                return err;
            }
            p++;
            while (*p == ' ' || *p == '\t') p++;
        }
        const char* pstart = p;
        while (is_ident_char(*p)) p++;
        size_t plen = (size_t)(p - pstart);
        if (plen == 0) {
            err.code = CXPR_ERR_SYNTAX; err.message = "Expected parameter name";
            return err;
        }
        if (param_count >= CXPR_DEF_MAX_PARAMS) {
            err.code = CXPR_ERR_SYNTAX; err.message = "Too many parameters (max 16)";
            return err;
        }
        if (plen >= 64) {
            err.code = CXPR_ERR_SYNTAX; err.message = "Parameter name too long";
            return err;
        }
        memcpy(param_buf[param_count], pstart, plen);
        param_buf[param_count][plen] = '\0';
        param_count++;
    }

    /* --- Expect '=>' --- */
    while (*p == ' ' || *p == '\t') p++;
    if (p[0] != '=' || p[1] != '>') {
        err.code = CXPR_ERR_SYNTAX; err.message = "Expected '=>'";
        return err;
    }
    p += 2;
    while (*p == ' ' || *p == '\t') p++;

    if (!*p) {
        err.code = CXPR_ERR_SYNTAX; err.message = "Empty function body";
        return err;
    }

    /* --- Parse body expression --- */
    cxpr_parser* parser = cxpr_parser_new();
    if (!parser) {
        err.code = CXPR_ERR_OUT_OF_MEMORY; err.message = "Out of memory";
        return err;
    }
    cxpr_ast* body_ast = cxpr_parse(parser, p, &err);
    cxpr_parser_free(parser);
    if (!body_ast) return err; /* err already set by cxpr_parse */

    /* --- Infer per-parameter field lists from body AST --- */
    const char* pnames[CXPR_DEF_MAX_PARAMS];
    for (size_t i = 0; i < param_count; i++) pnames[i] = param_buf[i];

    cxpr_def_field_set sets[CXPR_DEF_MAX_PARAMS];
    memset(sets, 0, sizeof(sets));
    collect_fields_in_ast(body_ast, pnames, param_count, sets);
    collect_transitive_fields_in_ast(body_ast, reg, pnames, param_count, sets);

    /* --- Allocate owned structures --- */
    char** owned_names   = (char**)calloc(param_count ? param_count : 1, sizeof(char*));
    char*** owned_fields  = (char***)calloc(param_count ? param_count : 1, sizeof(char**));
    size_t* owned_counts  = (size_t*)calloc(param_count ? param_count : 1, sizeof(size_t));
    if (!owned_names || !owned_fields || !owned_counts) goto oom;

    for (size_t i = 0; i < param_count; i++) {
        owned_names[i] = cxpr_strdup(param_buf[i]);
        if (!owned_names[i]) goto oom;

        owned_counts[i] = sets[i].count;
        if (sets[i].count > 0) {
            owned_fields[i] = (char**)calloc(sets[i].count, sizeof(char*));
            if (!owned_fields[i]) goto oom;
            for (size_t f = 0; f < sets[i].count; f++) {
                owned_fields[i][f] = cxpr_strdup(sets[i].fields[f]);
                if (!owned_fields[i][f]) goto oom;
            }
        }
    }

    /* --- Install into registry --- */
    {
        cxpr_func_entry* existing = cxpr_registry_find(reg, fname);
        if (existing) {
            if (existing->userdata_free) existing->userdata_free(existing->userdata);
            free_struct_fields(existing);
            free_defined_fn(existing);
            free_arg_types(existing);
            existing->sync_func                  = NULL;
            existing->value_func                 = NULL;
            existing->typed_func                 = NULL;
            existing->native_kind               = CXPR_NATIVE_KIND_NONE;
            memset(&existing->native_scalar, 0, sizeof(existing->native_scalar));
            existing->min_args                   = param_count;
            existing->max_args                   = param_count;
            existing->arg_types                  = NULL;
            existing->arg_type_count             = 0;
            existing->return_type                = CXPR_VALUE_NUMBER;
            existing->has_return_type            = false;
            existing->userdata                   = NULL;
            existing->userdata_free              = NULL;
            existing->defined_body               = body_ast;
            existing->defined_program            = NULL;
            existing->defined_program_failed     = false;
            existing->defined_param_names        = owned_names;
            existing->defined_param_count        = param_count;
            existing->defined_param_fields       = owned_fields;
            existing->defined_param_field_counts = owned_counts;
            reg->version++;
            return err; /* CXPR_OK */
        }

        if (reg->count >= reg->capacity && !cxpr_registry_grow(reg)) {
            err.code    = CXPR_ERR_OUT_OF_MEMORY;
            err.message = "Registry growth failed";
            return err;
        }
        cxpr_func_entry* entry           = &reg->entries[reg->count++];
        entry->name                      = cxpr_strdup(fname);
        entry->sync_func                 = NULL;
        entry->value_func                = NULL;
        entry->typed_func                = NULL;
        entry->native_kind              = CXPR_NATIVE_KIND_NONE;
        memset(&entry->native_scalar, 0, sizeof(entry->native_scalar));
        entry->min_args                  = param_count;
        entry->max_args                  = param_count;
        entry->arg_types                 = NULL;
        entry->arg_type_count            = 0;
        entry->return_type               = CXPR_VALUE_NUMBER;
        entry->has_return_type           = false;
        entry->userdata                  = NULL;
        entry->userdata_free             = NULL;
        entry->defined_body              = body_ast;
        entry->defined_program           = NULL;
        entry->defined_program_failed    = false;
        entry->defined_param_names       = owned_names;
        entry->defined_param_count       = param_count;
        entry->defined_param_fields      = owned_fields;
        entry->defined_param_field_counts = owned_counts;
        reg->version++;
        return err; /* CXPR_OK */
    }

oom:
    cxpr_ast_free(body_ast);
    if (owned_names) {
        for (size_t i = 0; i < param_count; i++) free(owned_names[i]);
        free(owned_names);
    }
    if (owned_fields) {
        for (size_t i = 0; i < param_count; i++) {
            if (owned_fields[i]) {
                for (size_t f = 0; f < owned_counts[i]; f++) free(owned_fields[i][f]);
                free(owned_fields[i]);
            }
        }
        free(owned_fields);
    }
    free(owned_counts);
    err.code = CXPR_ERR_OUT_OF_MEMORY; err.message = "Out of memory";
    return err;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Direct function invocation (for allocation-free evaluation)
 * ═══════════════════════════════════════════════════════════════════════════ */

cxpr_value cxpr_registry_call_typed(const cxpr_registry* reg, const char* name,
                                    const cxpr_value* args, size_t argc, cxpr_error* err) {
    cxpr_func_entry* entry = cxpr_registry_find(reg, name);
    double scalar_args[32];

    if (!entry || (!entry->sync_func && !entry->value_func && !entry->typed_func)) {
        if (err) {
            err->code = CXPR_ERR_UNKNOWN_FUNCTION;
            err->message = "Unknown function";
            err->position = 0;
            err->line = 0;
            err->column = 0;
        }
        return cxpr_fv_double(NAN);
    }

    if (argc < entry->min_args || argc > entry->max_args) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Wrong number of arguments";
            err->position = 0;
            err->line = 0;
            err->column = 0;
        }
        return cxpr_fv_double(NAN);
    }

    if (entry->arg_types) {
        for (size_t i = 0; i < argc && i < entry->arg_type_count; ++i) {
            if (args[i].type != entry->arg_types[i]) {
                if (err) {
                    err->code = CXPR_ERR_TYPE_MISMATCH;
                    err->message = "Function argument type mismatch";
                    err->position = 0;
                    err->line = 0;
                    err->column = 0;
                }
                return cxpr_fv_double(NAN);
            }
        }
    }

    if (entry->typed_func) {
        if (err) *err = (cxpr_error){0};
        return entry->typed_func(args, argc, entry->userdata);
    }

    if (argc > 32) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Too many function arguments";
            err->position = 0;
            err->line = 0;
            err->column = 0;
        }
        return cxpr_fv_double(NAN);
    }

    for (size_t i = 0; i < argc; ++i) {
        if (args[i].type != CXPR_VALUE_NUMBER) {
            if (err) {
                err->code = CXPR_ERR_TYPE_MISMATCH;
                err->message = "Function arguments must be doubles";
                err->position = 0;
                err->line = 0;
                err->column = 0;
            }
            return cxpr_fv_double(NAN);
        }
        scalar_args[i] = args[i].d;
    }

    if (entry->value_func) {
        if (err) *err = (cxpr_error){0};
        return entry->value_func(scalar_args, argc, entry->userdata);
    }
    if (err) *err = (cxpr_error){0};
    return cxpr_fv_double(entry->sync_func(scalar_args, argc, entry->userdata));
}

cxpr_value cxpr_registry_call_value(const cxpr_registry* reg, const char* name,
                                    const double* args, size_t argc, cxpr_error* err) {
    cxpr_value typed_args[32];
    if (argc > 32) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Too many function arguments";
            err->position = 0;
            err->line = 0;
            err->column = 0;
        }
        return cxpr_fv_double(NAN);
    }
    for (size_t i = 0; i < argc; ++i) {
        typed_args[i] = cxpr_fv_double(args[i]);
    }
    return cxpr_registry_call_typed(reg, name, typed_args, argc, err);
}

/**
 * @brief Call a registered synchronous function directly.
 * @param[in] reg Registry containing the function
 * @param[in] name Function name to call
 * @param[in] args Argument values array
 * @param[in] argc Number of arguments
 * @param[out] err Error output (can be NULL)
 * @return Function result, or NaN on error
 */
double cxpr_registry_call(const cxpr_registry* reg, const char* name,
                          const double* args, size_t argc, cxpr_error* err) {
    cxpr_value value = cxpr_registry_call_value(reg, name, args, argc, err);
    if (err && err->code != CXPR_OK) return NAN;
    if (value.type == CXPR_VALUE_NUMBER) return value.d;
    if (value.type == CXPR_VALUE_BOOL) return value.b ? 1.0 : 0.0;
    if (err) {
        err->code = CXPR_ERR_TYPE_MISMATCH;
        err->message = "Function did not evaluate to scalar";
        err->position = 0;
        err->line = 0;
        err->column = 0;
    }
    return NAN;
}
