/**
 * @file context_struct.c
 * @brief Typed struct storage support for cxpr contexts.
 */

#include "context/state.h"
#include "core.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Typed struct values
 * ═══════════════════════════════════════════════════════════════════════════ */

static void cxpr_struct_value_reset(cxpr_struct_value* s) {
    if (!s) return;
    for (size_t i = 0; i < s->field_count; i++) {
        free((char*)s->field_names[i]);
        cxpr_value_free(&s->field_values[i]);
    }
    free(s->field_names);
    free(s->field_values);
    s->field_names = NULL;
    s->field_values = NULL;
    s->field_count = 0;
}

static int cxpr_value_clone_failed(const cxpr_value* source, const cxpr_value* clone) {
    if (!source || !clone) return 0;
    if (source->type == CXPR_VALUE_STRUCT && source->s && !clone->s) return 1;
    if (source->type == CXPR_VALUE_STRING && source->str && !clone->str) return 1;
    if (source->type == CXPR_VALUE_ARRAY && source->a && !clone->a) return 1;
    return 0;
}

cxpr_value cxpr_value_clone(const cxpr_value* value) {
    char* string_copy;

    if (!value) return cxpr_num(0.0);

    switch (value->type) {
    case CXPR_VALUE_NUMBER:
        return cxpr_num(value->d);
    case CXPR_VALUE_BOOL:
        return cxpr_bool(value->b);
    case CXPR_VALUE_STRUCT:
        return cxpr_struct(cxpr_struct_value_new(
            value->s ? (const char* const*)value->s->field_names : NULL,
            value->s ? value->s->field_values : NULL,
            value->s ? value->s->field_count : 0));
    case CXPR_VALUE_STRING:
        string_copy = cxpr_strdup(value->str ? value->str : "");
        if (!string_copy) return (cxpr_value){ .type = CXPR_VALUE_STRING, .str = NULL };
        return (cxpr_value){ .type = CXPR_VALUE_STRING, .str = string_copy };
    case CXPR_VALUE_NULL:
        return cxpr_null();
    case CXPR_VALUE_TIMESTAMP:
        return cxpr_timestamp(value->i64);
    case CXPR_VALUE_DURATION:
        return cxpr_duration(value->i64);
    case CXPR_VALUE_ARRAY:
        return cxpr_array(value->a ? cxpr_array_value_new(value->a->values, value->a->count) : NULL);
    default:
        return cxpr_num(0.0);
    }
}

void cxpr_value_free(cxpr_value* value) {
    if (!value) return;
    if (value->type == CXPR_VALUE_STRUCT) {
        cxpr_struct_value_free(value->s);
    } else if (value->type == CXPR_VALUE_STRING) {
        free((char*)value->str);
    } else if (value->type == CXPR_VALUE_ARRAY) {
        cxpr_array_value_free(value->a);
    }
    *value = cxpr_num(0.0);
}

cxpr_struct_value* cxpr_struct_value_new(const char* const* field_names,
                                         const cxpr_value* field_values,
                                         size_t field_count) {
    cxpr_struct_value* s = (cxpr_struct_value*)calloc(1, sizeof(cxpr_struct_value));
    if (!s) return NULL;

    s->field_count = field_count;
    if (field_count == 0) return s;

    s->field_names = (const char**)calloc(field_count, sizeof(char*));
    s->field_values = (cxpr_value*)calloc(field_count, sizeof(cxpr_value));
    if (!s->field_names || !s->field_values) {
        cxpr_struct_value_free(s);
        return NULL;
    }

    for (size_t i = 0; i < field_count; i++) {
        s->field_names[i] = cxpr_strdup(field_names[i]);
        if (!s->field_names[i]) {
            cxpr_struct_value_free(s);
            return NULL;
        }
        s->field_values[i] = cxpr_value_clone(&field_values[i]);
        if (cxpr_value_clone_failed(&field_values[i], &s->field_values[i])) {
            cxpr_struct_value_free(s);
            return NULL;
        }
    }

    return s;
}

void cxpr_struct_value_free(cxpr_struct_value* s) {
    if (!s) return;
    cxpr_struct_value_reset(s);
    free(s);
}

cxpr_array_value* cxpr_array_value_new(const cxpr_value* values, size_t count) {
    cxpr_array_value* a = (cxpr_array_value*)calloc(1, sizeof(cxpr_array_value));
    if (!a) return NULL;

    a->count = count;
    if (count == 0) return a;
    if (!values) {
        cxpr_array_value_free(a);
        return NULL;
    }

    a->values = (cxpr_value*)calloc(count, sizeof(cxpr_value));
    if (!a->values) {
        cxpr_array_value_free(a);
        return NULL;
    }

    for (size_t i = 0; i < count; i++) {
        a->values[i] = cxpr_value_clone(&values[i]);
        if (cxpr_value_clone_failed(&values[i], &a->values[i])) {
            cxpr_array_value_free(a);
            return NULL;
        }
    }

    return a;
}

void cxpr_array_value_free(cxpr_array_value* a) {
    if (!a) return;
    for (size_t i = 0; i < a->count; i++) {
        cxpr_value_free(&a->values[i]);
    }
    free(a->values);
    free(a);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Struct map
 * ═══════════════════════════════════════════════════════════════════════════ */

static cxpr_struct_map_entry* cxpr_struct_map_find_slot(const cxpr_struct_map* map,
                                                        const char* name) {
    unsigned long hash;

    if (!map->entries || map->capacity == 0) return NULL;

    hash = cxpr_hash_string(name) % map->capacity;
    while (map->entries[hash].name) {
        if (strcmp(map->entries[hash].name, name) == 0) {
            return &((cxpr_struct_map*)map)->entries[hash];
        }
        hash = (hash + 1) % map->capacity;
    }
    return &((cxpr_struct_map*)map)->entries[hash];
}

static bool cxpr_struct_map_grow(cxpr_struct_map* map) {
    if (map->capacity > SIZE_MAX / 2) return false;
    size_t new_capacity = map->capacity * 2;
    cxpr_struct_map_entry* new_entries =
        (cxpr_struct_map_entry*)calloc(new_capacity, sizeof(cxpr_struct_map_entry));
    if (!new_entries) return false;

    for (size_t i = 0; i < map->capacity; i++) {
        if (!map->entries[i].name) continue;
        unsigned long hash = cxpr_hash_string(map->entries[i].name) % new_capacity;
        while (new_entries[hash].name) {
            hash = (hash + 1) % new_capacity;
        }
        new_entries[hash] = map->entries[i];
    }

    free(map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
    return true;
}

void cxpr_struct_map_init(cxpr_struct_map* map) {
    map->capacity = 0;
    map->count = 0;
    map->entries = NULL;
}

void cxpr_struct_map_destroy(cxpr_struct_map* map) {
    if (!map->entries) return;
    for (size_t i = 0; i < map->capacity; i++) {
        free(map->entries[i].name);
        cxpr_struct_value_free(map->entries[i].value);
    }
    free(map->entries);
    map->entries = NULL;
    map->capacity = 0;
    map->count = 0;
}

void cxpr_struct_map_clear(cxpr_struct_map* map) {
    for (size_t i = 0; i < map->capacity; i++) {
        free(map->entries[i].name);
        map->entries[i].name = NULL;
        cxpr_struct_value_free(map->entries[i].value);
        map->entries[i].value = NULL;
    }
    map->count = 0;
}

bool cxpr_struct_map_clone(cxpr_struct_map* dst, const cxpr_struct_map* src) {
    cxpr_struct_map_init(dst);
    if (!src || !src->entries || src->count == 0) return true;

    dst->capacity = CXPR_HASHMAP_INITIAL_CAPACITY;
    dst->entries = (cxpr_struct_map_entry*)calloc(dst->capacity, sizeof(cxpr_struct_map_entry));
    if (!dst->entries) return false;

    for (size_t i = 0; i < src->capacity; i++) {
        cxpr_struct_value* copy;
        cxpr_struct_map_entry* slot;
        if (!src->entries[i].name) continue;
        copy = cxpr_struct_value_new((const char* const*)src->entries[i].value->field_names,
                                     src->entries[i].value->field_values,
                                     src->entries[i].value->field_count);
        if (!copy) return false;
        if ((double)(dst->count + 1) / dst->capacity > CXPR_HASHMAP_LOAD_FACTOR
            && !cxpr_struct_map_grow(dst)) {
            cxpr_struct_value_free(copy);
            return false;
        }
        slot = cxpr_struct_map_find_slot(dst, src->entries[i].name);
        slot->name = cxpr_strdup(src->entries[i].name);
        slot->value = copy;
        if (!slot->name) return false;
        dst->count++;
    }
    return true;
}

static const cxpr_struct_map_entry* cxpr_struct_map_get(const cxpr_struct_map* map,
                                                        const char* name) {
    cxpr_struct_map_entry* slot = cxpr_struct_map_find_slot(map, name);
    if (!slot || !slot->name) return NULL;
    return slot;
}

void cxpr_context_store_struct(cxpr_struct_map* map, const char* name,
                               const cxpr_struct_value* value) {
    cxpr_struct_map_entry* slot;
    cxpr_struct_value* copy;

    if (!map || !name || !value) return;

    copy = cxpr_struct_value_new((const char* const*)value->field_names,
                                 value->field_values, value->field_count);
    if (!copy) return;

    if (!map->entries) {
        map->capacity = CXPR_HASHMAP_INITIAL_CAPACITY;
        map->entries =
            (cxpr_struct_map_entry*)calloc(map->capacity, sizeof(cxpr_struct_map_entry));
        if (!map->entries) {
            map->capacity = 0;
            cxpr_struct_value_free(copy);
            return;
        }
    }

    if ((double)(map->count + 1) / map->capacity > CXPR_HASHMAP_LOAD_FACTOR
        && !cxpr_struct_map_grow(map)) {
        cxpr_struct_value_free(copy);
        return;
    }

    slot = cxpr_struct_map_find_slot(map, name);
    if (slot->name) {
        cxpr_struct_value_free(slot->value);
        slot->value = copy;
        return;
    }

    slot->name = cxpr_strdup(name);
    if (!slot->name) {
        cxpr_struct_value_free(copy);
        return;
    }
    slot->value = copy;
    map->count++;
}

const cxpr_struct_value* cxpr_context_lookup_struct_map(const cxpr_struct_map* map,
                                                        const char* name) {
    const cxpr_struct_map_entry* entry;

    if (!map || !name) return NULL;

    entry = cxpr_struct_map_get(map, name);
    return entry ? entry->value : NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Array map
 * ═══════════════════════════════════════════════════════════════════════════ */

static cxpr_array_map_entry* cxpr_array_map_find_slot(const cxpr_array_map* map,
                                                      const char* name) {
    unsigned long hash;

    if (!map->entries || map->capacity == 0) return NULL;

    hash = cxpr_hash_string(name) % map->capacity;
    while (map->entries[hash].name) {
        if (strcmp(map->entries[hash].name, name) == 0) {
            return &((cxpr_array_map*)map)->entries[hash];
        }
        hash = (hash + 1) % map->capacity;
    }
    return &((cxpr_array_map*)map)->entries[hash];
}

static bool cxpr_array_map_grow(cxpr_array_map* map) {
    size_t new_capacity;
    cxpr_array_map_entry* new_entries;

    if (map->capacity > SIZE_MAX / 2) return false;
    new_capacity = map->capacity * 2;
    new_entries = (cxpr_array_map_entry*)calloc(new_capacity, sizeof(cxpr_array_map_entry));
    if (!new_entries) return false;

    for (size_t i = 0; i < map->capacity; i++) {
        unsigned long hash;
        if (!map->entries[i].name) continue;
        hash = cxpr_hash_string(map->entries[i].name) % new_capacity;
        while (new_entries[hash].name) {
            hash = (hash + 1) % new_capacity;
        }
        new_entries[hash] = map->entries[i];
    }

    free(map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
    return true;
}

void cxpr_array_map_init(cxpr_array_map* map) {
    map->capacity = 0;
    map->count = 0;
    map->entries = NULL;
}

void cxpr_array_map_destroy(cxpr_array_map* map) {
    if (!map->entries) return;
    for (size_t i = 0; i < map->capacity; i++) {
        free(map->entries[i].name);
        cxpr_array_value_free(map->entries[i].value);
    }
    free(map->entries);
    map->entries = NULL;
    map->capacity = 0;
    map->count = 0;
}

void cxpr_array_map_clear(cxpr_array_map* map) {
    for (size_t i = 0; i < map->capacity; i++) {
        free(map->entries[i].name);
        map->entries[i].name = NULL;
        cxpr_array_value_free(map->entries[i].value);
        map->entries[i].value = NULL;
    }
    map->count = 0;
}

bool cxpr_array_map_clone(cxpr_array_map* dst, const cxpr_array_map* src) {
    cxpr_array_map_init(dst);
    if (!src || !src->entries || src->count == 0) return true;

    dst->capacity = CXPR_HASHMAP_INITIAL_CAPACITY;
    dst->entries = (cxpr_array_map_entry*)calloc(dst->capacity, sizeof(cxpr_array_map_entry));
    if (!dst->entries) return false;

    for (size_t i = 0; i < src->capacity; i++) {
        cxpr_array_value* copy;
        cxpr_array_map_entry* slot;
        if (!src->entries[i].name) continue;
        copy = cxpr_array_value_new(src->entries[i].value->values,
                                    src->entries[i].value->count);
        if (!copy) return false;
        if ((double)(dst->count + 1) / dst->capacity > CXPR_HASHMAP_LOAD_FACTOR
            && !cxpr_array_map_grow(dst)) {
            cxpr_array_value_free(copy);
            return false;
        }
        slot = cxpr_array_map_find_slot(dst, src->entries[i].name);
        slot->name = cxpr_strdup(src->entries[i].name);
        slot->value = copy;
        if (!slot->name) return false;
        dst->count++;
    }
    return true;
}

static const cxpr_array_map_entry* cxpr_array_map_get(const cxpr_array_map* map,
                                                      const char* name) {
    cxpr_array_map_entry* slot = cxpr_array_map_find_slot(map, name);
    if (!slot || !slot->name) return NULL;
    return slot;
}

void cxpr_context_store_array(cxpr_array_map* map, const char* name,
                              const cxpr_array_value* value) {
    cxpr_array_map_entry* slot;
    cxpr_array_value* copy;

    if (!map || !name || !value) return;

    copy = cxpr_array_value_new(value->values, value->count);
    if (!copy) return;

    if (!map->entries) {
        map->capacity = CXPR_HASHMAP_INITIAL_CAPACITY;
        map->entries =
            (cxpr_array_map_entry*)calloc(map->capacity, sizeof(cxpr_array_map_entry));
        if (!map->entries) {
            map->capacity = 0;
            cxpr_array_value_free(copy);
            return;
        }
    }

    if ((double)(map->count + 1) / map->capacity > CXPR_HASHMAP_LOAD_FACTOR
        && !cxpr_array_map_grow(map)) {
        cxpr_array_value_free(copy);
        return;
    }

    slot = cxpr_array_map_find_slot(map, name);
    if (slot->name) {
        cxpr_array_value_free(slot->value);
        slot->value = copy;
        return;
    }

    slot->name = cxpr_strdup(name);
    if (!slot->name) {
        cxpr_array_value_free(copy);
        return;
    }
    slot->value = copy;
    map->count++;
}

void cxpr_context_remove_array(cxpr_array_map* map, const char* name) {
    cxpr_array_map_entry* slot;
    size_t index;

    if (!map || !name) return;
    slot = cxpr_array_map_find_slot(map, name);
    if (!slot || !slot->name) return;
    index = (size_t)(slot - map->entries);

    free(slot->name);
    slot->name = NULL;
    cxpr_array_value_free(slot->value);
    slot->value = NULL;
    if (map->count > 0u) map->count--;

    index = (index + 1u) % map->capacity;
    while (map->entries[index].name) {
        cxpr_array_map_entry displaced = map->entries[index];
        cxpr_array_map_entry* target;
        map->entries[index].name = NULL;
        map->entries[index].value = NULL;
        if (map->count > 0u) map->count--;
        target = cxpr_array_map_find_slot(map, displaced.name);
        *target = displaced;
        map->count++;
        index = (index + 1u) % map->capacity;
    }
}

const cxpr_array_value* cxpr_context_lookup_array_map(const cxpr_array_map* map,
                                                      const char* name) {
    const cxpr_array_map_entry* entry;

    if (!map || !name) return NULL;

    entry = cxpr_array_map_get(map, name);
    return entry ? entry->value : NULL;
}
