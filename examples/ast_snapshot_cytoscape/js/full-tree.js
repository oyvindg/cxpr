function buildFullTreeElements(doc) {
  const elements = { nodes: [], edges: [] };
  const edgeKeys = new Set();
  const snapshots = doc.snapshots || [];
  const flowNodes = doc.flow?.nodes || [];
  const flowEdges = doc.flow?.edges || [];
  const expressionFlowNodes = flowNodes.filter((node) => node.data.kind === "expression");
  const contextFlowNodes = flowNodes.filter((node) =>
    node.data.kind === "source" || node.data.kind === "param"
  );
  const contextCountByKind = new Map();
  contextFlowNodes.forEach((node) => {
    contextCountByKind.set(node.data.kind, (contextCountByKind.get(node.data.kind) || 0) + 1);
  });
  const contextIdByName = new Map(
    contextFlowNodes.map((node) => [node.data.name, `full_ctx_${node.data.id}`])
  );
  const literalIdByKey = new Map();
  const callIdBySource = new Map();
  const callSourceCounts = new Map();
  const usedContextGroups = new Set();

  expressionFlowNodes.forEach((node) => {
    const snapshot = snapshots[node.data.snapshot_index]?.snapshot;
    (snapshot?.elements?.nodes || []).forEach((astNode) => {
      if (astNode.data.id === "n0") return;
      if (astNode.data.kind !== "function_call" || !astNode.data.source) return;
      callSourceCounts.set(
        astNode.data.source,
        (callSourceCounts.get(astNode.data.source) || 0) + 1
      );
    });
  });

  contextFlowNodes.forEach((node) => {
    const groupId = `full_group_${node.data.kind}`;
    const useGroup = (contextCountByKind.get(node.data.kind) || 0) > 1;
    if (useGroup) usedContextGroups.add(node.data.kind);
    elements.nodes.push({
      data: {
        ...node.data,
        id: `full_ctx_${node.data.id}`,
        snapshot_index: undefined,
        parent: useGroup ? groupId : undefined
      }
    });
  });
  usedContextGroups.forEach((kind) => {
    elements.nodes.push({
      data: {
        id: `full_group_${kind}`,
        label: roleLabels[kind] || kind,
        display_label: roleLabels[kind] || kind,
        group_role: kind
      }
    });
  });

  expressionFlowNodes.forEach((node) => {
    const exprId = `full_expr_${node.data.index}`;
    const exprGroupId = `full_group_expr_${node.data.index}`;
    const snapshot = snapshots[node.data.snapshot_index]?.snapshot;
    const groupExpressions = expressionFlowNodes.length > 1;
    elements.nodes.push({
      data: {
        id: exprGroupId,
        label: node.data.name || `expr${node.data.index}`,
        display_label: node.data.name || `expr${node.data.index}`,
        group_role: "expression_detail",
        owner_snapshot_index: node.data.snapshot_index,
        parent: groupExpressions ? "full_group_expression" : undefined
      }
    });
    elements.nodes.push({
      data: {
        ...node.data,
        id: exprId,
        flow_id: node.data.id,
        display_label: expressionNodeDisplayLabel(node.data, snapshot),
        parent: exprGroupId
      }
    });

    if (!snapshot?.elements) return;
    const astElements = labelAstEdges(
      simplifyAstLabels(markFalseDescendants(snapshot.elements))
    );
    const astNodeById = new Map(
      (astElements.nodes || []).map((astNode) => [astNode.data.id, astNode.data])
    );
    const astNodeTargetById = new Map();
    (astElements.nodes || []).forEach((astNode) => {
      if (astNode.data.id === "n0") return;
      if (astNode.data.kind === "variable") {
        const paramName = (astNode.data.label || "").startsWith("$")
          ? astNode.data.label
          : `$${astNode.data.label}`;
        const paramId = contextIdByName.get(paramName);
        if (paramId) {
          astNodeTargetById.set(astNode.data.id, paramId);
          return;
        }
      }
      if (astNode.data.kind === "identifier" || astNode.data.kind === "lookback") {
        const sourceId = contextIdByName.get(astNode.data.source || astNode.data.label);
        if (sourceId) {
          astNodeTargetById.set(astNode.data.id, sourceId);
          return;
        }
      }
      if (astNode.data.kind === "number" || astNode.data.kind === "bool" || astNode.data.kind === "string") {
        const literalKey = `${astNode.data.kind}:${astNode.data.source || astNode.data.label}`;
        const existingId = literalIdByKey.get(literalKey);
        if (existingId) {
          astNodeTargetById.set(astNode.data.id, existingId);
          return;
        }
        const literalId = `full_literal_${literalIdByKey.size}`;
        literalIdByKey.set(literalKey, literalId);
        astNodeTargetById.set(astNode.data.id, literalId);
        elements.nodes.push({
          data: {
            ...astNode.data,
            id: literalId,
            owner_snapshot_index: node.data.snapshot_index,
            owner_name: node.data.name,
            owner_flow_id: node.data.id,
            name: undefined
          }
        });
        return;
      }
      if (astNode.data.kind === "function_call" && astNode.data.source) {
        const existingId = callIdBySource.get(astNode.data.source);
        if (existingId) {
          astNodeTargetById.set(astNode.data.id, existingId);
          return;
        }
        const callId = `full_call_${callIdBySource.size}`;
        callIdBySource.set(astNode.data.source, callId);
        astNodeTargetById.set(astNode.data.id, callId);
        elements.nodes.push({
          data: {
            ...astNode.data,
            id: callId,
            owner_snapshot_index: node.data.snapshot_index,
            owner_name: node.data.name,
            owner_flow_id: node.data.id,
            name: undefined,
            parent: (callSourceCounts.get(astNode.data.source) || 0) === 1
              ? exprGroupId
              : undefined
          }
        });
        return;
      }
      astNodeTargetById.set(astNode.data.id, `full_ast_${node.data.index}_${astNode.data.id}`);
      elements.nodes.push({
        data: {
          ...astNode.data,
          id: `full_ast_${node.data.index}_${astNode.data.id}`,
          owner_snapshot_index: node.data.snapshot_index,
          owner_name: node.data.name,
          owner_flow_id: node.data.id,
          name: undefined,
          parent: exprGroupId
        }
      });
    });
    (astElements.edges || []).forEach((astEdge) => {
      const sourceAstNode = astNodeById.get(astEdge.data.source);
      let source = astEdge.data.source === "n0"
        ? exprId
        : astNodeTargetById.get(astEdge.data.source);
      let target = astEdge.data.target === "n0"
        ? exprId
        : astNodeTargetById.get(astEdge.data.target);
      if (sourceAstNode?.kind === "lookback") {
        const tmp = source;
        source = target;
        target = tmp;
      }
      if (!source || !target || source === target) return;
      const edgeKey = `${source}->${target}`;
      if (edgeKeys.has(edgeKey)) return;
      edgeKeys.add(edgeKey);
      elements.edges.push({
        data: {
          ...astEdge.data,
          id: `full_ast_e_${node.data.index}_${astEdge.data.id}`,
          source,
          target
        }
      });
    });
  });

  flowEdges.forEach((edge) => {
    const source = flowNodes.find((node) => node.data.id === edge.data.source)?.data;
    const target = flowNodes.find((node) => node.data.id === edge.data.target)?.data;
    if (!source || !target || target.kind !== "expression") return;
    const sourceId = source.kind === "expression"
      ? `full_expr_${source.index}`
      : `full_ctx_${source.id}`;
    const targetId = `full_expr_${target.index}`;
    const edgeKey = `${sourceId}->full_expr_${target.index}`;
    if (edgeKeys.has(edgeKey)) return;
    if ((source.kind === "source" || source.kind === "param") &&
        (edgeKeys.has(`${targetId}->${sourceId}`) || edgeKeys.has(`${sourceId}->${targetId}`))) {
      return;
    }
    edgeKeys.add(edgeKey);
    elements.edges.push({
      data: {
        id: `full_flow_${edge.data.id}`,
        source: sourceId,
        target: targetId,
        role: edge.data.source_name || source.name,
        context_edge: source.kind === "source" || source.kind === "param" ? "true" : undefined,
        expression_edge: source.kind === "expression" ? "true" : undefined
      }
    });
  });

  {
    const connected = new Set();
    if (expressionFlowNodes.length > 1) {
      elements.nodes.push({
        data: {
          id: "full_group_expression",
          label: roleLabels.expression,
          display_label: roleLabels.expression,
          group_role: "expression"
        }
      });
    }
    groupAstAuxiliaryNodes(elements, "full_group");
    elements.edges.forEach((edge) => {
      connected.add(edge.data.source);
      connected.add(edge.data.target);
    });
    elements.nodes = elements.nodes.filter((node) =>
      node.data.group_role || connected.has(node.data.id)
    );
  }
  addExpressionTitleGroups(elements, "full_expr_title");
  return maybeBundleContextEdges(elements, "full", 1, ["source", "param", "number", "bool", "string"]);
}

function arrangeFullContextGroups(cy) {
  const expressionBox = cy.nodes("node[kind = 'expression']").boundingBox({ includeLabels: false });
  arrangeContextGroups(cy, expressionBox, "full_group_source", "full_group_param");
}

function arrangeAuxiliaryGroups(cy, groupPrefix, anchorSelector) {
  const anchorBox = cy.nodes(anchorSelector).boundingBox();
  const left = Number.isFinite(anchorBox.x1) ? anchorBox.x1 : -120;
  let top = Number.isFinite(anchorBox.y1) ? anchorBox.y1 - 120 : -160;
  const auxiliaryRoles = new Set(["expression", "expression_detail", "function", "sample"]);
  const groups = cy.nodes("node[group_role]").filter((node) =>
    node.id().startsWith(groupPrefix) && auxiliaryRoles.has(node.data("group_role"))
  );

  groups.forEach((group) => {
    let nodes = cy.nodes(`[parent = '${group.id()}']`);
    if (nodes.length === 0) return;
    const isExpressionGroup = group.data("group_role") === "expression";
    const isExpressionDetailGroup = group.data("group_role") === "expression_detail";
    const cols = Math.min(isExpressionDetailGroup ? 2 : 3, Math.max(1, nodes.length));
    const colWidth = isExpressionGroup ? 190 : (isExpressionDetailGroup ? 150 : 160);
    const rowHeight = isExpressionGroup ? 72 : (isExpressionDetailGroup ? 118 : 88);
    nodes.forEach((node, index) => {
      node.position({
        x: left + (index % cols) * colWidth,
        y: top + Math.floor(index / cols) * rowHeight
      });
    });
    top += Math.ceil(nodes.length / cols) * rowHeight + 60;
  });
}

function arrangeContextGroups(cy, expressionBox, sourceGroupId, paramGroupId) {
  const sourceNodes = cy.nodes(`[parent = '${sourceGroupId}']`);
  const paramNodes = cy.nodes(`[parent = '${paramGroupId}']`);
  const left = Number.isFinite(expressionBox.x1) ? expressionBox.x1 - 250 : -330;
  let top = Number.isFinite(expressionBox.y1) ? expressionBox.y1 : 0;

  function placeGrid(nodes, startY) {
    const cols = 2;
    const colWidth = 150;
    const rowHeight = 92;
    nodes.forEach((node, index) => {
      node.position({
        x: left + (index % cols) * colWidth,
        y: startY + Math.floor(index / cols) * rowHeight
      });
    });
    return startY + Math.ceil(nodes.length / cols) * rowHeight + 70;
  }

  if (sourceNodes.length > 0) top = placeGrid(sourceNodes, top);
  if (paramNodes.length > 0) placeGrid(paramNodes, top);
}

function tintEdgesByTarget(cy) {
  function colorForNode(node) {
    const data = node.data();
    if (data.kind === "param" || data.kind === "variable") return tokenColors.parameter;
    if (data.kind === "source" || data.kind === "identifier" || data.kind === "lookback") return tokenColors.property;
    if (data.role === "function") return tokenColors.functionNode;
    if (data.state === "true") return tokenColors.true;
    if (data.state === "false") return tokenColors.false;
    if (data.state === "number" || data.state === "value") return tokenColors.number;
    return "";
  }

  function colorForEdge(edge) {
    const source = edge.source();
    const target = edge.target();
    const edgeData = edge.data();
    const targetData = target.data();
    if (edgeData.bundle_color_kind === "param") return tokenColors.parameter;
    if (edgeData.bundle_color_kind === "source") return tokenColors.property;
    if (edgeData.bundle_color_kind === "number" ||
        edgeData.bundle_color_kind === "bool" ||
        edgeData.bundle_color_kind === "string") return tokenColors.number;
    const labelRepresentsSource =
      !!edgeData.source_name ||
      (edgeData.id || "").startsWith("full_flow_") ||
      (targetData.kind === "lookback" &&
        (edgeData.role === "source" || edgeData.role === "index"));
    return labelRepresentsSource
      ? colorForNode(source) || colorForNode(target)
      : colorForNode(target) || colorForNode(source);
  }

  cy.scratch("_edgeColorForEdge", colorForEdge);

  cy.edges().forEach((edge) => {
    const color = colorForEdge(edge);

    if (!color) return;
    edge.style({
      "line-color": color,
      "source-arrow-color": color,
      "target-arrow-color": color,
      "color": color
    });
  });
}

function installDimmedHover(cy) {
  void cy;
}
