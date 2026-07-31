#include <cxpr/debug_map.h>

#include <string.h>

static int cxpr_debug_map_has_node(
    const cxpr_debug_map* map,
    cxpr_debug_node_id id) {
    size_t i;
    for (i = 0u; i < map->node_count; ++i) {
        if (map->nodes[i].id == id) return 1;
    }
    return 0;
}

int cxpr_debug_map_validate(const cxpr_debug_map* map) {
    size_t i;
    size_t j;

    if (!map || map->abi_version != CXPR_DEBUG_MAP_ABI_VERSION ||
        !map->model_name || !map->model_name[0] ||
        (map->node_count > 0u && !map->nodes) ||
        (map->output_count > 0u && !map->outputs)) {
        return 0;
    }
    for (i = 0u; i < map->node_count; ++i) {
        const cxpr_debug_node* node = &map->nodes[i];
        if (node->id == 0u || !node->name || !node->name[0] ||
            !node->canonical_source ||
            (node->dependency_count > 0u && !node->dependencies)) {
            return 0;
        }
        for (j = i + 1u; j < map->node_count; ++j) {
            if (node->id == map->nodes[j].id) return 0;
        }
        for (j = 0u; j < node->dependency_count; ++j) {
            if (!cxpr_debug_map_has_node(map, node->dependencies[j])) return 0;
        }
    }
    for (i = 0u; i < map->output_count; ++i) {
        const cxpr_debug_output* output = &map->outputs[i];
        if (output->id == 0u || !output->name || !output->name[0] ||
            !cxpr_debug_map_has_node(map, output->node_id)) {
            return 0;
        }
        for (j = i + 1u; j < map->output_count; ++j) {
            if (output->id == map->outputs[j].id) return 0;
        }
    }
    return 1;
}
