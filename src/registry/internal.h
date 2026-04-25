/**
 * @file internal.h
 * @brief Internal registry declarations shared across cxpr modules.
 */

#ifndef CXPR_REGISTRY_INTERNAL_H
#define CXPR_REGISTRY_INTERNAL_H

#include <cxpr/registry.h>

/**
 * @brief A single registered function entry.
 */
typedef struct cxpr_func_entry {
    char* name;               /**< Function name, owned */
    cxpr_func_ptr sync_func;    /**< Scalar function pointer */
    cxpr_value_func_ptr value_func; /**< Numeric-in, typed-out function pointer */
    cxpr_typed_func_ptr typed_func; /**< Fully typed function pointer */
    cxpr_ast_func_ptr ast_func; /**< AST-aware function pointer */
    cxpr_struct_producer_ptr struct_producer; /**< Struct-producing callback */
    /* AST overlay: coexists with sync_func/struct_producer for TF string dispatch */
    cxpr_ast_func_ptr ast_func_overlay; /**< Overlay AST function; takes priority for FUNCTION_CALL */
    void* ast_func_overlay_userdata;
    cxpr_userdata_free_fn ast_func_overlay_userdata_free;
    enum {
        CXPR_NATIVE_KIND_NONE = 0,
        CXPR_NATIVE_KIND_NULLARY,
        CXPR_NATIVE_KIND_UNARY,
        CXPR_NATIVE_KIND_BINARY,
        CXPR_NATIVE_KIND_TERNARY
    } native_kind;
    union {
        double (*nullary)(void);
        double (*unary)(double);
        double (*binary)(double, double);
        double (*ternary)(double, double, double);
    } native_scalar;
    size_t min_args;
    size_t max_args;
    char** param_names;           /**< Optional parameter name array, owned */
    size_t param_name_count;      /**< Number of parameter names */
    cxpr_value_type* arg_types;   /**< Optional declared argument types (length max_args) */
    size_t arg_type_count;        /**< Number of entries in arg_types */
    cxpr_value_type return_type;  /**< Declared result type when known */
    bool has_return_type;         /**< Whether return_type is declared */
    void* userdata;
    cxpr_userdata_free_fn userdata_free;
    /* Struct expansion (NULL when not struct-aware) */
    char** struct_fields;     /**< Owned array of field name strings (e.g. {"x","y","z"}) */
    size_t fields_per_arg;    /**< Number of fields per struct argument */
    size_t struct_argc;       /**< Number of struct arguments accepted */
    /* Defined function (expression-based, via cxpr_registry_define_fn) */
    cxpr_ast*  defined_body;               /**< Parsed body AST; NULL for C functions */
    cxpr_program* defined_program;         /**< Lazily compiled body program; NULL until needed */
    bool       defined_program_failed;     /**< Sticky flag when program compilation is unsupported */
    char**     defined_param_names;        /**< Parameter name array, owned */
    size_t     defined_param_count;        /**< Number of parameters */
    char***    defined_param_fields;       /**< Per-param field lists; NULL entry = scalar */
    size_t*    defined_param_field_counts; /**< Per-param field counts */
} cxpr_func_entry;

#define CXPR_REGISTRY_INITIAL_CAPACITY 64

struct cxpr_registry {
    cxpr_func_entry* entries;   /**< Array of function entries */
    size_t capacity;
    size_t count;
    unsigned long version;      /**< Bumped whenever entries mutate or move */
    cxpr_lookback_resolver_ptr lookback_resolver;
    void* lookback_userdata;
    cxpr_userdata_free_fn free_lookback_userdata;
};

typedef struct {
    double (*fn)(double);
} cxpr_unary_userdata;

/** @brief Userdata wrapper for native binary scalar adapters. */
typedef struct {
    double (*fn)(double, double);
} cxpr_binary_userdata;

/** @brief Userdata wrapper for native nullary scalar adapters. */
typedef struct {
    double (*fn)(void);
} cxpr_nullary_userdata;

/** @brief Userdata wrapper for native ternary scalar adapters. */
typedef struct {
    double (*fn)(double, double, double);
} cxpr_ternary_userdata;

/** @brief Find one registry entry by name. */
cxpr_func_entry* cxpr_registry_find(const cxpr_registry* reg, const char* name);
/** @brief Return the canonical parameter-name array for one registry entry. */
const char* const* cxpr_registry_entry_param_names(const cxpr_func_entry* entry,
                                                   size_t* count);
/** @brief Register internal time-series builtins on a registry. */
void cxpr_register_timeseries_builtins(cxpr_registry* reg);
/** @brief Grow the registry entry array. */
bool cxpr_registry_grow(cxpr_registry* reg);
/** @brief Reset one entry to a clean zero-initialized state. */
void cxpr_registry_reset_entry(cxpr_func_entry* entry);
/** @brief Free all owned storage attached to one entry. */
void cxpr_registry_clear_owned_entry(cxpr_func_entry* entry);
/** @brief Prepare one entry with a copied function name and default metadata. */
void cxpr_registry_prepare_entry(cxpr_func_entry* entry, const char* name);
/** @brief Deep-copy a parameter-name array. */
char** cxpr_registry_clone_param_names(const char* const* param_names, size_t param_count);
/** @brief Clone a declared argument-type array. */
cxpr_value_type* cxpr_registry_clone_arg_types(const cxpr_value_type* arg_types, size_t arg_count);

/** @brief Adapter for wrapping a native unary scalar function in `cxpr_func_ptr` form. */
double cxpr_unary_adapter(const double* args, size_t argc, void* userdata);
/** @brief Adapter for wrapping a native binary scalar function in `cxpr_func_ptr` form. */
double cxpr_binary_adapter(const double* args, size_t argc, void* userdata);
/** @brief Adapter for wrapping a native nullary scalar function in `cxpr_func_ptr` form. */
double cxpr_nullary_adapter(const double* args, size_t argc, void* userdata);
/** @brief Adapter for wrapping a native ternary scalar function in `cxpr_func_ptr` form. */
double cxpr_ternary_adapter(const double* args, size_t argc, void* userdata);
/** @brief Internal variadic `min` builtin implementation. */
double cxpr_min(const double* args, size_t argc, void* userdata);
/** @brief Internal variadic `max` builtin implementation. */
double cxpr_max(const double* args, size_t argc, void* userdata);

#endif /* CXPR_REGISTRY_INTERNAL_H */
