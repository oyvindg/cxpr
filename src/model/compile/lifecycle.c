/**
 * @file model/compile/lifecycle.c
 * @brief Manage shared compiled-model lifecycle state.
 */

#include "core.h"
#include "ast/internal.h"
#include "ir/compile/internal.h"
#include "lookback.h"
#include "model/internal.h"
#include <cxpr/resample.h>
#include <stdlib.h>
#include <string.h>

#include <cxpr/source.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



void cxpr_model_set_error(cxpr_error* err, cxpr_error_code code,
                          const char* message, size_t line, size_t column) {
    if (!err) return;
    err->code = code;
    err->message = message;
    err->position = 0;
    err->line = line;
    err->column = column;
}


static void cxpr_model_slot_ref_free(cxpr_model_slot_ref* ref) {
    if (!ref) return;
    free(ref->name);
    ref->name = NULL;
    ref->hash = 0u;
    ref->slot = 0u;
    ref->result_kind = CXPR_MODEL_RESULT_UNKNOWN;
}

void cxpr_model_fused_program_clear(cxpr_model_compiled* program) {
    if (!program) return;
    cxpr_ir_program_reset(&program->fused_ir);
    for (size_t i = 0; i < program->fused_slot_count; ++i) {
        free(program->fused_slot_names[i]);
    }
    free(program->fused_slot_names);
    free(program->fused_slot_hashes);
    for (size_t i = 0; i < program->fused_input_count; ++i) {
        cxpr_model_slot_ref_free(&program->fused_inputs[i]);
    }
    free(program->fused_inputs);
    for (size_t i = 0; i < program->fused_export_count; ++i) {
        cxpr_model_slot_ref_free(&program->fused_exports[i]);
    }
    free(program->fused_exports);
    for (size_t i = 0; i < program->fused_output_count; ++i) {
        cxpr_model_slot_ref_free(&program->fused_outputs[i]);
    }
    free(program->fused_outputs);
    free(program->fused_commits);
    program->has_fused_ir = false;
    program->has_fused_layout = false;
    program->fused_disabled_opcode = NULL;
    program->fused_slot_names = NULL;
    program->fused_slot_hashes = NULL;
    program->fused_slot_count = 0u;
    program->fused_inputs = NULL;
    program->fused_input_count = 0u;
    program->fused_exports = NULL;
    program->fused_export_count = 0u;
    program->fused_outputs = NULL;
    program->fused_output_count = 0u;
    program->fused_commits = NULL;
    program->fused_commit_count = 0u;
}

bool cxpr_model_names_match(const char* a, const char* b) {
    return a && b && strcmp(a, b) == 0;
}

