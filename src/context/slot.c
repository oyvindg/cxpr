/**
 * @file slot.c
 * @brief Bound-slot helpers for hot-loop variable writes.
 */

#include "internal.h"

bool cxpr_context_slot_bind(cxpr_context* ctx, const char* name, cxpr_context_slot* slot) {
    unsigned long hash;
    cxpr_hashmap_entry* entry;

    if (!slot) return false;
    slot->_ptr = NULL;
    slot->_base = NULL;
    if (!ctx || !name) return false;

    hash = cxpr_hash_string(name);
    entry = cxpr_context_find_variable_slot(ctx, name, hash);
    if (!entry) return false;

    slot->_ptr = &entry->value;
    slot->_base = cxpr_context_variables_base(ctx);
    return true;
}

bool cxpr_context_slot_valid(const cxpr_context* ctx, const cxpr_context_slot* slot) {
    return ctx && slot && slot->_ptr && slot->_base == cxpr_context_variables_base(ctx);
}

void cxpr_context_slot_set(cxpr_context_slot* slot, double value) {
    *slot->_ptr = value;
}

double cxpr_context_slot_get(const cxpr_context_slot* slot) {
    return *slot->_ptr;
}
