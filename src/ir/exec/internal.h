/**
 * @file internal.h
 * @brief Shared internal helpers for split IR execution implementation.
 */

#ifndef CXPR_IR_EXEC_INTERNAL_H
#define CXPR_IR_EXEC_INTERNAL_H

#include "ir/internal.h"

/** @brief Read one scalar variable or param using a precomputed hash. */
double cxpr_ir_context_get_prehashed(const cxpr_context* ctx, const char* name,
                                     unsigned long hash, bool* found);
/** @brief Look up one typed field from a struct value. */
cxpr_value cxpr_ir_struct_get_field(const cxpr_struct_value* value,
                                    const char* field, bool* found);
/** @brief Load one dotted field-access value for an IR instruction. */
cxpr_value cxpr_ir_load_field_value(const cxpr_context* ctx, const cxpr_registry* reg,
                                    const cxpr_ir_instr* instr, cxpr_error* err);
/** @brief Load one multi-segment chain-access value for an IR instruction. */
cxpr_value cxpr_ir_load_chain_value(const cxpr_context* ctx, const cxpr_ir_instr* instr,
                                    cxpr_error* err);
/** @brief Push a squared numeric value onto the IR operand stack. */
bool cxpr_ir_push_squared(cxpr_value* stack, size_t* sp, cxpr_value value,
                          cxpr_error* err);
/** @brief Pop one value from the IR operand stack. */
bool cxpr_ir_pop1(cxpr_value* stack, size_t* sp, cxpr_value* out,
                  cxpr_error* err);
/** @brief Pop two values from the IR operand stack in left/right order. */
bool cxpr_ir_pop2(cxpr_value* stack, size_t* sp, cxpr_value* left,
                  cxpr_value* right, cxpr_error* err);
/** @brief Load a variable/param as a typed value for the general IR executor. */
cxpr_value cxpr_ir_load_variable_typed(const cxpr_context* ctx,
                                       const cxpr_ir_program* program,
                                       size_t ip,
                                       const cxpr_ir_instr* instr,
                                       bool* found);
/** @brief Invoke a struct producer and cache the full struct result. */
cxpr_value cxpr_ir_call_producer_cached(cxpr_func_entry* entry, const char* name,
                                        const char* cache_key,
                                        const cxpr_context* ctx,
                                        const cxpr_value* stack_args,
                                        size_t argc, cxpr_error* err);
/** @brief Invoke a struct producer without a prebuilt cache key. */
cxpr_value cxpr_ir_call_producer(cxpr_func_entry* entry, const char* name,
                                 const cxpr_context* ctx,
                                 const cxpr_value* stack_args,
                                 size_t argc, cxpr_error* err);
/** @brief Invoke a cached struct producer and return one selected field. */
cxpr_value cxpr_ir_call_producer_field_cached(cxpr_func_entry* entry,
                                              const char* name,
                                              const char* cache_key,
                                              const cxpr_context* ctx,
                                              const cxpr_value* stack_args,
                                              size_t argc,
                                              const char* field,
                                              cxpr_error* err);
/** @brief Invoke a struct producer and return one selected field. */
cxpr_value cxpr_ir_call_producer_field(cxpr_func_entry* entry, const char* name,
                                       const cxpr_context* ctx,
                                       const cxpr_value* stack_args,
                                       size_t argc, const char* field,
                                       cxpr_error* err);
/** @brief Return one field from a producer call with compile-time constant scalar args. */
cxpr_value cxpr_ir_call_producer_const_field(cxpr_func_entry* entry,
                                             const char* cache_key,
                                             const cxpr_context* ctx,
                                             const double* const_args,
                                             size_t argc,
                                             const char* field,
                                             cxpr_error* err);
/** @brief Execute one expression-defined function on the scalar-only IR path. */
cxpr_value cxpr_ir_call_defined_scalar(cxpr_func_entry* entry,
                                       const cxpr_context* ctx,
                                       const cxpr_registry* reg,
                                       const cxpr_value* args,
                                       size_t argc, cxpr_error* err);
/** @brief Execute the general typed IR interpreter. */
cxpr_value cxpr_ir_exec_typed(const cxpr_ir_program* program, const cxpr_context* ctx,
                              const cxpr_registry* reg, const double* locals,
                              size_t local_count, cxpr_error* err);
/** @brief Execute the scalar fast-path IR interpreter. */
double cxpr_ir_exec_scalar_fast(const cxpr_ir_program* program, const cxpr_context* ctx,
                                const cxpr_registry* reg, const double* locals,
                                size_t local_count, cxpr_error* err);
/** @brief Execute a boolean fast-path IR program with separate number/bool stacks. */
bool cxpr_ir_exec_bool_fast(const cxpr_ir_program* program, const cxpr_context* ctx,
                            const cxpr_registry* reg, const double* locals,
                            size_t local_count, bool* out_value, cxpr_error* err);
/** @brief Evaluate a public `cxpr_program` to a typed runtime value. */
cxpr_value cxpr_eval_program_value(const cxpr_program* prog, const cxpr_context* ctx,
                                   const cxpr_registry* reg, cxpr_error* err);

#endif /* CXPR_IR_EXEC_INTERNAL_H */
