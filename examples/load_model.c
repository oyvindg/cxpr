/* Minimal example: load a .cxpr model from disk through the public API. */
#include <cxpr/cxpr.h>

#include <stdio.h>

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "examples/load_model.cxpr";
    cxpr_error err = {0};
    cxpr_doc* doc = cxpr_doc_load_model(path, &err);
    const cxpr_model* model;
    cxpr_model_compiled* program;
    cxpr_model_session* session;
    cxpr_context* context;
    bool above_threshold;

    if (!doc) {
        char message[256];
        cxpr_error_format(&err, message, sizeof(message));
        fprintf(stderr, "Failed to load %s: %s\n", path, message);
        return 1;
    }

    model = cxpr_doc_model(doc);
    if (!model) {
        fprintf(stderr, "%s does not contain a model\n", path);
        cxpr_doc_free(doc);
        return 1;
    }

    program = cxpr_model_compile(model, NULL, &err);
    session = program ? cxpr_model_session_new(program, NULL, &err) : NULL;
    if (!session) {
        char message[256];
        cxpr_error_format(&err, message, sizeof(message));
        fprintf(stderr, "Failed to compile %s: %s\n", path, message);
        cxpr_model_compiled_free(program);
        cxpr_doc_free(doc);
        return 1;
    }

    context = cxpr_model_session_context(session);
    cxpr_context_set(context, "price", 125.0);
    if (!cxpr_model_session_tick(program, session, NULL, &err) ||
        !cxpr_model_session_get_bool(session, "above_threshold",
                                     &above_threshold)) {
        char message[256];
        cxpr_error_format(&err, message, sizeof(message));
        fprintf(stderr, "Failed to evaluate %s: %s\n", path, message);
        cxpr_model_session_free(session);
        cxpr_model_compiled_free(program);
        cxpr_doc_free(doc);
        return 1;
    }

    printf("price=125, above_threshold=%s\n",
           above_threshold ? "true" : "false");
    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
    cxpr_doc_free(doc);
    return 0;
}
