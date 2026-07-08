#include <cxpr/plugin.h>

int cxpr_plugin_run_model_backend(
    const cxpr_plugin_model_event* event,
    const cxpr_plugin_backend* backend,
    const void* options,
    const cxpr_plugin_host* host,
    cxpr_error* err) {
    if (err) *err = (cxpr_error){0};
    if (!backend || !backend->generate) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Invalid cxpr plugin backend";
        }
        return 0;
    }
    return backend->generate(event, options, host, err);
}
