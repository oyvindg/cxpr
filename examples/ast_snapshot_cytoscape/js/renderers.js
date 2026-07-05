function isTrivialAliasSnapshot(snapshot) {
  const nodes = snapshot?.elements?.nodes || [];
  const edges = snapshot?.elements?.edges || [];
  const root = nodes.find((node) => node.data?.id === "n0")?.data;
  if (!root) return false;
  if (edges.some((edge) => edge.data?.source === "n0" || edge.data?.target === "n0")) return false;
  return root.kind === "identifier" || root.kind === "variable";
}

function firstExpressionNeighbor(collection, currentId) {
  return collection
    .nodes("node[kind = 'expression']")
    .filter((candidate) =>
      candidate.id() !== currentId &&
      candidate.data("snapshot_index") !== undefined &&
      candidate.data("snapshot_index") !== null
    )
    .first();
}

function preferredExpressionConsumer(node) {
  const outgoing = firstExpressionNeighbor(node.outgoers(), node.id());
  if (outgoing.nonempty()) return outgoing;
  return firstExpressionNeighbor(node.connectedEdges().connectedNodes(), node.id());
}

function renderFullTree(doc) {
  const fullElements = flattenCompoundElements(
    buildFullTreeElements(doc),
    { keepGroupRoles: ["expression_title"] }
  );
  if (fullCy) fullCy.destroy();
  fullCy = cytoscape({
    container: document.getElementById("full"),
    elements: fullElements,
    layout: cytoscapeFlowLayout({ spacingFactor: 1.35, padding: 44, rankSep: 128 }),
    style: commonNodeStyle().concat([
      {
        selector: "node[kind = 'source']",
        style: {
          "background-color": "#1d2a30",
          "border-color": tokenColors.property,
          "color": tokenColors.property,
          "border-width": 2
        }
      },
      {
        selector: "node[kind = 'param']",
        style: {
          "background-color": "#172836",
          "border-color": tokenColors.parameter,
          "color": tokenColors.parameter,
          "border-width": 2
        }
      },
      {
        selector: "$node > node",
        style: {
          "background-opacity": 0.08,
          "border-width": 0,
          "padding": "18px",
          "text-valign": "top",
          "text-margin-y": -14,
          "text-halign": "center",
          "font-size": 13,
          "font-weight": 650
        }
      },
      {
        selector: "node[group_role = 'expression'], node[group_role = 'source'], node[group_role = 'param']",
        style: {
          "background-color": tokenColors.group,
          "border-color": tokenColors.group,
          "color": "#dbe5ef"
        }
      }
    ])
  });
  fullCy.ready(() => {
    tintEdgesByTarget(fullCy);
    installDimmedHover(fullCy);
    fullCy.fit(undefined, 28);
  });
  fullCy.on("tap", "node", (event) => {
    const node = event.target;
    if (node.data("group_role")) return;
    if (node.data("connector") === "true") return;

    let index = node.data("snapshot_index");
    let flowId = node.data("flow_id") || node.data("owner_flow_id");
    let displayName = node.data("name") || node.data("owner_name");
    if (index === undefined || index === null) {
      index = node.data("owner_snapshot_index");
    }

    const ownSnapshot = index !== undefined && index !== null
      ? doc.snapshots?.[index]?.snapshot
      : null;
    if (ownSnapshot && isTrivialAliasSnapshot(ownSnapshot)) {
      const neighbor = preferredExpressionConsumer(node);
      if (neighbor.nonempty()) {
        index = neighbor.data("snapshot_index");
        flowId = neighbor.data("flow_id") || neighbor.data("owner_flow_id") || neighbor.data("id");
        displayName = neighbor.data("name") || neighbor.data("owner_name") || displayName;
      }
    }

    if ((index === undefined || index === null) && !flowId) {
      const neighbor = preferredExpressionConsumer(node);
      if (neighbor.nonempty()) {
        index = neighbor.data("snapshot_index");
        flowId = neighbor.data("flow_id") || neighbor.data("owner_flow_id") || neighbor.data("id");
        displayName = neighbor.data("name") || neighbor.data("owner_name") || displayName;
      }
    }

    const snapshotEntry = doc.snapshots?.[index];
    const snapshot = snapshotEntry?.snapshot;
    if (!snapshot) return;

    const flowNode = (doc.flow?.nodes || []).find((candidate) => candidate.data.id === flowId)
      || (doc.flow?.nodes || []).find((candidate) => candidate.data.snapshot_index === index);
    showSnapshot(
      snapshot,
      displayName || snapshotEntry.name,
      flowNode,
      doc
    );
  });
}

function renderEvaluation(snapshot, name, flowNode, doc) {
  const lines = [];
  const flowNodes = doc?.flow?.nodes || [];
  const flowEdges = doc?.flow?.edges || [];
  const nodeById = new Map(flowNodes.map((node) => [node.data.id, node.data]));
  const flowData = typeof flowNode?.data === "function" ? flowNode.data() : (flowNode?.data || {});
  const targetId = flowData.id;
  const directInputs = flowEdges
    .filter((edge) => edge.data.target === targetId)
    .map((edge) => nodeById.get(edge.data.source))
    .filter(Boolean);
  const dependencies = directInputs.filter((node) => node.kind === "expression");
  const sources = directInputs.filter((node) => node.kind === "source");
  const params = directInputs.filter((node) => node.kind === "param");
  const sampleNodes = (snapshot.elements?.nodes || [])
    .map((node) => node.data)
    .filter((node) => (node.role || "").startsWith("sample["));
  const result = flowData.value || snapshot.result || "";
  const hostInfo = doc?.host;
  const nodeHost = flowData.host || {};

  lines.push(`${name || "expression"} = ${snapshot.expression || ""}`);
  if (snapshot.resolved && snapshot.resolved !== snapshot.expression) {
    lines.push(`values = ${snapshot.resolved}`);
  }
  if (result) lines.push(`result = ${result}`);
  if (hostInfo || Object.keys(nodeHost).length) {
    lines.push("");
    lines.push("Host:");
    if (hostInfo) {
      lines.push(`  ${hostInfo.name || "host"}${hostInfo.schema ? ` / ${hostInfo.schema}` : ""}`);
    }
    if (nodeHost.role) lines.push(`  role = ${nodeHost.role}`);
    if (nodeHost.output) lines.push(`  output = ${nodeHost.output_label || "true"}`);
    if (nodeHost.scenario) lines.push(`  scenario = ${nodeHost.scenario}`);
    if (nodeHost.stable_id) lines.push(`  stable_id = ${nodeHost.stable_id}`);
  }

  if (dependencies.length) {
    lines.push("");
    lines.push("Dependencies:");
    dependencies.forEach((node) => {
      lines.push(`  ${node.name} = ${node.value}`);
    });
  }
  if (sources.length) {
    lines.push("");
    lines.push("Context:");
    sources.forEach((node) => {
      lines.push(`  ${node.name} = ${node.value}`);
    });
  }
  if (params.length) {
    lines.push("");
    lines.push("Params:");
    params.forEach((node) => {
      lines.push(`  ${node.name} = ${node.value}`);
    });
  }
  if (sampleNodes.length) {
    lines.push("");
    lines.push("Samples:");
    sampleNodes.forEach((node) => {
      lines.push(`  ${node.label} = ${node.value}`);
    });
  }

  document.getElementById("evaluation").textContent = lines.join("\n");
}


function showSnapshot(snapshot, name, flowNode, doc) {
  const flowData = typeof flowNode?.data === "function" ? flowNode.data() : (flowNode?.data || {});
  const hostInfo = doc?.host;
  const nodeHost = flowData.host || {};
  currentSnapshot = snapshot;
  currentSnapshotName = name || "";
  currentFlowNode = { data: flowData };
  currentSnapshotDoc = doc;
  document.getElementById("name").textContent = name || "";
  document.getElementById("ast-title").textContent =
    name ? `AST Drilldown: ${name}` : "AST Drilldown";
  document.getElementById("expression").textContent = snapshot.expression || "";
  document.getElementById("resolved").textContent = snapshot.resolved || "";
  document.getElementById("host").textContent = hostInfo
    ? `${hostInfo.name || "host"} ${hostInfo.schema || ""}${nodeHost.role ? ` / ${nodeHost.role}` : ""}`
    : "core";
  renderEvaluation(snapshot, name, flowNode, doc);
  if (astCy) astCy.destroy();
  astCy = cytoscape({
    container: document.getElementById("ast"),
    elements: flattenCompoundElements(
      prepareAstElements(snapshot, name),
      { keepGroupRoles: ["expression_title"] }
    ),
    layout: cytoscapeFlowLayout({
      rankDir: "TB",
      spacingFactor: 1.35,
      padding: 28,
      rankSep: 84,
      sort: reverseLayoutOrderSort
    }),
    style: commonNodeStyle()
  });
  astCy.ready(() => {
    tintEdgesByTarget(astCy);
    installDimmedHover(astCy);
    astCy.fit(undefined, 24);
  });
}

function snapshotFileForScenario(scenario, metadataMode = "core") {
  const base = scenario === "trading" ? "snapshot.trading" : `snapshot.${scenario}`;
  return metadataMode === "host" ? `${base}.host.json` : `${base}.json`;
}

function fallbackSnapshotFileForScenario(scenario) {
  return scenario === "trading" ? "snapshot.json" : null;
}

function roleForFlowNode(node) {
  const data = node?.data || {};
  const hostRole = data.host?.role;
  if (hostRole) return hostRole;
  if (data.name && expressionRoles[data.name]) return expressionRoles[data.name];
  if (data.kind === "expression") return "expression";
  if (data.kind === "source" || data.kind === "param") return data.kind;
  return null;
}

function addHostOutputNodes(doc) {
  const nodes = doc.flow.nodes;
  const edges = doc.flow.edges;
  nodes
    .filter((node) => node.data?.kind === "expression" && node.data?.host?.output)
    .forEach((node) => {
      const source = node.data;
      const outputId = `output_${source.id}`;
      if (nodes.some((candidate) => candidate.data?.id === outputId)) return;
      nodes.push({
        data: {
          id: outputId,
          name: source.host.output_label || source.name,
          label: source.host.output_label || source.name,
          display_label: source.value ? `= ${source.value}` : "",
          kind: "output",
          role: "output",
          state: source.state,
          value: source.value,
          host: {
            scenario: source.host.scenario,
            role: "output",
            source_role: source.host.role,
            source_expression: source.name,
            stable_id: `${source.host.stable_id || source.name}:output`
          }
        }
      });
      edges.push({
        data: {
          id: `output_edge_${source.id}`,
          source: source.id,
          target: outputId,
          source_name: source.name || "output",
          target_name: source.host.output_label || source.name,
          role: "output",
          output_edge: "true"
        }
      });
    });
}

function prepareDoc(doc) {
  const usedRoles = new Set();
  doc.flow = doc.flow || { nodes: [], edges: [] };
  doc.flow.nodes = doc.flow.nodes || [];
  doc.flow.edges = doc.flow.edges || [];
  doc.flow.nodes = doc.flow.nodes.filter((node) =>
    !node.data?.group_role && node.data?.kind !== "output"
  );
  doc.flow.edges = doc.flow.edges.filter((edge) => edge.data?.output_edge !== "true");
  doc.flow.nodes.forEach((node) => {
    if (node.data) delete node.data.parent;
  });
  addHostOutputNodes(doc);

  const roleCounts = new Map();
  doc.flow.nodes.forEach((node) => {
    const role = roleForFlowNode(node);
    if (role) roleCounts.set(role, (roleCounts.get(role) || 0) + 1);
  });

  doc.flow.nodes.forEach((node) => {
    const role = roleForFlowNode(node);
    if (!role) return;
    node.data.role = role;
    if (node.data.kind === "expression") {
      node.data.display_label = expressionNodeDisplayLabel(node.data);
    }
    if ((roleCounts.get(role) || 0) > 1) {
      node.data.parent = `group_${role}`;
      usedRoles.add(role);
    }
  });
  addExpressionTitleGroups(doc.flow, "flow_expr_title");
  usedRoles.forEach((role) => {
    doc.flow.nodes.push({
      data: {
        id: `group_${role}`,
        label: roleLabels[role] || role,
        display_label: roleLabels[role] || role,
        group_role: role
      }
    });
  });
  const flowNodeById = new Map(doc.flow.nodes.map((node) => [node.data.id, node.data]));
  doc.flow.edges.forEach((edge) => {
    const source = flowNodeById.get(edge.data.source);
    const target = flowNodeById.get(edge.data.target);
    if (source?.kind === "source" || source?.kind === "param") {
      edge.data.context_edge = "true";
    }
    if (source?.kind === "expression" && target?.kind === "expression") {
      edge.data.expression_edge = "true";
    }
  });
}

function renderFlow(doc) {
  const snapshots = doc.snapshots || [];
  const flowElements = flattenCompoundElements(
    maybeBundleContextEdges(doc.flow, "flow", 2, ["source", "param"]),
    { keepGroupRoles: ["output", "expression_title"] }
  );
  if (flowCy) flowCy.destroy();
  flowCy = cytoscape({
    container: document.getElementById("flow"),
    elements: flowElements,
    layout: cytoscapeFlowLayout({
      spacingFactor: 1.55,
      padding: 40,
      rankSep: 112,
      stop: function () {
        const cy = this && this.cy && this.cy();
        if (cy) placeOutputGroupRight(cy);
      }
    }),
    style: commonNodeStyle().concat([
      {
        selector: "edge",
        style: {
          "source-label": "",
          "target-label": "data(source_name)",
          "target-text-offset": 28
        }
      },
      {
        selector: "node:selected",
        style: { "border-width": 3, "border-color": "#2457a6" }
      },
      {
        selector: "node[kind = 'source']",
        style: {
          "background-color": "#1d2a30",
          "border-color": tokenColors.property,
          "color": tokenColors.property,
          "border-width": 2
        }
      },
      {
        selector: "node[kind = 'param']",
        style: {
          "background-color": "#172836",
          "border-color": tokenColors.parameter,
          "color": tokenColors.parameter,
          "border-width": 2
        }
      },
      {
        selector: "node[role = 'entry'], node[role = 'decision']",
        style: {
          "background-color": "#17302b",
          "border-color": tokenColors.true,
          "color": tokenColors.true,
          "border-width": 2
        }
      },
      {
        selector: "node[role = 'exit'], node[role = 'blocked'], node[role = 'risk']",
        style: {
          "background-color": "#351f26",
          "border-color": tokenColors.false,
          "color": tokenColors.false,
          "border-width": 2
        }
      },
      {
        selector: "node[role = 'filter']",
        style: {
          "background-color": "#1d2a30",
          "border-color": tokenColors.property,
          "color": tokenColors.property,
          "border-width": 2
        }
      },
      {
        selector: "node[role = 'score'], node[role = 'derived']",
        style: {
          "background-color": "#2b291b",
          "border-color": tokenColors.function,
          "color": tokenColors.function,
          "border-width": 2
        }
      },
      {
        selector: "node[kind = 'expression'][state = 'true']",
        style: {
          "background-color": "#17302b",
          "border-color": tokenColors.true,
          "color": tokenColors.true,
          "border-width": 2
        }
      },
      {
        selector: "node[kind = 'expression'][state = 'false']",
        style: {
          "background-color": "#351f26",
          "border-color": tokenColors.false,
          "color": tokenColors.false,
          "border-width": 2
        }
      },
      {
        selector: "node[kind = 'output']",
        style: {
          "shape": "round-rectangle",
          "border-width": 2
        }
      },
      {
        selector: "node[group_role = 'output']",
        style: {
          "padding": "14px",
          "text-margin-y": -10,
          "background-opacity": 0.12,
          "border-width": 1,
          "border-color": "#dbe5ef"
        }
      },
      {
        selector: "edge[output_edge = 'true']",
        style: {
          "line-color": "#dbe5ef",
          "source-arrow-shape": "none",
          "source-arrow-color": "#dbe5ef",
          "target-arrow-shape": "triangle",
          "target-arrow-color": "#dbe5ef",
          "width": 2,
          "line-style": "dashed"
        }
      },
      {
        selector: "$node > node",
        style: {
          "background-opacity": 0.08,
          "border-width": 0,
          "padding": "18px",
          "text-valign": "top",
          "text-margin-y": -14,
          "text-halign": "center",
          "font-size": 13,
          "font-weight": 650
        }
      },
      {
        selector: "node[group_role]",
        style: {
          "background-color": tokenColors.group,
          "border-color": tokenColors.group,
          "color": "#dbe5ef"
        }
      }
    ])
  });
  flowCy.ready(() => {
    placeOutputGroupRight(flowCy);
    tintEdgesByTarget(flowCy);
    installDimmedHover(flowCy);
      flowCy.fit(undefined, 28);
  });

  flowCy.on("tap", "node", (event) => {
    let index = event.target.data("snapshot_index");
    let flowNode = event.target;
    if (event.target.data("kind") === "output") {
      const incoming = event.target.incomers("edge[output_edge = 'true']").first();
      const sourceId = incoming.data("source");
      flowNode = flowCy.getElementById(sourceId);
      index = flowNode.data("snapshot_index");
    } else if (event.target.data("kind") !== "expression") {
      return;
    }
    if (snapshots[index]) {
      showSnapshot(
        snapshots[index].snapshot,
        flowNode.data("name") || snapshots[index].name,
        flowNode,
        doc
      );
    }
  });
}

function renderDoc(doc) {
  activeDoc = doc;
  prepareDoc(doc);
  const snapshots = doc.snapshots || [];
  const first = snapshots[0] && snapshots[0].snapshot;
  const flowNodes = doc.flow?.nodes || [];
  if (first) {
    const firstFlowNode = flowNodes.find((node) => node.data.snapshot_index === 0);
    showSnapshot(first, snapshots[0].name, firstFlowNode, doc);
  }
  renderFullTree(doc);
  renderFlow(doc);
}

function loadScenario(scenario) {
  const metadataMode = document.getElementById("metadata-mode").value;
  const file = snapshotFileForScenario(scenario, metadataMode);
  document.getElementById("name").textContent = "loading";
  document.getElementById("expression").textContent = file;
  document.getElementById("resolved").textContent = "";
  document.getElementById("host").textContent = metadataMode === "host" ? `${file} expected` : "core";
  fetch(`${file}?v=${Date.now()}`)
    .then((response) => {
      const fallbackFile = metadataMode === "core" ? fallbackSnapshotFileForScenario(scenario) : null;
      if (!response.ok && fallbackFile) {
        return fetch(`${fallbackFile}?v=${Date.now()}`);
      }
      return response;
    })
    .then((response) => {
      if (!response.ok) throw new Error(`${file} not found`);
      return response.json();
    })
    .then(renderDoc)
    .catch((err) => {
      document.getElementById("name").textContent = "error";
      document.getElementById("expression").textContent = err.message;
      document.getElementById("host").textContent = "not loaded";
      document.getElementById("resolved").textContent =
        "Run the C example to generate snapshot.<scenario>.json.";
    });
}

function loadJsonFile(file) {
  if (!file) return;
  document.getElementById("name").textContent = "loading";
  document.getElementById("expression").textContent = file.name;
  document.getElementById("resolved").textContent = "";
  document.getElementById("host").textContent = "reading local file";

  const reader = new FileReader();
  reader.onload = () => {
    try {
      const doc = JSON.parse(String(reader.result || ""));
      renderDoc(doc);
      document.getElementById("expression").textContent = file.name;
    } catch (err) {
      document.getElementById("name").textContent = "error";
      document.getElementById("expression").textContent = file.name;
      document.getElementById("host").textContent = "not loaded";
      document.getElementById("resolved").textContent = err.message;
    }
  };
  reader.onerror = () => {
    document.getElementById("name").textContent = "error";
    document.getElementById("expression").textContent = file.name;
    document.getElementById("host").textContent = "not loaded";
    document.getElementById("resolved").textContent = "Failed to read selected JSON file.";
  };
  reader.readAsText(file);
}


document.getElementById("scenario").addEventListener("change", (event) => {
  loadScenario(event.target.value);
});
document.getElementById("metadata-mode").addEventListener("change", () => {
  loadScenario(document.getElementById("scenario").value);
});
document.getElementById("json-file").addEventListener("change", (event) => {
  loadJsonFile(event.target.files && event.target.files[0]);
});
document.getElementById("connector-mode").addEventListener("change", () => {
  if (!activeDoc) return;
  renderFullTree(activeDoc);
  renderFlow(activeDoc);
});
document.getElementById("ast-flow-mode").addEventListener("change", () => {
  if (activeDoc) {
    renderFullTree(activeDoc);
    renderFlow(activeDoc);
  }
  if (!currentSnapshot) return;
  showSnapshot(currentSnapshot, currentSnapshotName, currentFlowNode, currentSnapshotDoc);
});
document.getElementById("comparator-mode").addEventListener("change", () => {
  if (!currentSnapshot) return;
  showSnapshot(currentSnapshot, currentSnapshotName, currentFlowNode, currentSnapshotDoc);
});
window.addEventListener("resize", () => {
  [flowCy, astCy, fullCy].forEach((cy) => {
    if (!cy) return;
    cy.resize();
    cy.fit(undefined, 24);
  });
});
