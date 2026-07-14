#ifndef CXPR_MODEL_WINDOW_PLAN_H
#define CXPR_MODEL_WINDOW_PLAN_H

#include "model/internal.h"

typedef enum {
    CXPR_MODEL_WINDOW_PLAN_OP_NONE = 0,
    CXPR_MODEL_WINDOW_PLAN_OP_ROC,
    CXPR_MODEL_WINDOW_PLAN_OP_SUM,
    CXPR_MODEL_WINDOW_PLAN_OP_MEAN,
    CXPR_MODEL_WINDOW_PLAN_OP_STDDEV,
    CXPR_MODEL_WINDOW_PLAN_OP_HIGHEST,
    CXPR_MODEL_WINDOW_PLAN_OP_LOWEST
} cxpr_model_window_plan_op;

typedef struct {
    const cxpr_ast* ast;
    cxpr_model_window_plan_op op;
    const cxpr_ast* value_ast;
    const cxpr_ast* period_ast;
    size_t period_capacity;
    size_t slot_offset;
    size_t slot_count;
    size_t child_index;
    bool has_child;
} cxpr_model_window_plan_node;

typedef struct {
    cxpr_model_window_plan_node* nodes;
    size_t node_count;
    size_t slot_count;
} cxpr_model_window_plan;

bool cxpr_model_window_plan_build(const cxpr_model_program* program,
                                  cxpr_model_window_plan* out,
                                  cxpr_error* err);
void cxpr_model_window_plan_free(cxpr_model_window_plan* plan);
const cxpr_model_window_plan_node* cxpr_model_window_plan_find_ast(
    const cxpr_model_window_plan* plan,
    const cxpr_ast* ast);
size_t cxpr_model_window_plan_slot_count(const cxpr_model_window_plan* plan);

#endif /* CXPR_MODEL_WINDOW_PLAN_H */
