function valueLabel(ele) {
  const d = ele.data();
  if (d.display_label) return d.display_label;
  const title = d.name || d.label || "";
  const expression = d.expression ? `\n${d.expression}` : "";
  const resolved = d.resolved || "";
  const value = d.value || "";
  const finalValue = resolved && value && resolved !== value ? `\n= ${value}` : "";
  const valueLine = resolved ? `\n= ${resolved}${finalValue}` : (value ? `\n= ${value}` : "");
  return `${title}${expression}${valueLine}`;
}

function commonNodeStyle() {
  return [
    {
      selector: "node",
      style: {
        "shape": "round-rectangle",
        "width": "label",
        "height": "label",
        "padding": "10px",
        "background-color": "#1f1f1f",
        "border-width": 1,
        "border-color": "#2a3a4f",
        "label": valueLabel,
        "font-size": 12,
        "color": tokenColors.foreground,
        "text-wrap": "wrap",
        "text-max-width": 160,
        "text-valign": "center",
        "text-halign": "center",
        "text-justification": "left"
      }
    },
    {
      selector: "node[kind = 'expression']",
      style: {
        "background-color": "#2b2618",
        "border-color": tokenColors.expression,
        "color": tokenColors.expression,
        "border-width": 2
      }
    },
    {
      selector: "node[state = 'true']",
      style: {
        "background-color": "#17302b",
        "border-color": tokenColors.true,
        "color": tokenColors.true
      }
    },
    {
      selector: "node[state = 'false']",
      style: {
        "background-color": "#351f26",
        "border-color": tokenColors.false,
        "color": tokenColors.false
      }
    },
    {
      selector: "node[state = 'number'], node[state = 'value']",
      style: {
        "background-color": "#22291f",
        "border-color": tokenColors.number,
        "color": tokenColors.number
      }
    },
    {
      selector: "node[kind = 'variable']",
      style: {
        "background-color": "#172836",
        "border-color": tokenColors.parameter,
        "color": tokenColors.parameter,
        "border-width": 2
      }
    },
    {
      selector: "node[kind = 'identifier'], node[kind = 'lookback'], node[role = 'source']",
      style: {
        "background-color": "#1d2a30",
        "border-color": tokenColors.property,
        "color": tokenColors.property,
        "border-width": 2
      }
    },
    {
      selector: "node[role = 'function']",
      style: {
        "background-color": "#30213a",
        "border-color": tokenColors.functionNode,
        "color": tokenColors.functionNode,
        "border-width": 2
      }
    },
    {
      selector: "node[kind = 'binary_op'], node[kind = 'unary_op']",
      style: {
        "border-width": 2
      }
    },
    {
      selector: "node[state = 'skipped']",
      style: {
        "background-color": "#151a21",
        "border-color": "#303948",
        "color": "#737f8f"
      }
    },
    {
      selector: "node[state = 'error']",
      style: {
        "background-color": "#2d2518",
        "border-color": tokenColors.symbolReference,
        "color": tokenColors.symbolReference
      }
    },
    {
      selector: "edge",
      style: {
        "curve-style": "unbundled-bezier",
        "control-point-distances": 36,
        "control-point-weights": 0.42,
        "source-arrow-shape": "triangle",
        "source-arrow-color": "#8a95a3",
        "target-arrow-shape": "none",
        "line-color": "#8a95a3",
        "width": 1.15,
        "source-label": "data(role)",
        "font-size": 10,
        "color": "#596778",
        "text-rotation": "none",
        "source-text-offset": 28,
        "text-margin-y": -8,
        "text-margin-x": 0,
        "text-background-color": "#10161d",
        "text-background-opacity": 1,
        "text-background-padding": 2
      }
    },
    {
      selector: "edge[context_edge = 'true']",
      style: {
        "curve-style": "unbundled-bezier",
        "control-point-distances": 30,
        "control-point-weights": 0.38,
        "source-arrow-shape": "none",
        "target-arrow-shape": "triangle",
        "width": 1.6,
        "source-text-offset": 32,
        "text-margin-y": -10,
        "text-margin-x": 0
      }
    },
    {
      selector: "edge[expression_edge = 'true']",
      style: {
        "source-arrow-shape": "none",
        "target-arrow-shape": "triangle"
      }
    },
    {
      selector: "edge[bundle_edge = 'true']",
      style: {
        "curve-style": "unbundled-bezier",
        "control-point-distances": "data(curve_distance)",
        "control-point-weights": "data(curve_weight)",
        "width": 1.4,
        "source-text-offset": 36,
        "text-margin-y": -12,
        "text-margin-x": 0
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
        "text-justification": "center",
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
    },
    {
      selector: "node[connector = 'true']",
      style: {
        "shape": "ellipse",
        "width": 12,
        "height": 12,
        "padding": "0px",
        "label": "data(label)",
        "background-color": "#263241",
        "border-width": 1,
        "border-color": "#8a95a3",
        "font-size": 10,
        "text-max-width": 90,
        "text-valign": "top",
        "text-halign": "center",
        "text-justification": "center",
        "text-margin-y": -8,
        "text-background-opacity": 1,
        "text-background-color": "#10161d",
        "text-background-padding": 2
      }
    },
    {
      selector: "node[connector_kind = 'source']",
      style: {
        "background-color": tokenColors.property,
        "border-color": tokenColors.property
      }
    },
    {
      selector: "node[connector_kind = 'param']",
      style: {
        "background-color": tokenColors.parameter,
        "border-color": tokenColors.parameter
      }
    },
    {
      selector: "node[connector_kind = 'number'], node[connector_kind = 'bool'], node[connector_kind = 'string']",
      style: {
        "background-color": tokenColors.number,
        "border-color": tokenColors.number,
        "color": tokenColors.number
      }
    }
  ];
}
