/**
 * @file registry.h
 * @brief Public function registry API for cxpr.
 */

#ifndef CXPR_REGISTRY_H
#define CXPR_REGISTRY_H

#include <cxpr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback type for synchronous scalar functions.
 * @param args Evaluated numeric arguments.
 * @param argc Number of arguments.
 * @param userdata Opaque user pointer supplied at registration time.
 * @return Numeric function result.
 */
typedef double (*cxpr_func_ptr)(const double* args, size_t argc, void* userdata);
/**
 * @brief Callback type for synchronous typed-value functions.
 * @param args Evaluated numeric arguments.
 * @param argc Number of arguments.
 * @param userdata Opaque user pointer supplied at registration time.
 * @return Typed function result.
 */
typedef cxpr_value (*cxpr_value_func_ptr)(const double* args, size_t argc, void* userdata);
/**
 * @brief Callback type for synchronous fully-typed functions.
 * @param args Evaluated typed arguments.
 * @param argc Number of arguments.
 * @param userdata Opaque user pointer supplied at registration time.
 * @return Typed function result.
 */
typedef cxpr_value (*cxpr_typed_func_ptr)(const cxpr_value* args, size_t argc, void* userdata);
/**
 * @brief Callback type for AST-aware functions.
 * @param call_ast The full function-call AST node.
 * @param ctx Evaluation context.
 * @param reg Function registry.
 * @param userdata Opaque user pointer supplied at registration time.
 * @param err Optional error output.
 * @return Typed function result.
 */
typedef cxpr_value (*cxpr_ast_func_ptr)(const cxpr_ast* call_ast,
                                        const cxpr_context* ctx,
                                        const cxpr_registry* reg,
                                        void* userdata,
                                        cxpr_error* err);
/**
 * @brief Callback type for struct-producing functions.
 * @param args Evaluated numeric arguments.
 * @param argc Number of arguments.
 * @param out Output field array to populate.
 * @param field_count Number of fields expected in `out`.
 * @param userdata Opaque user pointer supplied at registration time.
 */
typedef void (*cxpr_struct_producer_ptr)(const double* args, size_t argc,
                                         cxpr_value* out, size_t field_count,
                                         void* userdata);
/**
 * @brief Optional cleanup callback for registry user data.
 * @param userdata User pointer to destroy.
 */
typedef void (*cxpr_userdata_free_fn)(void* userdata);

/**
 * @brief Create a new registry.
 * @return Newly allocated registry, or NULL on allocation failure.
 */
cxpr_registry* cxpr_registry_new(void);
/**
 * @brief Free a registry and its owned registrations.
 * @param reg Registry to free. May be NULL.
 */
void cxpr_registry_free(cxpr_registry* reg);

/**
 * @brief Register a scalar function.
 * @param reg Destination registry.
 * @param name Function name.
 * @param func Callback to invoke.
 * @param min_args Minimum accepted arity.
 * @param max_args Maximum accepted arity.
 * @param userdata User pointer passed to `func`.
 * @param free_userdata Optional cleanup callback for `userdata`.
 */
void cxpr_registry_add(cxpr_registry* reg, const char* name,
                       cxpr_func_ptr func, size_t min_args, size_t max_args,
                       void* userdata, cxpr_userdata_free_fn free_userdata);
/**
 * @brief Register a typed-value function.
 * @param reg Destination registry.
 * @param name Function name.
 * @param func Callback to invoke.
 * @param min_args Minimum accepted arity.
 * @param max_args Maximum accepted arity.
 * @param userdata User pointer passed to `func`.
 * @param free_userdata Optional cleanup callback for `userdata`.
 */
void cxpr_registry_add_value(cxpr_registry* reg, const char* name,
                             cxpr_value_func_ptr func, size_t min_args, size_t max_args,
                             void* userdata, cxpr_userdata_free_fn free_userdata);
/**
 * @brief Register a fully-typed function with typed argument validation.
 * @param reg Destination registry.
 * @param name Function name.
 * @param func Callback to invoke.
 * @param min_args Minimum accepted arity.
 * @param max_args Maximum accepted arity.
 * @param arg_types Optional argument-type array with `max_args` entries.
 *        When non-NULL, the first `argc` elements are validated before calling `func`.
 * @param return_type Declared result type for analysis and validation.
 * @param userdata User pointer passed to `func`.
 * @param free_userdata Optional cleanup callback for `userdata`.
 */
void cxpr_registry_add_typed(cxpr_registry* reg, const char* name,
                             cxpr_typed_func_ptr func, size_t min_args, size_t max_args,
                             const cxpr_value_type* arg_types, cxpr_value_type return_type,
                             void* userdata, cxpr_userdata_free_fn free_userdata);
/**
 * @brief Register an AST-aware function.
 * @param reg Destination registry.
 * @param name Function name.
 * @param func Callback that receives the full call AST and runtime context.
 * @param min_args Minimum accepted arity.
 * @param max_args Maximum accepted arity.
 * @param return_type Declared result type for analysis and validation.
 * @param userdata User pointer passed to `func`.
 * @param free_userdata Optional cleanup callback for `userdata`.
 */
void cxpr_registry_add_ast(cxpr_registry* reg, const char* name,
                           cxpr_ast_func_ptr func, size_t min_args, size_t max_args,
                           cxpr_value_type return_type,
                           void* userdata, cxpr_userdata_free_fn free_userdata);
/**
 * @brief Register a unary scalar function.
 * @param reg Destination registry.
 * @param name Function name.
 * @param func Unary scalar callback.
 */
void cxpr_registry_add_unary(cxpr_registry* reg, const char* name,
                             double (*func)(double));
/**
 * @brief Register a binary scalar function.
 * @param reg Destination registry.
 * @param name Function name.
 * @param func Binary scalar callback.
 */
void cxpr_registry_add_binary(cxpr_registry* reg, const char* name,
                              double (*func)(double, double));
/**
 * @brief Register a nullary scalar function.
 * @param reg Destination registry.
 * @param name Function name.
 * @param func Nullary scalar callback.
 */
void cxpr_registry_add_nullary(cxpr_registry* reg, const char* name,
                               double (*func)(void));
/**
 * @brief Register a ternary scalar function.
 * @param reg Destination registry.
 * @param name Function name.
 * @param func Ternary scalar callback.
 */
void cxpr_registry_add_ternary(cxpr_registry* reg, const char* name,
                               double (*func)(double, double, double));

/**
 * @brief Look up a function registration by name.
 * @param reg Registry to query.
 * @param name Function name.
 * @param min_args Optional minimum-arity output.
 * @param max_args Optional maximum-arity output.
 * @return True if the function exists, false otherwise.
 */
bool cxpr_registry_lookup(const cxpr_registry* reg, const char* name,
                          size_t* min_args, size_t* max_args);
/**
 * @brief Call a registered scalar function directly.
 * @param reg Registry to query.
 * @param name Function name.
 * @param args Numeric argument array.
 * @param argc Number of arguments.
 * @param err Optional error output.
 * @return Numeric result, or NaN on error.
 */
double cxpr_registry_call(const cxpr_registry* reg, const char* name,
                          const double* args, size_t argc, cxpr_error* err);
/**
 * @brief Call a registered typed-value function directly.
 * @param reg Registry to query.
 * @param name Function name.
 * @param args Numeric argument array.
 * @param argc Number of arguments.
 * @param err Optional error output.
 * @return Typed result, or `cxpr_fv_double(NAN)` on error.
 */
cxpr_value cxpr_registry_call_value(const cxpr_registry* reg, const char* name,
                                    const double* args, size_t argc, cxpr_error* err);
/**
 * @brief Call a registered function directly with typed arguments.
 * @param reg Registry to query.
 * @param name Function name.
 * @param args Typed argument array.
 * @param argc Number of arguments.
 * @param err Optional error output.
 * @return Typed result, or `cxpr_fv_double(NAN)` on error.
 */
cxpr_value cxpr_registry_call_typed(const cxpr_registry* reg, const char* name,
                                    const cxpr_value* args, size_t argc, cxpr_error* err);

/**
 * @brief Register a struct-aware scalar function.
 * @param reg Destination registry.
 * @param name Function name.
 * @param func Scalar callback receiving expanded fields.
 * @param fields Field names to expand per struct argument.
 * @param fields_per_arg Number of fields expanded per struct argument.
 * @param struct_argc Number of struct arguments accepted by the expression call.
 * @param userdata User pointer passed to `func`.
 * @param free_userdata Optional cleanup callback for `userdata`.
 */
void cxpr_registry_add_fn(cxpr_registry* reg, const char* name,
                          cxpr_func_ptr func,
                          const char* const* fields, size_t fields_per_arg,
                          size_t struct_argc,
                          void* userdata, cxpr_userdata_free_fn free_userdata);
/**
 * @brief Register a struct-producing function.
 * @param reg Destination registry.
 * @param name Producer name.
 * @param func Producer callback.
 * @param min_args Minimum accepted arity.
 * @param max_args Maximum accepted arity.
 * @param fields Output field names produced by `func`.
 * @param field_count Number of output fields.
 * @param userdata User pointer passed to `func`.
 * @param free_userdata Optional cleanup callback for `userdata`.
 */
void cxpr_registry_add_struct(cxpr_registry* reg, const char* name,
                              cxpr_struct_producer_ptr func,
                              size_t min_args, size_t max_args,
                              const char* const* fields, size_t field_count,
                              void* userdata,
                              cxpr_userdata_free_fn free_userdata);

/**
 * @brief Register the built-in math library into a registry.
 * @param reg Destination registry.
 */
void cxpr_register_builtins(cxpr_registry* reg);
/**
 * @brief Register an expression-defined function.
 * @param reg Destination registry.
 * @param def Definition string of the form `name(args) => body`.
 * @return Error payload with `code == CXPR_OK` on success.
 */
cxpr_error cxpr_registry_define_fn(cxpr_registry* reg, const char* def);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_REGISTRY_H */
