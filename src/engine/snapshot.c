/** @file engine/snapshot.c @brief Engine execution-flow snapshots. */
#include "engine/internal.h"

bool cxpr_engine_snapshot_flow(const cxpr_engine_session* session,
                               cxpr_eval_snapshot_flow* out_flow,
                               cxpr_error* err) {
    cxpr_engine_session* previous_session;
    size_t previous_offset;
    bool ok;

    if (!out_flow) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "engine: snapshot output is NULL";
        }
        return false;
    }
    if (!session || !session->eval || !session->ctx) {
        memset(out_flow, 0, sizeof(*out_flow));
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_SYNTAX;
            err->message = "engine: snapshot requires a live session";
        }
        return false;
    }

    previous_session = g_engine_tls_session;
    previous_offset = g_engine_tls_lookback_offset;
    g_engine_tls_session = (cxpr_engine_session*)session;
    g_engine_tls_lookback_offset = 0u;
    ok = cxpr_eval_snapshot_build_flow(
        session->eval,
        session->ctx,
        session->prog ? session->prog->registry : NULL,
        out_flow,
        err);
    g_engine_tls_lookback_offset = previous_offset;
    g_engine_tls_session = previous_session;
    return ok;
}

bool cxpr_engine_snapshot_flow_fallback(const cxpr_engine_session* session,
                                        const cxpr_context* parent_ctx,
                                        cxpr_eval_snapshot_flow* out_flow,
                                        cxpr_error* err) {
    const cxpr_context* previous_parent;
    bool ok;

    if (!session || !session->ctx || !parent_ctx) {
        return cxpr_engine_snapshot_flow(session, out_flow, err);
    }
    previous_parent = session->ctx->parent;
    session->ctx->parent = parent_ctx;
    ok = cxpr_engine_snapshot_flow(session, out_flow, err);
    session->ctx->parent = previous_parent;
    return ok;
}
