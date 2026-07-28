#ifndef CXPR_MODEL_WINDOW_PLAN_H
#define CXPR_MODEL_WINDOW_PLAN_H

#include "model/internal.h"
#include <cxpr/window.h>

typedef cxpr_window_op cxpr_model_window_plan_op;

#define CXPR_MODEL_WINDOW_PLAN_OP_NONE CXPR_WINDOW_OP_NONE
#define CXPR_MODEL_WINDOW_PLAN_OP_ROC CXPR_WINDOW_OP_ROC
#define CXPR_MODEL_WINDOW_PLAN_OP_SUM CXPR_WINDOW_OP_SUM
#define CXPR_MODEL_WINDOW_PLAN_OP_MEAN CXPR_WINDOW_OP_MEAN
#define CXPR_MODEL_WINDOW_PLAN_OP_WMA CXPR_WINDOW_OP_WMA
#define CXPR_MODEL_WINDOW_PLAN_OP_STDDEV CXPR_WINDOW_OP_STDDEV
#define CXPR_MODEL_WINDOW_PLAN_OP_HIGHEST CXPR_WINDOW_OP_HIGHEST
#define CXPR_MODEL_WINDOW_PLAN_OP_LOWEST CXPR_WINDOW_OP_LOWEST

/** @brief One planned window expression and its generated-state layout. */
typedef struct {
    const cxpr_expr_ast* ast;
    cxpr_model_window_plan_op op;
    const cxpr_expr_ast* value_ast;
    const cxpr_expr_ast* period_ast;
    size_t period_capacity;
    size_t slot_offset;
    size_t slot_count;
    size_t child_index;
    bool has_child;
} cxpr_model_window_plan_node;

/** @brief Complete window execution plan for a compiled model. */
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
    const cxpr_expr_ast* ast);
size_t cxpr_model_window_plan_slot_count(const cxpr_model_window_plan* plan);

#endif /* CXPR_MODEL_WINDOW_PLAN_H */
