#ifndef CXPR_MODEL_COMPILE_INTERNAL_H
#define CXPR_MODEL_COMPILE_INTERNAL_H

#include "model/internal.h"

cxpr_model_result_kind cxpr_model_infer_result_kind(
    const cxpr_expr_ast* ast,
    const cxpr_registry* reg);

bool cxpr_model_copy_bindings_and_outputs(cxpr_model* dst,
                                          const cxpr_model* src,
                                          cxpr_error* err);

void cxpr_model_expanded_copy_free(cxpr_model* model);

const char* cxpr_model_import_namespace_name(const cxpr_model* model,
                                             const char* import_name);

bool cxpr_model_infer_inputs_for_compile(const cxpr_model* model,
                                         const cxpr_model_import* imports,
                                         size_t import_count,
                                         char*** out_inputs,
                                         size_t* out_input_count,
                                         cxpr_error* err);

bool cxpr_model_expand_anonymous_outputs(cxpr_model* model,
                                         const cxpr_model_import* imports,
                                         size_t import_count,
                                         cxpr_error* err);

#endif
