#ifndef CXPR_MODEL_WINDOW_WINDOW_H
#define CXPR_MODEL_WINDOW_WINDOW_H

#include "model/internal.h"

bool cxpr_model_window_is_function(const char* name);
bool cxpr_model_window_collect_call(const cxpr_model* model,
                                    const cxpr_ast* call,
                                    cxpr_model_history_spec** specs,
                                    size_t* count,
                                    cxpr_error* err);

#endif /* CXPR_MODEL_WINDOW_WINDOW_H */
