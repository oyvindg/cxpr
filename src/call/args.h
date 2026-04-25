/**
 * @file args.h
 * @brief Internal declarations for call argument binding helpers.
 */

#ifndef CXPR_CALL_ARGS_H
#define CXPR_CALL_ARGS_H

#include "../registry/internal.h"

/**
 * @brief Bind call-site arguments to a registry entry's canonical parameter order.
 * @param ast Function or producer call AST.
 * @param entry Registry entry describing accepted arguments.
 * @param out_args Output array receiving AST arguments in canonical positional order.
 * @param out_code Optional error-code output on failure.
 * @param out_message Optional static error-message output on failure.
 * @return True on success, false when binding or validation fails.
 */
bool cxpr_call_bind_args(const cxpr_ast* ast, const cxpr_func_entry* entry,
                         const cxpr_ast** out_args,
                         cxpr_error_code* out_code,
                         const char** out_message);

#endif
