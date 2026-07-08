function markFalseDescendants(elements) {
  return JSON.parse(JSON.stringify(elements || { nodes: [], edges: [] }));
}

function cloneElements(elements) {
  return JSON.parse(JSON.stringify(elements || { nodes: [], edges: [] }));
}

function bundleContextEdges(elements, prefix, minEdges = 2, kinds = ["source", "param"]) {
  const cloned = cloneElements(elements);
  const nodes = cloned.nodes || [];
  const edges = cloned.edges || [];
  const nodeById = new Map(nodes.map((node) => [node.data.id, node.data]));
  const contextEdgesByNode = new Map();
  const bundleKinds = new Set(kinds);

  edges.forEach((edge) => {
    const source = nodeById.get(edge.data.source);
    const target = nodeById.get(edge.data.target);
    let contextId = null;
    let direction = "out";

    if (bundleKinds.has(source?.kind)) {
      contextId = edge.data.source;
      direction = "out";
    } else if (bundleKinds.has(target?.kind)) {
      contextId = edge.data.target;
      direction = "in";
    }
    if (!contextId) return;
    if (!contextEdgesByNode.has(contextId)) contextEdgesByNode.set(contextId, []);
    contextEdgesByNode.get(contextId).push({ edge, direction });
  });

  contextEdgesByNode.forEach((group, contextId) => {
    if (group.length <= minEdges) return;
    const context = nodeById.get(contextId);
    const hubId = `${prefix}_bundle_${safeId(contextId)}`;
    const label = connectorLabelForNode(context, group.map((item) => item.edge));
    nodes.push({
      data: {
        id: hubId,
        label,
        display_label: label,
        connector: "true",
        connector_kind: context.kind,
        connector_for: contextId,
        kind: "connector"
      }
    });

    group.forEach(({ edge }) => {
      edge.data._bundled = "true";
    });
    edges.push({
      data: {
        id: `${hubId}_in`,
        source: contextId,
        target: hubId,
        role: "",
        source_name: "",
        context_edge: "true",
        bundle_edge: "true",
        bundle_color_kind: context.kind,
        curve_distance: 24,
        curve_weight: 0.5
      }
    });
    group.forEach(({ edge, direction }, index) => {
      const otherId = direction === "out" ? edge.data.target : edge.data.source;
      const spread = (index % 2 === 0 ? 1 : -1) * (36 + Math.floor(index / 2) * 18);
      edges.push({
        data: {
          id: `${edge.data.id}_bundle_${index}`,
          source: hubId,
          target: otherId,
          role: "",
          context_edge: "true",
          bundle_edge: "true",
          bundle_color_kind: context.kind,
          curve_distance: spread,
          curve_weight: index % 3 === 0 ? 0.22 : 0.34
        }
      });
    });
  });

  cloned.edges = edges.filter((edge) => edge.data._bundled !== "true");
  return cloned;
}

function maybeBundleContextEdges(elements, prefix, minEdges = 2, kinds = ["source", "param"]) {
  if (document.getElementById("connector-mode")?.value === "off") {
    return cloneElements(elements);
  }
  return bundleContextEdges(elements, prefix, minEdges, kinds);
}

function connectorLabelForNode(node, edges) {
  if (node.display_label) return node.display_label;
  const name = node.name || node.label || edgeLabelForGroup(edges);
  const resolved = node.resolved || "";
  const value = node.value || "";
  if (resolved && value && resolved !== value) return `${name}\n= ${resolved}\n= ${value}`;
  const result = resolved || value;
  if (!result || result === name) return name;
  return `${name}\n= ${result}`;
}

function edgeLabelForGroup(group) {
  const first = group && group[0] && group[0].data;
  return first?.source_name || first?.role || "";
}

function safeId(value) {
  return String(value || "id").replace(/[^a-zA-Z0-9_]+/g, "_");
}

function expressionNodeDisplayLabel(data, snapshot = null) {
  const expression = snapshot?.expression || data.expression || "";
  const resolved = snapshot?.resolved || data.resolved || "";
  const value = snapshot?.result || data.value || "";
  const finalValue = value && resolved && value !== resolved ? `\n= ${value}` : "";
  if (expression && resolved) return `${expression}\n= ${resolved}${finalValue}`;
  if (expression && value) return `${expression}\n= ${value}`;
  if (expression) return expression;
  return resolved || value || "";
}

function addExpressionTitleGroups(elements, prefix) {
  const nodes = elements.nodes || [];
  const titleIds = new Set(nodes.map((node) => node.data.id));
  nodes
    .filter((node) => node.data.kind === "expression" && node.data.name)
    .forEach((node) => {
      const titleId = `${prefix}_${safeId(node.data.id)}`;
      if (titleIds.has(titleId)) return;
      const parent = node.data.parent;
      node.data.parent = titleId;
      nodes.push({
        data: {
          id: titleId,
          label: node.data.name,
          display_label: node.data.name,
          group_role: "expression_title",
          parent
        }
      });
      titleIds.add(titleId);
    });
  return elements;
}

function dedupeAstLeafNodes(elements) {
  const leafKinds = new Set(["identifier", "lookback", "variable", "number", "bool", "string"]);
  const nodes = elements.nodes || [];
  const edges = elements.edges || [];
  const canonicalByKey = new Map();
  const targetById = new Map();
  const reuseCountById = new Map();
  const keepNodeIds = new Set();
  const nodeById = new Map(nodes.map((node) => [node.data.id, node.data]));
  const parentEdgeByTarget = new Map(edges.map((edge) => [edge.data.target, edge.data]));

  function leafKey(data) {
    if (!leafKinds.has(data.kind) || data.id === "n0") return "";
    if ((data.role || "").startsWith("sample[")) return "";
    const parentEdge = parentEdgeByTarget.get(data.id);
    const parent = parentEdge ? nodeById.get(parentEdge.source) : null;
    const parentKey = parent ? (parent.source || parent.label || parent.id || "") : "";
    const label = data.source || data.label || data.display_label || "";
    const value = data.resolved || data.value || "";
    return `${parentKey}:${data.kind}:${label}:${value}`;
  }

  nodes.forEach((node) => {
    const key = leafKey(node.data);
    if (!key) {
      keepNodeIds.add(node.data.id);
      targetById.set(node.data.id, node.data.id);
      return;
    }
    const existingId = canonicalByKey.get(key);
    if (existingId) {
      targetById.set(node.data.id, existingId);
      reuseCountById.set(existingId, (reuseCountById.get(existingId) || 1) + 1);
      return;
    }
    canonicalByKey.set(key, node.data.id);
    keepNodeIds.add(node.data.id);
    targetById.set(node.data.id, node.data.id);
    reuseCountById.set(node.data.id, 1);
  });

  elements.nodes = nodes.filter((node) => keepNodeIds.has(node.data.id));
  elements.nodes.forEach((node) => {
    const count = reuseCountById.get(node.data.id) || 1;
    if (count <= 1) return;
    node.data.reuse_count = count;
  });

  const edgeKeys = new Set();
  elements.edges = edges
    .map((edge) => ({
      data: {
        ...edge.data,
        source: targetById.get(edge.data.source) || edge.data.source,
        target: targetById.get(edge.data.target) || edge.data.target
      }
    }))
    .filter((edge) => {
      if (edge.data.source === edge.data.target) return false;
      const key = `${edge.data.source}->${edge.data.target}:${edge.data.role || ""}`;
      if (edgeKeys.has(key)) return false;
      edgeKeys.add(key);
      return true;
    });

  return elements;
}

function groupMatchingNodes(elements, groupId, groupRole, predicate) {
  const matchingNodes = (elements.nodes || []).filter((node) => predicate(node.data));
  if (matchingNodes.length <= 1) return elements;
  matchingNodes.forEach((node) => {
    node.data.parent = groupId;
  });
  elements.nodes.push({
    data: {
      id: groupId,
      label: roleLabels[groupRole] || groupRole,
      display_label: roleLabels[groupRole] || groupRole,
      group_role: groupRole
    }
  });
  return elements;
}

function groupSampleNodes(elements, prefix) {
  const nodes = elements.nodes || [];
  const edges = elements.edges || [];
  const nodeById = new Map(nodes.map((node) => [node.data.id, node.data]));
  const sampleNodes = nodes.filter((node) => (node.data.role || "").startsWith("sample["));
  if (sampleNodes.length <= 1) return elements;

  const parentIds = new Set();
  edges.forEach((edge) => {
    if ((nodeById.get(edge.data.target)?.role || "").startsWith("sample[")) {
      parentIds.add(edge.data.source);
    }
  });
  const parentNames = [...parentIds]
    .map((id) => nodeById.get(id))
    .filter((data) => data && data.kind === "function_call")
    .map((data) => data.label || data.source)
    .filter(Boolean);
  const label = parentNames.length === 1 && parentIds.size === 1
    ? parentNames[0]
    : roleLabels.sample;

  sampleNodes.forEach((node) => {
    node.data.parent = `${prefix}_sample`;
  });
  elements.nodes.push({
    data: {
      id: `${prefix}_sample`,
      label,
      display_label: label,
      group_role: "sample"
    }
  });
  return elements;
}

function groupAstAuxiliaryNodes(elements, prefix) {
  groupMatchingNodes(
    elements,
    `${prefix}_function`,
    "function",
    (data) => data.role === "function"
  );
  groupSampleNodes(elements, prefix);
  return elements;
}

function simplifySampleLabels(elements) {
  (elements.nodes || []).forEach((node) => {
    const data = node.data;
    if (!(data.role || "").startsWith("sample[")) return;
    if (data.display_label) {
      data.display_label = data.display_label.replace(/^sample\[\d+\]:\s*/, "");
    }
  });
  return elements;
}

function simplifyComparatorLabels(elements) {
  if (document.getElementById("comparator-mode")?.value !== "operator") return elements;
  const comparators = new Set([">", ">=", "<", "<=", "==", "!="]);
  (elements.nodes || []).forEach((node) => {
    const data = node.data;
    if (data.kind !== "binary_op" || !comparators.has(data.label)) return;
    const expression = data.source || "";
    const calculation = data.resolved && data.resolved !== data.value
      ? data.resolved
      : "";
    const result = data.value || "";
    data.display_label = [
      expression,
      calculation && result ? `= ${calculation}\n= ${result}` :
        (calculation ? `= ${calculation}` : ""),
      !calculation && result ? `= ${result}` : ""
    ].filter(Boolean).join("\n");
  });
  return elements;
}

function simplifyAstLabels(elements) {
  return simplifyComparatorLabels(simplifySampleLabels(elements));
}

function prepareAstElements(snapshot, name) {
  const elements = simplifyAstLabels(
    dedupeAstLeafNodes(markFalseDescendants(snapshot.elements))
  );
  const root = (elements.nodes || []).find((node) => node.data.id === "n0");
  if (root && name && snapshot.expression) {
    root.data.display_label = expressionNodeDisplayLabel(root.data, snapshot);
    root.data.parent = "ast_root_title";
    elements.nodes.push({
      data: {
        id: "ast_root_title",
        label: name,
        display_label: name,
        group_role: "expression_title"
      }
    });
  }
  return labelAstEdges(annotateAstChildOrder(groupAstAuxiliaryNodes(elements, "group")));
}

function annotateAstChildOrder(elements) {
  const nodeOrderById = new Map();
  const edges = elements.edges || [];

  edges.forEach((edge, index) => {
    const order = astChildRoleOrder(edge.data.role, index);
    edge.data.layout_order = order;
    if (!nodeOrderById.has(edge.data.target)) {
      nodeOrderById.set(edge.data.target, order);
    }
  });

  (elements.nodes || []).forEach((node, index) => {
    node.data.layout_order = nodeOrderById.get(node.data.id)
      ?? node.data.numeric_id
      ?? index;
  });

  return elements;
}

function astChildRoleOrder(role, fallback) {
  const value = String(role || "");
  if (value === "function") return -20;
  if (value === "source") return -10;
  if (value === "left") return 0;
  if (value === "right") return 10;
  if (value === "index") return 20;
  const sample = value.match(/^sample\[(\d+)\]/);
  if (sample) return 30 + Number(sample[1]);
  const argument = value.match(/^(argument|arg)\[(\d+)\]/);
  if (argument) return 40 + Number(argument[2]);
  return 100 + fallback;
}

function layoutOrderSort(a, b) {
  return (a.data("layout_order") ?? 0) - (b.data("layout_order") ?? 0);
}

function reverseLayoutOrderSort(a, b) {
  return layoutOrderSort(b, a);
}

function labelAstEdges(elements) {
  const cloned = cloneElements(elements);
  const nodeById = new Map((cloned.nodes || []).map((node) => [node.data.id, node.data]));
  cloned.edges = (cloned.edges || []).map((edge) => ({
    data: {
      ...edge.data,
      role: document.getElementById("ast-flow-mode")?.value === "name"
        ? astEdgeNameLabel(edge.data, nodeById)
        : astEdgeRoleLabel(edge.data, nodeById)
    }
  }));
  return cloned;
}

function astEdgeRoleLabel(edge, nodeById) {
  const role = edge.role || "";
  const target = nodeById.get(edge.target);
  if (!target || !role) return role;
  const positionalSource = astPositionalArgSourceLabel(edge, target);
  if (positionalSource) return positionalSource;
  const targetLabel = target.label ? readableAstLabel(target.label) : "";
  if (targetLabel && readableAstLabel(role) === targetLabel) return "";
  return role;
}

function astEdgeNameLabel(edge, nodeById) {
  const source = nodeById.get(edge.source);
  const target = nodeById.get(edge.target);
  if (!target) return edge.role || "";
  const positionalSource = astPositionalArgSourceLabel(edge, target);
  if (positionalSource) return positionalSource;
  if (target.kind === "binary_op" || target.kind === "unary_op") {
    const binder = compactOperatorLabel(source?.label || "");
    return binder ? `${binder} ${edge.role || ""}`.trim() : (edge.role || "");
  }
  if (target.kind === "lookback" && target.source) return readableAstLabel(target.source);
  if (target.label) return readableAstLabel(target.label);
  if (target.name) return readableAstLabel(target.name);
  if (target.source) return readableAstLabel(target.source);
  return edge.role || "";
}

function astPositionalArgSourceLabel(edge, target) {
  if (!/^arg\d+$/.test(edge.role || "")) return "";
  if (!["identifier", "lookback", "variable"].includes(target.kind)) return "";
  const source = target.source || target.resolved || target.label || "";
  if (!source || /^arg\d+$/.test(source)) return "";
  return readableAstLabel(source);
}

function compactOperatorLabel(label) {
  const operatorLabels = {
    "+": "add",
    "-": "sub",
    "*": "mul",
    "/": "div",
    "%": "mod",
    "^": "pow",
    ">": "gt",
    ">=": "gte",
    "<": "lt",
    "<=": "lte",
    "==": "eq",
    "!=": "neq",
    "and": "and",
    "or": "or",
    "not": "not"
  };
  return operatorLabels[label] || "";
}

function readableAstLabel(label) {
  const operatorLabels = {
    "+": "plus",
    "-": "minus",
    "*": "multiply",
    "/": "divide",
    "%": "modulo",
    "^": "power",
    ">": "greater than",
    ">=": "greater or equal",
    "<": "less than",
    "<=": "less or equal",
    "==": "equal",
    "!=": "not equal"
  };
  return operatorLabels[label] || label;
}


function cytoscapeFlowLayout(options = {}) {
  return {
    name: "dagre",
    rankDir: options.rankDir || "LR",
    nodeSep: options.nodeSep || 44,
    edgeSep: options.edgeSep || 18,
    rankSep: options.rankSep || 116,
    padding: options.padding || 48,
    fit: true,
    animate: false,
    stop: options.stop,
    sort: options.sort
  };
}

function placeOutputGroupRight(cy) {
  const outputs = cy.nodes("node[kind = 'output']").sort((a, b) =>
    String(a.data("label") || a.id()).localeCompare(String(b.data("label") || b.id()))
  );
  if (outputs.empty()) return;

  const anchors = cy.nodes().filter((node) =>
    !node.isParent() &&
    node.data("kind") !== "output" &&
    node.data("connector") !== "true"
  );
  if (anchors.empty()) return;

  const bounds = anchors.boundingBox({ includeLabels: false });
  const outputWidth = Math.max(...outputs.map((node) => node.outerWidth()), 96);
  const outputHeight = Math.max(...outputs.map((node) => node.outerHeight()), 42);
  const columns = outputs.length > 6 ? 2 : 1;
  const rows = Math.ceil(outputs.length / columns);
  const columnGap = Math.max(outputWidth + 28, 130);
  const rowGap = Math.max(outputHeight + 18, 64);
  const startX = bounds.x2 + 150;
  const startY = bounds.y1 + (bounds.h - (rows - 1) * rowGap) / 2;

  outputs.forEach((node, index) => {
    const column = Math.floor(index / rows);
    const row = index % rows;
    node.position({
      x: startX + column * columnGap,
      y: startY + row * rowGap
    });
  });
}

function flattenCompoundElements(elements, options = {}) {
  const cloned = cloneElements(elements);
  const keepGroupRoles = new Set(options.keepGroupRoles || []);
  const groupRoleById = new Map(
    (cloned.nodes || [])
      .filter((node) => node.data.group_role)
      .map((node) => [node.data.id, node.data.group_role])
  );
  cloned.nodes = (cloned.nodes || [])
    .filter((node) => !node.data.group_role || keepGroupRoles.has(node.data.group_role))
    .map((node) => {
      const parentRole = groupRoleById.get(node.data.parent);
      if (!keepGroupRoles.has(parentRole)) {
        delete node.data.parent;
      }
      return node;
    });
  return cloned;
}
