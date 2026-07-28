#ifndef CXPR_EVAL_SNAPSHOT_INTERNAL_H
#define CXPR_EVAL_SNAPSHOT_INTERNAL_H

#include <cxpr/snapshot.h>

char* cxpr_snapshot_strdup(const char* text);
char* cxpr_snapshot_printf_number(double value);
char* cxpr_snapshot_value_text(const cxpr_value* value);
cxpr_snapshot_state cxpr_snapshot_state_for_value(const cxpr_value* value);
bool cxpr_snapshot_value_clone_failed(const cxpr_value* source,
                                      const cxpr_value* clone);
void cxpr_snapshot_set_string(char** dst, char* value);

bool cxpr_snapshot_json_string(FILE* out, const char* text);
bool cxpr_snapshot_write_optional_value_fields(FILE* out,
                                               const cxpr_value* value,
                                               int has_value);
bool cxpr_snapshot_write_document_prefix(
    FILE* out,
    const char* schema,
    const cxpr_snapshot_json_hooks* hooks);

#endif /* CXPR_EVAL_SNAPSHOT_INTERNAL_H */
