const CXPR_TOKEN_TYPES = [
  "function",
  "import",
  "importPath",
  "parameter",
  "namedArgument",
  "assignment",
  "block",
  "plotBlock",
  "property",
  "reference",
  "input",
  "state",
  "variable",
  "string",
  "number",
  "operator",
  "keyword",
  "punctuation",
  "comment"
];

const CXPR_DEFAULT_TOKEN_COLORS = {
  function: "#DCDCAA",
  import: "#DCDCAA",
  importPath: "#D7BA7D",
  parameter: "#4FC1FF",
  namedArgument: "#9D9D9D",
  assignment: "#569CD6",
  block: "#C586C0",
  plotBlock: "#D7BA7D",
  property: "#9CDCFE",
  reference: "#569CD6",
  input: "#4EC9B0",
  state: "#E06C75",
  variable: "#9CDCFE",
  string: "#CE9178",
  number: "#B5CEA8",
  operator: "#D4D4D4",
  keyword: "#C586C0",
  punctuation: "#D4D4D4",
  comment: "#6A9955"
};

const CXPR_KEYWORDS = [
  "and",
  "assert",
  "as",
  "or",
  "not",
  "if",
  "then",
  "else",
  "fn",
  "from",
  "in",
  "initial",
  "model",
  "name",
  "out",
  "optimize",
  "return",
  "state",
  "update",
  "use",
  "true",
  "false",
  "null"
];

const CXPR_OPERATOR_TOKENS = ["<=", ">=", "==", "!=", "&&", "||", "|>", "**"];
const CXPR_PUNCTUATION_CHARS = [".", ",", "(", ")", "[", "]", "{", "}"];
const CXPR_SINGLE_CHAR_OPERATORS = ["+", "-", "*", "/", "%", "^", "<", ">", "=", "!", "?", ":"];
const CXPR_NUMBER_PATTERN = /^(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?/;

function nextNonSpace(text, index) {
  for (let i = index; i < text.length; i += 1) {
    if (!/\s/.test(text[i])) return text[i];
  }
  return "";
}

function prevNonSpace(text, index) {
  for (let i = index; i >= 0; i -= 1) {
    if (!/\s/.test(text[i])) return text[i];
  }
  return "";
}

function stripLineComment(lineText) {
  let quote = "";
  for (let i = 0; i < lineText.length; i += 1) {
    const ch = lineText[i];
    const prev = i > 0 ? lineText[i - 1] : "";
    if (!quote && (ch === "\"" || ch === "'")) {
      quote = ch;
      continue;
    }
    if (quote && ch === quote && prev !== "\\") {
      quote = "";
      continue;
    }
    if (!quote && ch === "#") return lineText.slice(0, i);
    if (!quote && ch === "/" && lineText[i + 1] === "/") return lineText.slice(0, i);
  }
  return lineText;
}

function isGroupedUseImportName(text, tokenStart, tokenEnd) {
  const before = text.slice(0, tokenStart);
  const after = text.slice(tokenEnd);
  return (
    (/^\s*use\s*\{[^}]*$/.test(before) && /\}[^{}]*\bfrom\b/.test(after)) ||
    (/^\s*use\s+[^{}]*$/.test(before) && /\bfrom\b/.test(after))
  );
}

function isUseImportPathSegment(text, tokenStart, tokenEnd) {
  const direct = text.match(/^\s*use\s+([A-Za-z0-9_./-]+)/);
  if (direct) {
    const start = direct.index + direct[0].lastIndexOf(direct[1]);
    const end = start + direct[1].length;
    if (tokenStart >= start && tokenEnd <= end) return true;
  }
  const before = text.slice(0, tokenStart);
  const after = text.slice(tokenEnd);
  return /^\s*use\b.*\bfrom\s+(?:[A-Za-z0-9_.-]+\/)*$/.test(before) &&
         /^\s*(?:\/[A-Za-z0-9_.-]+)*\s*$/.test(after);
}

function groupedUseAliasesFromLine(lineText) {
  const code = stripLineComment(lineText);
  let match = code.match(/^\s*use\s*\{([^}]*)\}\s*from\s+([A-Za-z0-9_./-]+)/);
  if (!match) {
    match = code.match(/^\s*use\s+([^{}\n]+?)\s+from\s+([A-Za-z0-9_./-]+)/);
  }
  if (!match) return [];
  const base = match[2].replace(/\/$/, "");
  const aliases = [];
  for (const itemMatch of match[1].matchAll(/\b([A-Za-z_][A-Za-z0-9_]*)(?:\s+as\s+([A-Za-z_][A-Za-z0-9_]*))?\b/g)) {
    const name = itemMatch[1];
    const alias = itemMatch[2] || name;
    aliases.push({
      name,
      alias,
      path: `${base}/${name}`
    });
  }
  return aliases;
}

function rangeStartCharacter(range) {
  if (typeof range?.start?.character === "number") return range.start.character;
  if (typeof range?.startColumn === "number") return range.startColumn - 1;
  return -1;
}

function rangeEndCharacter(range) {
  if (typeof range?.end?.character === "number") return range.end.character;
  if (typeof range?.endColumn === "number") return range.endColumn - 1;
  return -1;
}

function useImportPathAtLine(lineText, range) {
  const code = stripLineComment(lineText);
  const match = code.match(/\bfrom\s+([A-Za-z0-9_./-]+)/);
  if (!match || !match[1]) return "";
  const start = match.index + match[0].lastIndexOf(match[1]);
  const end = start + match[1].length;
  const rangeStart = rangeStartCharacter(range);
  const rangeEnd = rangeEndCharacter(range);
  if (rangeStart < start || rangeEnd > end) return "";
  return match[1];
}

module.exports = {
  CXPR_DEFAULT_TOKEN_COLORS,
  CXPR_KEYWORDS,
  CXPR_NUMBER_PATTERN,
  CXPR_OPERATOR_TOKENS,
  CXPR_PUNCTUATION_CHARS,
  CXPR_SINGLE_CHAR_OPERATORS,
  CXPR_TOKEN_TYPES,
  groupedUseAliasesFromLine,
  isGroupedUseImportName,
  isUseImportPathSegment,
  nextNonSpace,
  prevNonSpace,
  stripLineComment,
  useImportPathAtLine
};
