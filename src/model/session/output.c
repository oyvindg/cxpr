/**
 * @file model/session/output.c
 * @brief Model-session output lookup and transition queries.
 */

#include "model/internal.h"

static const cxpr_model_output_state*
cxpr_model_session_find_output(const cxpr_model_session* session, const char* name) {
    if (!session || !name) return NULL;
    for (size_t i = 0; i < session->output_count; ++i) {
        if (cxpr_model_names_match(session->outputs[i].name, name)) return &session->outputs[i];
    }
    return NULL;
}

bool cxpr_model_session_get_bool(const cxpr_model_session* session,
                                    const char* name,
                                    bool* out_value) {
    const cxpr_model_output_state* state = cxpr_model_session_find_output(session, name);
    if (!state || !state->has_current) return false;
    if (out_value) *out_value = state->current;
    return true;
}

bool cxpr_model_session_get_number(const cxpr_model_session* session,
                                      const char* name,
                                      double* out_value) {
    const cxpr_model_output_state* state = cxpr_model_session_find_output(session, name);
    if (!state || !state->has_number_current) return false;
    if (out_value) *out_value = state->number_current;
    return true;
}

bool cxpr_model_session_is_rising(const cxpr_model_session* session, const char* name) {
    const cxpr_model_output_state* state = cxpr_model_session_find_output(session, name);
    return state && state->has_current && state->current &&
           (!state->has_previous || !state->previous);
}

bool cxpr_model_session_is_falling(const cxpr_model_session* session, const char* name) {
    const cxpr_model_output_state* state = cxpr_model_session_find_output(session, name);
    return state && state->has_current && !state->current &&
           state->has_previous && state->previous;
}

bool cxpr_model_session_is_changed(const cxpr_model_session* session, const char* name) {
    const cxpr_model_output_state* state = cxpr_model_session_find_output(session, name);
    return state && state->has_current &&
           (!state->has_previous || state->current != state->previous);
}
