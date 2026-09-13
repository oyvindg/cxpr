let astCy = null;
let fullCy = null;
let flowCy = null;
let activeDoc = null;
let currentSnapshot = null;
let currentSnapshotName = "";
let currentFlowNode = null;
let currentSnapshotDoc = null;
const expressionRoles = {
  entry: "entry",
  blocked_entry: "blocked",
  exit: "exit"
};
const roleLabels = {
  entry: "Entry Signals",
  blocked: "Blocked Entry",
  exit: "Exit Signals",
  decision: "Decisions",
  filter: "Filters",
  risk: "Risk",
  score: "Scores",
  derived: "Derived",
  output: "Outputs",
  expression: "Expressions",
  source: "Context",
  param: "Params",
  function: "Functions",
  sample: "Samples"
};
const tokenColors = {
  function: "#DCDCAA",
  expression: "#D7BA7D",
  functionNode: "#C586C0",
  operator: "#569CD6",
  parameter: "#4FC1FF",
  property: "#9CDCFE",
  qualifier: "#4EC9B0",
  number: "#B5CEA8",
  keyword: "#C586C0",
  symbolReference: "#D7BA7D",
  syntax: "#569CD6",
  foreground: "#D4D4D4",
  true: "#4EC9B0",
  false: "#F87171",
  group: "#9CA3AF"
};
