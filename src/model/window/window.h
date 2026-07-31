/**
 * @file model/window/window.h
 * @brief Internal model window-function discovery and history collection.
 */

#ifndef CXPR_MODEL_WINDOW_WINDOW_H
#define CXPR_MODEL_WINDOW_WINDOW_H

#include "model/internal.h"

/**
 * @brief Check whether a function name identifies a built-in window operation.
 *
 * @param name Function name to inspect.
 * @return true when @p name has registered window IR metadata.
 */
bool cxpr_model_window_is_function(const char* name);

/**
 * @brief Collect history requirements for one window-function call.
 *
 * Existing entries in @p specs are retained and their depths are increased
 * when the call requires more history. New entries and their cloned target
 * ASTs are owned by the caller through the resulting history-spec array.
 * Non-call and non-window AST nodes are accepted without changing the array.
 *
 * @param model Model used to resolve parameter bounds and constant defaults.
 * @param call Candidate window-function call AST.
 * @param enclosing_depth History offset already applied around the call.
 * @param specs In/out history-spec array, grown as required.
 * @param count In/out number of entries in @p specs.
 * @param err Optional error output for invalid calls or allocation failures.
 * @return true on success; false when validation or allocation fails.
 */
bool cxpr_model_window_collect_call(const cxpr_model* model,
                                    const cxpr_expr_ast* call,
                                    size_t enclosing_depth,
                                    cxpr_model_history_spec** specs,
                                    size_t* count,
                                    cxpr_error* err);

#endif /* CXPR_MODEL_WINDOW_WINDOW_H */
