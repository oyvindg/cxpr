#include <cxpr/bulk_snapshot.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { CXPR_BULK_SNAPSHOT_HEADER_SIZE = 56 };

static const unsigned char cxpr_bulk_snapshot_magic[8] = {
    'C', 'X', 'P', 'R', 'B', 'S', 'N', 'P'
};

static void write_u32_le(unsigned char* dst, uint32_t value) {
    size_t i;
    for (i = 0u; i < 4u; ++i) dst[i] = (unsigned char)(value >> (i * 8u));
}

static void write_u64_le(unsigned char* dst, uint64_t value) {
    size_t i;
    for (i = 0u; i < 8u; ++i) dst[i] = (unsigned char)(value >> (i * 8u));
}

static uint32_t read_u32_le(const unsigned char* src) {
    uint32_t value = 0u;
    size_t i;
    for (i = 0u; i < 4u; ++i) value |= (uint32_t)src[i] << (i * 8u);
    return value;
}

static uint64_t read_u64_le(const unsigned char* src) {
    uint64_t value = 0u;
    size_t i;
    for (i = 0u; i < 8u; ++i) value |= (uint64_t)src[i] << (i * 8u);
    return value;
}

static uint64_t hash_bytes(uint64_t hash, const void* data, size_t size) {
    const unsigned char* bytes = (const unsigned char*)data;
    size_t i;
    for (i = 0u; i < size; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t hash_u64(uint64_t hash, uint64_t value) {
    unsigned char bytes[8];
    write_u64_le(bytes, value);
    return hash_bytes(hash, bytes, sizeof(bytes));
}

static uint64_t hash_name(uint64_t hash, const char* name) {
    const size_t size = strlen(name);
    hash = hash_u64(hash, (uint64_t)size);
    return hash_bytes(hash, name, size);
}

/* Public descriptor schema plus state size define the generated state ABI. */
static uint64_t descriptor_layout_hash(
    const cxpr_generated_model_descriptor* descriptor,
    uint64_t state_layout_id) {
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;
    hash = hash_name(hash, descriptor->name);
    hash = hash_u64(hash, state_layout_id);
    hash = hash_u64(hash, descriptor->abi_version);
    hash = hash_u64(hash, descriptor->state_size());
    hash = hash_u64(hash, descriptor->input_count);
    for (i = 0u; i < descriptor->input_count; ++i) {
        hash = hash_name(hash, descriptor->input_names[i]);
        hash = hash_u64(hash, descriptor->input_types[i]);
    }
    hash = hash_u64(hash, descriptor->output_count);
    for (i = 0u; i < descriptor->output_count; ++i) {
        hash = hash_name(hash, descriptor->output_names[i]);
        hash = hash_u64(hash, descriptor->output_types[i]);
    }
    hash = hash_u64(hash, descriptor->param_count);
    for (i = 0u; i < descriptor->param_count; ++i) {
        hash = hash_name(hash, descriptor->param_names[i]);
        hash = hash_u64(hash, descriptor->param_types[i]);
    }
    return hash;
}

static uint64_t payload_checksum(const unsigned char* data, size_t size) {
    return hash_bytes(UINT64_C(14695981039346656037), data, size);
}

const char* cxpr_bulk_snapshot_status_message(cxpr_bulk_snapshot_status status) {
    switch (status) {
    case CXPR_BULK_SNAPSHOT_OK: return "bulk snapshot is valid";
    case CXPR_BULK_SNAPSHOT_INVALID_ARGUMENT: return "invalid bulk snapshot argument";
    case CXPR_BULK_SNAPSHOT_INVALID_DESCRIPTOR: return "invalid generated model descriptor";
    case CXPR_BULK_SNAPSHOT_INVALID_FORMAT: return "invalid bulk snapshot format";
    case CXPR_BULK_SNAPSHOT_UNSUPPORTED_VERSION: return "unsupported bulk snapshot version";
    case CXPR_BULK_SNAPSHOT_LAYOUT_MISMATCH: return "bulk snapshot model layout does not match";
    case CXPR_BULK_SNAPSHOT_ELEMENT_COUNT_MISMATCH: return "bulk snapshot element count does not match";
    case CXPR_BULK_SNAPSHOT_STATE_STRIDE_TOO_SMALL: return "bulk snapshot state stride is too small";
    case CXPR_BULK_SNAPSHOT_CHECKSUM_MISMATCH: return "bulk snapshot checksum does not match";
    case CXPR_BULK_SNAPSHOT_ALLOCATION_FAILED: return "bulk snapshot allocation failed";
    default: return "unknown bulk snapshot error";
    }
}

cxpr_bulk_snapshot_status cxpr_bulk_snapshot_create(
    const cxpr_generated_model_descriptor* descriptor,
    uint64_t state_layout_id,
    const cxpr_bulk_view* view,
    cxpr_bulk_snapshot* out_snapshot) {
    unsigned char* data;
    unsigned char* payload;
    size_t state_size;
    size_t payload_size;
    size_t i;
    if (!view || !out_snapshot || state_layout_id == 0u)
        return CXPR_BULK_SNAPSHOT_INVALID_ARGUMENT;
    out_snapshot->data = NULL;
    out_snapshot->size = 0u;
    if (!cxpr_generated_model_descriptor_abi_valid(descriptor))
        return CXPR_BULK_SNAPSHOT_INVALID_DESCRIPTOR;
    state_size = descriptor->state_size();
    if (state_size && (!view->states || view->state_stride < state_size))
        return CXPR_BULK_SNAPSHOT_STATE_STRIDE_TOO_SMALL;
    if (state_size && view->element_count > SIZE_MAX / state_size)
        return CXPR_BULK_SNAPSHOT_INVALID_ARGUMENT;
    payload_size = state_size * view->element_count;
    if (payload_size > SIZE_MAX - CXPR_BULK_SNAPSHOT_HEADER_SIZE)
        return CXPR_BULK_SNAPSHOT_INVALID_ARGUMENT;
    data = (unsigned char*)malloc(CXPR_BULK_SNAPSHOT_HEADER_SIZE + payload_size);
    if (!data) return CXPR_BULK_SNAPSHOT_ALLOCATION_FAILED;
    payload = data + CXPR_BULK_SNAPSHOT_HEADER_SIZE;
    for (i = 0u; state_size && i < view->element_count; ++i) {
        memcpy(payload + i * state_size,
               (const unsigned char*)view->states + i * view->state_stride,
               state_size);
    }
    memcpy(data, cxpr_bulk_snapshot_magic, sizeof(cxpr_bulk_snapshot_magic));
    write_u32_le(data + 8u, CXPR_BULK_SNAPSHOT_FORMAT_VERSION);
    write_u32_le(data + 12u, CXPR_BULK_SNAPSHOT_HEADER_SIZE);
    write_u32_le(data + 16u, descriptor->abi_version);
    write_u32_le(data + 20u, 0u);
    write_u64_le(data + 24u, descriptor_layout_hash(descriptor, state_layout_id));
    write_u64_le(data + 32u, state_size);
    write_u64_le(data + 40u, view->element_count);
    write_u64_le(data + 48u, payload_checksum(payload, payload_size));
    out_snapshot->data = data;
    out_snapshot->size = CXPR_BULK_SNAPSHOT_HEADER_SIZE + payload_size;
    return CXPR_BULK_SNAPSHOT_OK;
}

cxpr_bulk_snapshot_status cxpr_bulk_snapshot_restore(
    const cxpr_generated_model_descriptor* descriptor,
    uint64_t state_layout_id,
    const cxpr_bulk_snapshot* snapshot,
    const cxpr_bulk_view* view) {
    const unsigned char* payload;
    uint64_t stored_state_size;
    uint64_t stored_count;
    size_t expected_payload_size;
    size_t state_size;
    size_t i;
    if (!snapshot || !view || !snapshot->data || state_layout_id == 0u)
        return CXPR_BULK_SNAPSHOT_INVALID_ARGUMENT;
    if (!cxpr_generated_model_descriptor_abi_valid(descriptor))
        return CXPR_BULK_SNAPSHOT_INVALID_DESCRIPTOR;
    if (snapshot->size < CXPR_BULK_SNAPSHOT_HEADER_SIZE ||
        memcmp(snapshot->data, cxpr_bulk_snapshot_magic,
               sizeof(cxpr_bulk_snapshot_magic)) != 0 ||
        read_u32_le(snapshot->data + 12u) != CXPR_BULK_SNAPSHOT_HEADER_SIZE)
        return CXPR_BULK_SNAPSHOT_INVALID_FORMAT;
    if (read_u32_le(snapshot->data + 8u) != CXPR_BULK_SNAPSHOT_FORMAT_VERSION)
        return CXPR_BULK_SNAPSHOT_UNSUPPORTED_VERSION;
    if (read_u32_le(snapshot->data + 16u) != descriptor->abi_version ||
        read_u64_le(snapshot->data + 24u) !=
            descriptor_layout_hash(descriptor, state_layout_id))
        return CXPR_BULK_SNAPSHOT_LAYOUT_MISMATCH;
    state_size = descriptor->state_size();
    stored_state_size = read_u64_le(snapshot->data + 32u);
    stored_count = read_u64_le(snapshot->data + 40u);
    if (stored_state_size != state_size)
        return CXPR_BULK_SNAPSHOT_LAYOUT_MISMATCH;
    if (stored_count != view->element_count)
        return CXPR_BULK_SNAPSHOT_ELEMENT_COUNT_MISMATCH;
    if (state_size && (!view->states || view->state_stride < state_size))
        return CXPR_BULK_SNAPSHOT_STATE_STRIDE_TOO_SMALL;
    if (state_size && view->element_count > SIZE_MAX / state_size)
        return CXPR_BULK_SNAPSHOT_INVALID_FORMAT;
    expected_payload_size = state_size * view->element_count;
    if (expected_payload_size > SIZE_MAX - CXPR_BULK_SNAPSHOT_HEADER_SIZE ||
        snapshot->size != CXPR_BULK_SNAPSHOT_HEADER_SIZE + expected_payload_size)
        return CXPR_BULK_SNAPSHOT_INVALID_FORMAT;
    payload = snapshot->data + CXPR_BULK_SNAPSHOT_HEADER_SIZE;
    if (read_u64_le(snapshot->data + 48u) !=
        payload_checksum(payload, expected_payload_size))
        return CXPR_BULK_SNAPSHOT_CHECKSUM_MISMATCH;
    for (i = 0u; state_size && i < view->element_count; ++i) {
        memcpy((unsigned char*)view->states + i * view->state_stride,
               payload + i * state_size, state_size);
    }
    return CXPR_BULK_SNAPSHOT_OK;
}

void cxpr_bulk_snapshot_free(cxpr_bulk_snapshot* snapshot) {
    if (!snapshot) return;
    free(snapshot->data);
    snapshot->data = NULL;
    snapshot->size = 0u;
}
