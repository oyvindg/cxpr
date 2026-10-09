#include "core.h"
#include "model/internal.h"
#include "model/compile/internal.h"
#include <cxpr/typecheck.h>
#include <stdio.h>
#include <stdlib.h>

static bool cxpr_model_compile_condition_group(
    const cxpr_model_assert* sources,
    size_t count,
    const char* label,
    cxpr_model_assert** out,
    const cxpr_registry* registry,
    cxpr_error* err) {
    if (count == 0u) return true;
    *out = (cxpr_model_assert*)calloc(count, sizeof(**out));
    if (!*out) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    for (size_t i = 0u; i < count; ++i) {
        const char* references[1];
        cxpr_model_assert* target = &(*out)[i];
        target->source = cxpr_strdup(sources[i].source);
        target->description = cxpr_strdup(sources[i].description);
        target->expr = cxpr_model_inline_defined_calls(sources[i].expr, registry, err);
        target->span = sources[i].span;
        target->has_span = sources[i].has_span;
        if (!target->source || !target->description || !target->expr) {
            if (err && err->code == CXPR_OK)
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        if (!cxpr_typecheck(target->expr, registry, NULL, err)) return false;
        if (cxpr_expr_ast_references(target->expr, references, 1u) != 0u) {
            static CXPR_THREAD_LOCAL char message[256];
            snprintf(message, sizeof(message),
                     "%s may only reference $params; found '%s'", label,
                     references[0] ? references[0] : "identifier");
            cxpr_model_set_error(
                err, CXPR_ERR_SYNTAX, message,
                sources[i].has_span ? sources[i].span.start.line : 0u,
                sources[i].has_span ? sources[i].span.start.column : 0u);
            return false;
        }
    }
    return true;
}

bool cxpr_model_compile_conditions(const cxpr_model* model,
                                   cxpr_model_compiled* program,
                                   const cxpr_registry* registry,
                                   cxpr_error* err) {
    program->assert_count = model->assert_count;
    program->optimize_constraint_count = model->optimize_constraint_count;
    return cxpr_model_compile_condition_group(
               model->asserts, model->assert_count, "assert",
               &program->asserts, registry, err) &&
           cxpr_model_compile_condition_group(
               model->optimize_constraints, model->optimize_constraint_count,
               "optimize constraint", &program->optimize_constraints,
               registry, err);
}
