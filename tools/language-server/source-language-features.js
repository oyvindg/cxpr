const fs = require("fs");
const path = require("path");
const childProcess = require("child_process");
const vscode = require("./vscode-lsp-shim");

function loadCxprLanguageCore() {
  return require("./cxpr-language-core");
}

const {
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
} = loadCxprLanguageCore();

const TOKEN_TYPES = CXPR_TOKEN_TYPES;
const KEYWORD_TOKENS = new Set(CXPR_KEYWORDS);
const OPERATOR_TOKENS = CXPR_OPERATOR_TOKENS;
const PUNCTUATION_CHARS = new Set(CXPR_PUNCTUATION_CHARS);
const SINGLE_CHAR_OPERATORS = new Set(CXPR_SINGLE_CHAR_OPERATORS);
const NUMBER_RE = CXPR_NUMBER_PATTERN;
let additionalImportRoots = [];

function configureImportRoots(roots) {
  additionalImportRoots = (roots || []).filter(Boolean);
}

function readColorProfileFile(filePath) {
  try {
    const parsed = JSON.parse(fs.readFileSync(filePath, "utf8"));
    if (!parsed || typeof parsed !== "object" || !parsed.tokens) return null;
    return parsed.tokens;
  } catch (_err) {
    return null;
  }
}

function loadDefaultColors() {
  const packagedProfile = path.join(__dirname, "theme", "cxpr-color-profile.json");
  const repoProfile = path.join(__dirname, "..", "cxpr", "theme", "cxpr-color-profile.json");
  const profile = readColorProfileFile(packagedProfile) || readColorProfileFile(repoProfile);
  return { ...CXPR_DEFAULT_TOKEN_COLORS, ...(profile || {}) };
}

const DEFAULT_COLORS = loadDefaultColors();

const BUILTIN_FUNCTIONS = new Map([
  ["min", { returns: "double", signature: "min(value, ...)", summary: "Returns the smallest numeric argument." }],
  ["max", { returns: "double", signature: "max(value, ...)", summary: "Returns the largest numeric argument." }],
  ["clamp", { returns: "double", signature: "clamp(value, min, max)", summary: "Clamps a numeric value between min and max." }],
  ["sign", { returns: "double", signature: "sign(value)", summary: "Returns the sign of a numeric value." }],
  ["add", { returns: "double", signature: "add(a, b)", summary: "Returns a + b." }],
  ["sub", { returns: "double", signature: "sub(a, b)", summary: "Returns a - b." }],
  ["mul", { returns: "double", signature: "mul(a, b)", summary: "Returns a * b." }],
  ["div", { returns: "double", signature: "div(a, b)", summary: "Returns a / b." }],
  ["lerp", { returns: "double", signature: "lerp(a, b, t)", summary: "Linearly interpolates between a and b." }],
  ["smoothstep", { returns: "double", signature: "smoothstep(edge0, edge1, x)", summary: "Returns smooth Hermite interpolation between 0 and 1." }],
  ["sigmoid", { returns: "double", signature: "sigmoid(x, midpoint, steepness)", summary: "Returns a sigmoid curve value." }],
  ["if", { returns: "double", signature: "if(condition, when_true, when_false)", summary: "Returns when_true when condition is true, otherwise when_false." }],
  ["abs", { returns: "double", signature: "abs(value)", summary: "Returns the absolute numeric value." }],
  ["floor", { returns: "double", signature: "floor(value)", summary: "Rounds down to the nearest integer value." }],
  ["ceil", { returns: "double", signature: "ceil(value)", summary: "Rounds up to the nearest integer value." }],
  ["round", { returns: "double", signature: "round(value)", summary: "Rounds to the nearest integer value." }],
  ["trunc", { returns: "double", signature: "trunc(value)", summary: "Truncates the fractional part." }],
  ["sqrt", { returns: "double", signature: "sqrt(value)", summary: "Returns the square root." }],
  ["cbrt", { returns: "double", signature: "cbrt(value)", summary: "Returns the cube root." }],
  ["hypot", { returns: "double", signature: "hypot(x, y)", summary: "Returns sqrt(x*x + y*y)." }],
  ["pow", { returns: "double", signature: "pow(base, exponent)", summary: "Raises base to exponent." }],
  ["exp", { returns: "double", signature: "exp(value)", summary: "Returns e raised to value." }],
  ["exp2", { returns: "double", signature: "exp2(value)", summary: "Returns 2 raised to value." }],
  ["expm1", { returns: "double", signature: "expm1(value)", summary: "Returns exp(value) - 1." }],
  ["log", { returns: "double", signature: "log(value)", summary: "Returns the natural logarithm." }],
  ["log10", { returns: "double", signature: "log10(value)", summary: "Returns the base-10 logarithm." }],
  ["log2", { returns: "double", signature: "log2(value)", summary: "Returns the base-2 logarithm." }],
  ["log1p", { returns: "double", signature: "log1p(value)", summary: "Returns log(1 + value)." }],
  ["mod", { returns: "double", signature: "mod(a, b)", summary: "Returns the floating-point remainder." }],
  ["copysign", { returns: "double", signature: "copysign(value, sign)", summary: "Returns value with sign copied from sign." }],
  ["radians", { returns: "double", signature: "radians(degrees)", summary: "Converts degrees to radians." }],
  ["degrees", { returns: "double", signature: "degrees(radians)", summary: "Converts radians to degrees." }],
  ["sin", { returns: "double", signature: "sin(value)", summary: "Returns sine." }],
  ["cos", { returns: "double", signature: "cos(value)", summary: "Returns cosine." }],
  ["tan", { returns: "double", signature: "tan(value)", summary: "Returns tangent." }],
  ["asin", { returns: "double", signature: "asin(value)", summary: "Returns arcsine." }],
  ["acos", { returns: "double", signature: "acos(value)", summary: "Returns arccosine." }],
  ["atan", { returns: "double", signature: "atan(value)", summary: "Returns arctangent." }],
  ["atan2", { returns: "double", signature: "atan2(y, x)", summary: "Returns arctangent using y and x signs." }],
  ["sinh", { returns: "double", signature: "sinh(value)", summary: "Returns hyperbolic sine." }],
  ["cosh", { returns: "double", signature: "cosh(value)", summary: "Returns hyperbolic cosine." }],
  ["tanh", { returns: "double", signature: "tanh(value)", summary: "Returns hyperbolic tangent." }],
  ["pi", { returns: "double", signature: "pi()", summary: "Returns pi." }],
  ["e", { returns: "double", signature: "e()", summary: "Returns Euler's number." }],
  ["nan", { returns: "double", signature: "nan()", summary: "Returns NaN." }],
  ["inf", { returns: "double", signature: "inf()", summary: "Returns infinity." }],
  ["contains", { returns: "bool", signature: "contains(source, value)", summary: "Returns whether source contains value." }],
  ["within", { returns: "bool", signature: "within(source, min, max, include_min?, include_max?)", summary: "Returns whether source is within a numeric range." }],
  ["cross_above", { returns: "bool", signature: "cross_above(left, right)", summary: "Returns true when left crosses from below to above right." }],
  ["cross_below", { returns: "bool", signature: "cross_below(left, right)", summary: "Returns true when left crosses from above to below right." }],
  ["rising", { returns: "bool", signature: "rising(value, samples)", summary: "Returns true when value rises across the requested sample count. `bars` is accepted as the common strategy alias." }],
  ["falling", { returns: "bool", signature: "falling(value, samples)", summary: "Returns true when value falls across the requested sample count. `bars` is accepted as the common strategy alias." }],
  ["repeat", { returns: "bool", signature: "repeat(condition, samples)", summary: "Returns true when condition has held for the requested sample count. `bars` is accepted as the common strategy alias." }],
  ["net_up", { returns: "bool", signature: "net_up(value, samples)", summary: "Returns true when the net change over the window is positive." }],
  ["net_down", { returns: "bool", signature: "net_down(value, samples)", summary: "Returns true when the net change over the window is negative." }],
  ["overlaps", { returns: "bool", signature: "overlaps(left, right, bars?)", summary: "Returns true when both conditions occur within the requested window." }],
  ["signal_overlaps", { returns: "bool", signature: "signal_overlaps(left, right, bars?)", summary: "Alias for overlaps used by strategy signals." }],
  ["delta", { returns: "double", signature: "delta(value, samples)", summary: "Returns the value change over the requested sample count." }],
  ["roc", { returns: "double", signature: "roc(value, samples)", summary: "Returns the rate of change over the requested sample count." }],
  ["highest", { returns: "double", signature: "highest(value, samples)", summary: "Returns the highest value in the window." }],
  ["lowest", { returns: "double", signature: "lowest(value, samples)", summary: "Returns the lowest value in the window." }],
  ["window", { returns: "window", signature: "window(value, samples)", summary: "Creates a rolling expression window for reductions such as sum, mean, min, max, stddev, roc, and wma." }],
  ["sum", { returns: "double", signature: "sum(window(value, samples))", summary: "Returns the sum of values in a rolling expression window." }],
  ["stddev", { returns: "double", signature: "stddev(window(value, samples))", summary: "Returns the population standard deviation of a rolling expression window." }],
  ["wma", { returns: "double", signature: "wma(window(value, samples))", summary: "Returns the weighted mean of a rolling expression window, weighting newer samples more heavily." }],
  ["mean_absdev", { returns: "double", signature: "mean_absdev(window(value, samples), center)", summary: "Returns the mean absolute deviation from center over a rolling expression window." }],
  ["window_sum", { returns: "double", signature: "window_sum(value, samples)", summary: "Returns the sum of values in the window." }],
  ["window_mean", { returns: "double", signature: "window_mean(value, samples)", summary: "Returns the mean value in the window." }],
  ["window_wma", { returns: "double", signature: "window_wma(value, samples)", summary: "Returns the weighted moving average in the window." }],
  ["window_lag", { returns: "double", signature: "window_lag(value, samples)", summary: "Returns the value from the requested number of samples ago." }],
  ["window_highest", { returns: "double", signature: "window_highest(value, samples)", summary: "Returns the highest value in the window." }],
  ["window_lowest", { returns: "double", signature: "window_lowest(value, samples)", summary: "Returns the lowest value in the window." }],
  ["window_stddev", { returns: "double", signature: "window_stddev(value, samples)", summary: "Returns the standard deviation in the window." }],
  ["window_roc", { returns: "double", signature: "window_roc(value, samples)", summary: "Returns the rate of change across the window." }],
  ["bars_since_extreme", { returns: "double", signature: "bars_since_extreme(value, samples, mode)", summary: "Returns the bars since the selected window extreme." }],
  ["window_mean_absdev", { returns: "double", signature: "window_mean_absdev(value, samples, center)", summary: "Returns the mean absolute deviation from center in the window." }],
  ["avg", { returns: "double", signature: "avg(basket)", summary: "Returns the average value across a basket." }],
  ["any", { returns: "bool", signature: "any(basket)", summary: "Returns whether any basket value is true." }],
  ["all", { returns: "bool", signature: "all(basket)", summary: "Returns whether all basket values are true." }],
  ["count", { returns: "double", signature: "count(basket)", summary: "Returns the number of true basket values." }],
  ["mean", { returns: "double", signature: "mean(value, ...)", summary: "Returns the arithmetic mean of its arguments." }],
  ["coalesce", { returns: "value", signature: "coalesce(value, fallback)", summary: "Returns the first non-null value." }],
  ["isnan", { returns: "bool", signature: "isnan(value)", summary: "Returns whether value is NaN." }],
  ["isfinite", { returns: "bool", signature: "isfinite(value)", summary: "Returns whether value is finite." }],
  ["is_null", { returns: "bool", signature: "is_null(value)", summary: "Returns whether value is null." }]
]);

function findLineCommentStart(lineText) {
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
    if (quote) continue;
    if (ch === "#") return i;
    if (ch === "/" && lineText[i + 1] === "/") return i;
  }
  return -1;
}

function analyzeComments(text) {
  const chars = Array.from(text);
  const tokens = [];
  let quote = "";
  let line = 0;
  let col = 0;

  function addCommentToken(startLine, startCol, length) {
    if (length <= 0) return;
    tokens.push({
      line: startLine,
      start: startCol,
      length,
      type: "comment"
    });
  }

  for (let i = 0; i < chars.length; i += 1) {
    const ch = chars[i];
    const prev = i > 0 ? chars[i - 1] : "";

    if (!quote && (ch === "\"" || ch === "'")) {
      quote = ch;
    } else if (quote && ch === quote && prev !== "\\") {
      quote = "";
    }

    if (!quote && ch === "#") {
      const startLine = line;
      const startCol = col;
      let length = 0;
      while (i < chars.length && chars[i] !== "\n") {
        chars[i] = " ";
        i += 1;
        col += 1;
        length += 1;
      }
      addCommentToken(startLine, startCol, length);
      if (i >= chars.length) break;
      line += 1;
      col = 0;
      continue;
    } else if (!quote && ch === "/" && chars[i + 1] === "/") {
      const startLine = line;
      const startCol = col;
      let length = 0;
      while (i < chars.length && chars[i] !== "\n") {
        chars[i] = " ";
        i += 1;
        col += 1;
        length += 1;
      }
      addCommentToken(startLine, startCol, length);
      if (i >= chars.length) break;
      line += 1;
      col = 0;
      continue;
    } else if (!quote && ch === "/" && chars[i + 1] === "*") {
      let segmentLine = line;
      let segmentStart = col;
      let segmentLength = 0;
      while (i < chars.length) {
        const current = chars[i];
        const closes = current === "*" && chars[i + 1] === "/";
        chars[i] = current === "\n" ? "\n" : " ";
        if (current === "\n") {
          addCommentToken(segmentLine, segmentStart, segmentLength);
          line += 1;
          col = 0;
          segmentLine = line;
          segmentStart = col;
          segmentLength = 0;
          i += 1;
          continue;
        }
        col += 1;
        segmentLength += 1;
        i += 1;
        if (closes && i < chars.length) {
          chars[i] = " ";
          col += 1;
          segmentLength += 1;
          i += 1;
          break;
        }
      }
      addCommentToken(segmentLine, segmentStart, segmentLength);
      i -= 1;
      continue;
    }

    if (ch === "\n") {
      line += 1;
      col = 0;
    } else {
      col += 1;
    }
  }

  return {
    text: chars.join(""),
    tokens
  };
}

function stripCommentsPreserveLayout(text) {
  return analyzeComments(text).text;
}

function stripStringsPreserveLayout(text) {
  const chars = Array.from(text);
  let quote = "";
  for (let i = 0; i < chars.length; i += 1) {
    const ch = chars[i];
    const prev = i > 0 ? chars[i - 1] : "";
    if (!quote && (ch === "\"" || ch === "'")) {
      quote = ch;
      chars[i] = " ";
      continue;
    }
    if (quote) {
      chars[i] = ch === "\n" ? "\n" : " ";
      if (ch === quote && prev !== "\\") quote = "";
    }
  }
  return chars.join("");
}

function isLineAssignment(text, tokenStart, tokenEnd) {
  if (text.slice(0, tokenStart).trim().length > 0) return false;
  const index = tokenEnd + text.slice(tokenEnd).search(/\S/);
  return index >= tokenEnd && text[index] === "=" && text[index + 1] !== "=";
}

function isDeclarationAssignment(text, tokenStart, tokenEnd) {
  const prefix = text.slice(0, tokenStart).trim();
  if (prefix !== "out" && prefix !== "state") return false;
  const index = tokenEnd + text.slice(tokenEnd).search(/\S/);
  return index >= tokenEnd && text[index] === "=" && text[index + 1] !== "=";
}

function isFunctionDeclarationName(text, tokenStart, tokenEnd) {
  if (text.slice(0, tokenStart).trim() !== "fn") return false;
  return nextNonSpace(text, tokenEnd) === "(";
}

function isNameDeclaration(text, tokenStart, tokenEnd) {
  const prefix = text.slice(0, tokenStart).trim();
  if (prefix !== "name" && prefix !== "model") return false;
  const next = nextNonSpace(text, tokenEnd);
  return next === "{" || next === "";
}

function isNamedDeclarationKeyword(text, tokenStart, tokenEnd) {
  const word = text.slice(tokenStart, tokenEnd);
  if (word !== "name" && word !== "model") return false;
  if (text.slice(0, tokenStart).trim().length > 0) return false;
  return /^[ \t]+[A-Za-z_][A-Za-z0-9_.]*(?:[ \t]*\{|[ \t]*)$/.test(text.slice(tokenEnd));
}

function isBlockDeclaration(text, tokenStart, tokenEnd) {
  if (text.slice(0, tokenStart).trim().length > 0) return false;
  const next = nextNonSpace(text, tokenEnd);
  return next === "{";
}

function isHostBlockDeclaration(text, tokenStart) {
  if (text.slice(0, tokenStart).trim().length > 0) return false;

  let quote = "";
  for (let i = tokenStart; i < text.length; i += 1) {
    const ch = text[i];
    const prev = i > 0 ? text[i - 1] : "";
    if (!quote && (ch === "\"" || ch === "'")) {
      quote = ch;
      continue;
    }
    if (quote && ch === quote && prev !== "\\") {
      quote = "";
      continue;
    }
    if (quote) continue;
    if (ch === "#" || ch === "=") return false;
    if (ch === "{") return true;
  }
  return false;
}

function isHostBlockHeaderLabel(text, tokenStart, tokenEnd) {
  const prefix = text.slice(0, tokenStart).trim();
  if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(prefix)) return false;

  let quote = "";
  for (let i = tokenEnd; i < text.length; i += 1) {
    const ch = text[i];
    const prev = i > 0 ? text[i - 1] : "";
    if (!quote && (ch === "\"" || ch === "'")) {
      quote = ch;
      continue;
    }
    if (quote && ch === quote && prev !== "\\") {
      quote = "";
      continue;
    }
    if (quote) continue;
    if (ch === "#" || ch === "=") return false;
    if (ch === "{") return true;
  }
  return false;
}

function isLineAssignmentOperator(text, index) {
  if (text[index] !== "=" || text[index + 1] === "=") return false;
  const left = text.slice(0, index).trim();
  return /^(?:(?:out|state)\s+)?\$?[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*$/.test(left);
}

function collectInputNames(text) {
  const inputs = new Set();
  const code = stripCommentsPreserveLayout(text);
  const inputDeclRe = /^\s*in\b\s*(?:(?:([A-Za-z_][A-Za-z0-9_]*)\s*)?\{([^}]*)\}|([A-Za-z_][A-Za-z0-9_]*(?:\s*,\s*[A-Za-z_][A-Za-z0-9_]*)*))/gm;
  let match;
  while ((match = inputDeclRe.exec(code)) !== null) {
    const root = match[1] || "";
    const body = match[2] || match[3] || "";
    for (const entry of body.split(/,|\r?\n/)) {
      const input = entry.trim().match(/^([A-Za-z_][A-Za-z0-9_]*)(?:\s*:\s*[A-Za-z_][A-Za-z0-9_<>]*)?$/);
      if (input) inputs.add(root ? `${root}.${input[1]}` : input[1]);
    }
  }
  let inputBlockDepth = 0;
  for (const line of code.split(/\r?\n/)) {
    if (/^\s*in\s*\{/.test(line)) {
      inputBlockDepth = 1;
      continue;
    }
    if (inputBlockDepth > 0) {
      if (inputBlockDepth === 1) {
        const input = line.trim().match(/^([A-Za-z_][A-Za-z0-9_]*)\s*,?$/);
        if (input) inputs.add(input[1]);
      }
      inputBlockDepth += lineBraceDelta(line);
      if (inputBlockDepth < 0) inputBlockDepth = 0;
      continue;
    }
    const match = line.match(/^\s*in\s+([^{}]+)$/);
    if (!match) continue;
    for (const entry of match[1].split(",")) {
      const input = entry.trim().match(/^([A-Za-z_][A-Za-z0-9_]*)$/);
      if (input) inputs.add(input[1]);
    }
  }
  return inputs;
}

function collectPublicCallParams(text) {
  const params = new Set();
  const code = stripCommentsPreserveLayout(text);
  let inputBlockDepth = 0;
  for (const line of code.split(/\r?\n/)) {
    if (/^\s*in\s*\{/.test(line)) {
      inputBlockDepth = 1;
      continue;
    }
    if (inputBlockDepth > 0) {
      if (inputBlockDepth === 1) {
        const param = line.trim().match(/^\$([A-Za-z_][A-Za-z0-9_]*)\s*=/);
        if (param) params.add(`$${param[1]}`);
      }
      inputBlockDepth += lineBraceDelta(line);
      if (inputBlockDepth < 0) inputBlockDepth = 0;
      continue;
    }
    const match = line.match(/^\s*in\s+([^{}]+)$/);
    if (!match) continue;
    for (const entry of match[1].split(",")) {
      const param = entry.trim().match(/^\$([A-Za-z_][A-Za-z0-9_]*)\s*=/);
      if (param) params.add(`$${param[1]}`);
    }
  }
  return params;
}

function collectInputRoots(text) {
  const roots = new Set();
  for (const name of collectInputNames(text)) {
    const dot = name.indexOf(".");
    if (dot > 0) roots.add(name.slice(0, dot));
  }
  return roots;
}

function fullInputNameAt(text, word, beforeWord) {
  const rootMatch = beforeWord.match(/([A-Za-z_][A-Za-z0-9_]*)\.\s*$/);
  if (rootMatch) return `${rootMatch[1]}.${word}`;
  return word;
}

function collectStateNames(text) {
  const states = new Set();
  const code = stripCommentsPreserveLayout(text);
  const stateRe = /^\s*state\s*\{/gm;
  let match;
  while ((match = stateRe.exec(code)) !== null) {
    const block = readBlockAfter(code, match.index);
    for (const entryMatch of block.matchAll(/^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=/gm)) {
      states.add(entryMatch[1]);
    }
  }
  for (const match of code.matchAll(
    /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:=.+?\binitial\b\s+.+$/gm
  )) {
    states.add(match[1]);
  }
  return states;
}

function collectStateBlockLines(lines) {
  const blockLines = new Set();
  let depth = 0;

  for (let line = 0; line < lines.length; line += 1) {
    const text = stripLineComment(lines[line]);
    const startsState = depth === 0 && /^\s*state\s*\{/.test(text);
    const inStateBlock = depth > 0 || startsState;
    if (inStateBlock) blockLines.add(line);
    if (inStateBlock) {
      depth += lineBraceDelta(text);
      if (depth < 0) depth = 0;
    }
  }

  return blockLines;
}

function isMethodCallReceiver(text, tokenEnd) {
  const after = text.slice(tokenEnd);
  return /^\s*\.\s*[A-Za-z_][A-Za-z0-9_]*\s*\(/.test(after);
}

function structInputRootFromLine(text) {
  const match = stripLineComment(text).match(/^\s*in\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{/);
  return match ? match[1] : null;
}

function isStructInputRoot(text, currentStructInputRoot, word, tokenStart, tokenEnd) {
  const root = structInputRootFromLine(text) || currentStructInputRoot;
  if (!root || root !== word) return false;
  const before = text.slice(0, tokenStart);
  const after = text.slice(tokenEnd);
  return /^\s*in\s+$/.test(before) && /^\s*\{/.test(after);
}

function isStructInputField(text, currentStructInputRoot, inputNames, word, tokenStart, tokenEnd) {
  const root = structInputRootFromLine(text) || currentStructInputRoot;
  if (!root || !inputNames.has(`${root}.${word}`)) return false;
  const before = text.slice(0, tokenStart);
  const after = text.slice(tokenEnd);
  return (/\{\s*(?:[A-Za-z_][A-Za-z0-9_]*\s*,\s*)*$/.test(before) ||
          /^\s*(?:[A-Za-z_][A-Za-z0-9_]*\s*,\s*)*$/.test(before)) &&
         /^\s*(?:,|\})/.test(after);
}

function tokenizeLine(line, text, inputNames, inputRoots, currentStructInputRoot, stateNames, referenceNames, suppressStateTokens, inDocumentBlock, inStateBlock) {
  const tokens = [];
  const outputRole = outputRoleDeclaration(text);

  function pushToken(localStart, localEnd, type) {
    if (localEnd <= localStart) return;
    tokens.push({
      line,
      start: localStart,
      length: localEnd - localStart,
      type
    });
  }

  let i = 0;
  while (i < text.length) {
    const ch = text[i];
    if (/\s/.test(ch)) {
      i += 1;
      continue;
    }

    if (ch === "\"" || ch === "'") {
      const tokenStart = i;
      const quote = ch;
      i += 1;
      while (i < text.length) {
        const current = text[i];
        const prev = i > 0 ? text[i - 1] : "";
        i += 1;
        if (current === quote && prev !== "\\") break;
      }
      pushToken(tokenStart, i, "string");
      continue;
    }

    if (PUNCTUATION_CHARS.has(ch)) {
      pushToken(i, i + 1, "punctuation");
      i += 1;
      continue;
    }

    if (ch === "$") {
      const match = text.slice(i).match(/^\$[A-Za-z_][A-Za-z0-9_]*/);
      if (match) {
        const tokenEnd = i + match[0].length;
        pushToken(i, tokenEnd, isLineAssignment(text, i, tokenEnd) || isDeclarationAssignment(text, i, tokenEnd) ? "assignment" : "parameter");
        i += match[0].length;
        continue;
      }
    }

    const operator = OPERATOR_TOKENS.find((token) => text.startsWith(token, i));
    if (operator) {
      pushToken(i, i + operator.length, "operator");
      i += operator.length;
      continue;
    }

    if (SINGLE_CHAR_OPERATORS.has(ch)) {
      pushToken(i, i + 1, isLineAssignmentOperator(text, i) ? "assignment" : "operator");
      i += 1;
      continue;
    }

    const numberMatch = text.slice(i).match(NUMBER_RE);
    if (numberMatch) {
      pushToken(i, i + numberMatch[0].length, "number");
      i += numberMatch[0].length;
      continue;
    }

    const wordMatch = text.slice(i).match(/^[A-Za-z_][A-Za-z0-9_]*/);
    if (wordMatch) {
      const word = wordMatch[0];
      const wordEnd = i + word.length;
      const prev = prevNonSpace(text, i - 1);
      const next = nextNonSpace(text, wordEnd);
      if (outputRole && i === outputRole.roleStart && wordEnd === outputRole.roleEnd) {
        pushToken(i, wordEnd, "property");
      } else if (isLineAssignment(text, i, wordEnd) || isDeclarationAssignment(text, i, wordEnd)) {
        pushToken(i, wordEnd, inStateBlock ? "state" : "assignment");
      } else if (isFunctionDeclarationName(text, i, wordEnd)) {
        pushToken(i, wordEnd, "function");
      } else if (KEYWORD_TOKENS.has(word) || isNamedDeclarationKeyword(text, i, wordEnd)) {
        pushToken(i, wordEnd, "keyword");
      } else if (inDocumentBlock && isBareDocumentStringValue(text, i, wordEnd)) {
        pushToken(i, wordEnd, "string");
      } else if (isNameDeclaration(text, i, wordEnd)) {
        pushToken(i, wordEnd, "assignment");
      } else if (word === "optimize" && (next === "=" || next === "{")) {
        pushToken(i, wordEnd, "namedArgument");
      } else if (isStructInputRoot(text, currentStructInputRoot, word, i, wordEnd)) {
        pushToken(i, wordEnd, "reference");
      } else if (isStructInputField(text, currentStructInputRoot, inputNames, word, i, wordEnd)) {
        pushToken(i, wordEnd, "input");
      } else if (word === "plot" && isHostBlockDeclaration(text, i)) {
        pushToken(i, wordEnd, "plotBlock");
      } else if (isBlockDeclaration(text, i, wordEnd) || isHostBlockDeclaration(text, i)) {
        pushToken(i, wordEnd, "block");
      } else if (isHostBlockHeaderLabel(text, i, wordEnd)) {
        pushToken(i, wordEnd, "variable");
      } else if (!suppressStateTokens && stateNames.has(word) && prev !== "." && next !== "(" && next !== "=") {
        pushToken(i, wordEnd, "state");
      } else if (inputNames.has(word) && prev !== "." && next !== "=") {
        pushToken(i, wordEnd, "input");
      } else if (prev === "." && inputNames.has(fullInputNameAt(text, word, text.slice(0, i)))) {
        pushToken(i, wordEnd, "input");
      } else if (next === "(") {
        pushToken(i, wordEnd, "function");
      } else if (prev === ".") {
        pushToken(i, wordEnd, "property");
      } else if (inputRoots.has(word) && next === "." && !isMethodCallReceiver(text, wordEnd)) {
        pushToken(i, wordEnd, "reference");
      } else if (referenceNames.has(word) && next === "." && !isMethodCallReceiver(text, wordEnd)) {
        pushToken(i, wordEnd, "reference");
      } else if (isGroupedUseImportName(text, i, wordEnd)) {
        pushToken(i, wordEnd, "import");
      } else if (isUseImportPathSegment(text, i, wordEnd)) {
        pushToken(i, wordEnd, "importPath");
      } else if (next === "=") {
        pushToken(i, wordEnd, "namedArgument");
      } else {
        pushToken(i, wordEnd, "variable");
      }
      i = wordEnd;
      continue;
    }

    i += 1;
  }

  return tokens;
}

function collectReferenceNames(text) {
  const references = new Set();
  const code = stripCommentsPreserveLayout(text);
  const assignmentRe = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*[A-Za-z_][A-Za-z0-9_]*\s*\(/gm;
  let match;
  while ((match = assignmentRe.exec(code)) !== null) {
    references.add(match[1]);
  }
  for (const name of collectCxprRecordBindings(code).keys()) {
    references.add(name);
  }
  return references;
}

function collectFunctionLineNumbers(text) {
  const functionLines = new Set();
  const lines = stripCommentsPreserveLayout(text).split(/\r?\n/);

  for (let line = 0; line < lines.length; line += 1) {
    const lineText = lines[line];
    const fnMatch = lineText.match(/^(\s*)fn\s+[A-Za-z_][A-Za-z0-9_]*\b/);
    if (!fnMatch) continue;

    const fnIndent = fnMatch[1].length;
    functionLines.add(line);

    if (lineText.includes("{")) {
      let depth = 0;
      for (let blockLine = line; blockLine < lines.length; blockLine += 1) {
        const current = lines[blockLine];
        for (const ch of current) {
          if (ch === "{") depth += 1;
          if (ch === "}") depth -= 1;
        }
        functionLines.add(blockLine);
        if (depth <= 0 && blockLine > line) {
          line = blockLine;
          break;
        }
      }
      continue;
    }

    for (let bodyLine = line + 1; bodyLine < lines.length; bodyLine += 1) {
      const current = lines[bodyLine];
      if (!current.trim()) break;
      const indent = current.match(/^\s*/)[0].length;
      if (indent <= fnIndent) break;
      functionLines.add(bodyLine);
      line = bodyLine;
    }
  }

  return functionLines;
}

function collectStructInputRootByLine(lines) {
  const roots = new Map();
  let currentRoot = null;
  for (let line = 0; line < lines.length; line += 1) {
    const text = lines[line];
    const startMatch = text.match(/^\s*in\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{/);
    if (startMatch) currentRoot = startMatch[1];
    if (currentRoot) roots.set(line, currentRoot);
    if (currentRoot && text.includes("}")) currentRoot = null;
  }
  return roots;
}

function lineBraceDelta(text) {
  let quote = "";
  let delta = 0;
  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i];
    const prev = i > 0 ? text[i - 1] : "";
    if (!quote && (ch === "\"" || ch === "'")) {
      quote = ch;
      continue;
    }
    if (quote && ch === quote && prev !== "\\") {
      quote = "";
      continue;
    }
    if (quote) continue;
    if (ch === "{") delta += 1;
    if (ch === "}") delta -= 1;
  }
  return delta;
}

function isDocumentBlockStart(text) {
  const code = stripLineComment(text);
  return (
    /^\s*model\s+[A-Za-z_][A-Za-z0-9_.]*\s*\{/.test(code) ||
    /^\s*out\s+[A-Za-z_][A-Za-z0-9_.]*(?:\s+as\s+[A-Za-z_][A-Za-z0-9_]*)?\s*\{/.test(code) ||
    /^\s*(?!(?:name|use|in|fn|update|out|state|meta)\b)[A-Za-z_][A-Za-z0-9_]*(?:\s+[A-Za-z0-9_][A-Za-z0-9_.-]*)?\s*\{/.test(code)
  );
}

function collectDocumentBlockLines(lines) {
  const blockLines = new Set();
  let depth = 0;

  for (let line = 0; line < lines.length; line += 1) {
    const text = lines[line];
    const startsBlock = depth === 0 && isDocumentBlockStart(text);
    const inBlock = depth > 0 || startsBlock;
    if (inBlock) blockLines.add(line);

    if (inBlock) {
      depth += lineBraceDelta(stripLineComment(text));
      if (depth < 0) depth = 0;
    }
  }

  return blockLines;
}

function isBareDocumentStringValue(text, tokenStart, tokenEnd) {
  const word = text.slice(tokenStart, tokenEnd);
  if (KEYWORD_TOKENS.has(word)) return false;
  if (!/^[A-Za-z_][A-Za-z0-9_.-]*$/.test(word)) return false;

  const before = text.slice(0, tokenStart);
  const after = text.slice(tokenEnd);
  const valuePrefix = before.match(/(?:=\s*|\[\s*|,\s*)$/);
  if (!valuePrefix) return false;
  if (!/^\s*(?:,|\]|\}|$)/.test(after)) return false;
  return true;
}

function outputRoleDeclaration(lineText) {
  const code = stripLineComment(lineText);
  const match = code.match(
    /^\s*out\s+([A-Za-z_][A-Za-z0-9_]*)\s+as\s+([A-Za-z_][A-Za-z0-9_]*)\b/
  );
  if (!match) return null;
  const roleStart = code.indexOf(match[2], code.indexOf(match[1]) + match[1].length);
  return {
    output: match[1],
    role: match[2],
    roleStart,
    roleEnd: roleStart + match[2].length
  };
}

function buildDocumentTokens(text) {
  const tokens = [];
  const comments = analyzeComments(text);
  const lines = comments.text.split(/\r?\n/);
  const inputNames = collectInputNames(text);
  const inputRoots = collectInputRoots(text);
  const stateNames = collectStateNames(text);
  const referenceNames = collectReferenceNames(text);
  const functionLines = collectFunctionLineNumbers(text);
  const structInputRootByLine = collectStructInputRootByLine(lines);
  const documentBlockLines = collectDocumentBlockLines(lines);
  const stateBlockLines = collectStateBlockLines(lines);
  tokens.push(...comments.tokens);
  for (let line = 0; line < lines.length; line += 1) {
    const code = lines[line];
    if (!code.trim()) continue;
    tokens.push(...tokenizeLine(
      line,
      code,
      inputNames,
      inputRoots,
      structInputRootByLine.get(line) || null,
      stateNames,
      referenceNames,
      functionLines.has(line),
      documentBlockLines.has(line),
      stateBlockLines.has(line)
    ));
  }
  return tokens;
}

const toolingCache = new Map();

function candidateToolingExecutables(document) {
  const configured = vscode.workspace.getConfiguration("cxprLanguage").get("tooling.executable", "");
  const root = workspaceRootFor(document);
  return [
    configured,
    path.join(root, "build", "libs", "cxpr", "cxpr_document_tooling"),
    path.join(root, "build", "libs", "cxpr", "Debug", "cxpr_document_tooling"),
    path.join(root, "build", "libs", "cxpr", "Release", "cxpr_document_tooling")
  ].filter(Boolean);
}

function resolveToolingExecutable(document) {
  for (const candidate of candidateToolingExecutables(document)) {
    if (fs.existsSync(candidate)) return candidate;
  }
  return "";
}

function loadAstTooling(document) {
  if (!document || document.languageId !== "cxpr" || document.uri.scheme !== "file") return null;
  const cacheKey = `${document.uri.toString()}@${document.version}`;
  if (toolingCache.has(cacheKey)) return toolingCache.get(cacheKey);

  const executable = resolveToolingExecutable(document);
  if (!executable) {
    toolingCache.set(cacheKey, null);
    return null;
  }

  let result = null;
  try {
    const run = childProcess.spawnSync(
      executable,
      ["--source-name", document.uri.fsPath],
      {
        input: document.getText(),
        encoding: "utf8",
        cwd: workspaceRootFor(document),
        maxBuffer: 16 * 1024 * 1024
      }
    );
    if (run.status === 0 && run.stdout) {
      const parsed = JSON.parse(run.stdout);
      if (parsed && parsed.ok) result = parsed;
    }
  } catch (_err) {
    result = null;
  }

  toolingCache.set(cacheKey, result);
  if (toolingCache.size > 32) {
    const first = toolingCache.keys().next().value;
    toolingCache.delete(first);
  }
  return result;
}

function buildDocumentTokensForDocument(document) {
  const tokens = buildDocumentTokens(document.getText());
  const tooling = loadAstTooling(document);
  if (tooling && Array.isArray(tooling.tokens) && tooling.tokens.length > 0) {
    for (const token of tooling.tokens) {
      if (!TOKEN_TYPES.includes(token.type)) continue;
      const tokenEnd = token.start + token.length;
      const overlaps = tokens.some((existing) => (
        existing.line === token.line &&
        token.start < existing.start + existing.length &&
        tokenEnd > existing.start
      ));
      if (!overlaps) tokens.push(token);
    }
  }
  return tokens.sort((a, b) => (
    a.line === b.line ? a.start - b.start : a.line - b.line
  ));
}

function getDecorationSettings() {
  const config = vscode.workspace.getConfiguration("cxprLanguage");
  const profilePath = config.get("theme.profilePath", "");
  const profileColors = profilePath ? readColorProfileFile(profilePath) : null;
  const defaults = { ...DEFAULT_COLORS, ...(profileColors || {}) };
  const colors = {};
  for (const type of TOKEN_TYPES) {
    const key = `decoration.${type}Color`;
    const inspected = config.inspect(key);
    colors[type] =
      inspected && inspected.workspaceFolderValue !== undefined ? inspected.workspaceFolderValue :
      inspected && inspected.workspaceValue !== undefined ? inspected.workspaceValue :
      inspected && inspected.globalValue !== undefined ? inspected.globalValue :
      defaults[type];
  }
  return {
    enabled: config.get("decoration.enabled", true),
    colors
  };
}

function toDecorationColor(value) {
  if (typeof value !== "string" || value.trim().length === 0) return undefined;
  if (value.startsWith("#") || value.startsWith("rgb") || value.startsWith("hsl")) return value;
  return new vscode.ThemeColor(value);
}

function findMatchingBrace(text, openIndex) {
  let depth = 0;
  let quote = "";
  for (let i = openIndex; i < text.length; i += 1) {
    const ch = text[i];
    const prev = i > 0 ? text[i - 1] : "";
    if (!quote && (ch === "\"" || ch === "'")) {
      quote = ch;
      continue;
    }
    if (quote && ch === quote && prev !== "\\") {
      quote = "";
      continue;
    }
    if (quote) continue;
    if (ch === "{") depth += 1;
    if (ch === "}") {
      depth -= 1;
      if (depth === 0) return i;
    }
  }
  return -1;
}

function readBlockAfter(text, startIndex) {
  const openIndex = text.indexOf("{", startIndex);
  if (openIndex < 0) return "";
  const closeIndex = findMatchingBrace(text, openIndex);
  if (closeIndex < 0) return "";
  return text.slice(openIndex + 1, closeIndex);
}

function readMetadataValue(block, key) {
  const quoted = block.match(new RegExp(`\\b${key}\\s*=\\s*"([^"]*)"`, "m"));
  if (quoted) return quoted[1];
  const bare = block.match(new RegExp(`\\b${key}\\s*=\\s*([^\\s{}]+)`, "m"));
  return bare ? bare[1] : "";
}

function collectLeadingCommentSummary(text) {
  const lines = text.split(/\r?\n/);
  const comments = [];
  for (const line of lines) {
    const trimmed = line.trim();
    if (!trimmed) {
      if (comments.length > 0) break;
      continue;
    }
    if (!trimmed.startsWith("#")) break;
    comments.push(trimmed.replace(/^#\s?/, ""));
  }
  return comments.join(" ");
}

function parseDocComment(rawComment) {
  const doc = { summary: "", params: new Map(), returns: "" };
  const summary = [];
  for (const rawLine of rawComment.split(/\r?\n/)) {
    const line = rawLine.trim().replace(/^#\s?/, "").trim();
    if (!line) continue;
    const paramMatch = line.match(/^@param\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.+)$/);
    if (paramMatch) {
      doc.params.set(paramMatch[1], paramMatch[2]);
      continue;
    }
    const returnMatch = line.match(/^@returns?\s+(.+)$/);
    if (returnMatch) {
      doc.returns = returnMatch[1];
      continue;
    }
    summary.push(line);
  }
  doc.summary = summary.join(" ");
  return doc;
}

function lineStartIndex(text, index) {
  const start = text.lastIndexOf("\n", index);
  return start < 0 ? 0 : start + 1;
}

function readFunctionReturnExpression(text, signatureEnd) {
  let index = signatureEnd;
  while (index < text.length && /[ \t]/.test(text[index])) index += 1;

  if (text[index] === "=") {
    const firstLineStart = lineStartIndex(text, signatureEnd);
    const firstLineEnd = text.indexOf("\n", signatureEnd);
    const firstLine = text.slice(firstLineStart, firstLineEnd < 0 ? text.length : firstLineEnd);
    const fnIndent = (firstLine.match(/^\s*/) || [""])[0].length;
    const exprLines = [firstLine.slice(index - firstLineStart + 1).trim()].filter(Boolean);
    const lines = text.slice((firstLineEnd < 0 ? text.length : firstLineEnd + 1)).split(/\r?\n/);

    for (const line of lines) {
      if (!line.trim()) break;
      const indent = (line.match(/^\s*/) || [""])[0].length;
      if (indent <= fnIndent) break;
      exprLines.push(line.trim());
    }

    return exprLines.join(" ");
  }

  if (text[index] === "{") {
    const block = readBlockAfter(text, index);
    const match = block.match(/^\s*(?:return|out)\s+(.+?)\s*$/m);
    return match ? match[1].trim() : "";
  }

  return "";
}

function splitTopLevelArgs(text) {
  const args = [];
  let start = 0;
  let depth = 0;
  let quote = "";
  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i];
    const prev = i > 0 ? text[i - 1] : "";
    if (!quote && (ch === "\"" || ch === "'")) {
      quote = ch;
      continue;
    }
    if (quote && ch === quote && prev !== "\\") {
      quote = "";
      continue;
    }
    if (quote) continue;
    if (ch === "(" || ch === "[" || ch === "{") depth += 1;
    if (ch === ")" || ch === "]" || ch === "}") depth -= 1;
    if (ch === "," && depth === 0) {
      args.push(text.slice(start, i).trim());
      start = i + 1;
    }
  }
  const tail = text.slice(start).trim();
  if (tail) args.push(tail);
  return args;
}

function hasSingleOuterParens(text) {
  if (!text.startsWith("(") || !text.endsWith(")")) return false;
  let depth = 0;
  let quote = "";
  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i];
    const prev = i > 0 ? text[i - 1] : "";
    if (!quote && (ch === "\"" || ch === "'")) {
      quote = ch;
      continue;
    }
    if (quote && ch === quote && prev !== "\\") {
      quote = "";
      continue;
    }
    if (quote) continue;
    if (ch === "(") depth += 1;
    if (ch === ")") {
      depth -= 1;
      if (depth === 0 && i < text.length - 1) return false;
    }
  }
  return depth === 0;
}

function stripOuterParens(expr) {
  let text = expr.trim();
  while (hasSingleOuterParens(text)) {
    text = text.slice(1, -1).trim();
  }
  return text;
}

function splitConditionalExpression(expr) {
  let parenDepth = 0;
  let nestedConditionals = 0;
  let question = -1;
  let quote = "";
  for (let i = 0; i < expr.length; i += 1) {
    const ch = expr[i];
    const prev = i > 0 ? expr[i - 1] : "";
    if (!quote && (ch === "\"" || ch === "'")) {
      quote = ch;
      continue;
    }
    if (quote && ch === quote && prev !== "\\") {
      quote = "";
      continue;
    }
    if (quote) continue;
    if (ch === "(") parenDepth += 1;
    if (ch === ")") parenDepth -= 1;
    if (parenDepth !== 0) continue;
    if (ch === "?") {
      if (question < 0) question = i;
      else nestedConditionals += 1;
      continue;
    }
    if (ch !== ":" || question < 0) continue;
    if (nestedConditionals > 0) {
      nestedConditionals -= 1;
      continue;
    }
    return [expr.slice(question + 1, i), expr.slice(i + 1)];
  }
  return null;
}

function hasTopLevelBooleanOperator(expr) {
  let parenDepth = 0;
  let quote = "";
  for (let i = 0; i < expr.length; i += 1) {
    const ch = expr[i];
    const prev = i > 0 ? expr[i - 1] : "";
    if (!quote && (ch === "\"" || ch === "'")) {
      quote = ch;
      continue;
    }
    if (quote && ch === quote && prev !== "\\") {
      quote = "";
      continue;
    }
    if (quote) continue;
    if (ch === "(") {
      parenDepth += 1;
      continue;
    }
    if (ch === ")") {
      parenDepth -= 1;
      continue;
    }
    if (parenDepth !== 0) continue;
    const tail = expr.slice(i);
    if (/^(?:&&|\|\||<=|>=|==|!=|<|>)/.test(tail)) return true;
    if (/^(?:and|or)\b/.test(tail) && (i === 0 || !/[A-Za-z0-9_]/.test(prev))) return true;
  }
  return false;
}

function inferReturnTypeFromExpression(expr) {
  const text = stripOuterParens(expr || "");
  if (!text) return "";
  if (/^(?:true|false)\b/.test(text)) return "bool";
  if (/^null\b/.test(text)) return "null";
  if (/^["']/.test(text)) return "string";
  if (/^(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(text)) return "double";

  const conditional = splitConditionalExpression(text);
  if (conditional) {
    const thenType = inferReturnTypeFromExpression(conditional[0]);
    const elseType = inferReturnTypeFromExpression(conditional[1]);
    return thenType && thenType === elseType ? thenType : "";
  }

  const ifMatch = text.match(/^if\s*\((.*)\)$/);
  if (ifMatch) {
    const args = splitTopLevelArgs(ifMatch[1]);
    if (args.length >= 3) {
      const thenType = inferReturnTypeFromExpression(args[1]);
      const elseType = inferReturnTypeFromExpression(args[2]);
      return thenType && thenType === elseType ? thenType : "";
    }
  }

  if (hasTopLevelBooleanOperator(text)) return "bool";

  const callMatch = text.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*\(/);
  if (callMatch) {
    const builtin = BUILTIN_FUNCTIONS.get(callMatch[1]);
    if (builtin) return builtin.returns;
  }

  if (/[+\-*/%^]|\*\*/.test(text)) return "double";
  return "";
}

function parseCxprIndicatorMetadata(text, filePath, workspaceRoot = "") {
  const metadata = {
    filePath,
    workspaceRoot,
    name: "",
    label: "",
    type: "",
    sourceArg: "",
    summary: collectLeadingCommentSummary(text),
    params: [],
    outputs: [],
    functions: [],
    states: []
  };

  const nameMatch = text.match(/^\s*(?:name|model)\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*\{)?/m);
  if (nameMatch) {
    metadata.name = nameMatch[1];
    if (text[nameMatch.index + nameMatch[0].length - 1] === "{") {
      const block = readBlockAfter(text, nameMatch.index);
      metadata.label = readMetadataValue(block, "label");
      metadata.type = readMetadataValue(block, "type");
      metadata.sourceArg = readMetadataValue(block, "source_arg");
    }
  }

  const paramRe = /^\s*\$([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^{\n]+)(?:\{)?/gm;
  let paramMatch;
  while ((paramMatch = paramRe.exec(text)) !== null) {
    const hasBlock = text.indexOf("{", paramMatch.index) >= 0 &&
      text.indexOf("{", paramMatch.index) < text.indexOf("\n", paramMatch.index);
    const block = hasBlock ? readBlockAfter(text, paramMatch.index) : "";
    metadata.params.push({
      name: paramMatch[1],
      defaultValue: paramMatch[2].trim(),
      description: block ? readMetadataValue(block, "description") : ""
    });
  }
  for (const line of stripCommentsPreserveLayout(text).split(/\r?\n/)) {
    const inputMatch = line.match(/^\s*in\s+([^{}]+)$/);
    if (!inputMatch) continue;
    for (const entry of inputMatch[1].split(",")) {
      const publicParam = entry.trim().match(
        /^\$([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$/
      );
      if (!publicParam ||
          metadata.params.some((param) => param.name === publicParam[1])) continue;
      metadata.params.push({
        name: publicParam[1],
        defaultValue: publicParam[2].trim(),
        description: ""
      });
    }
  }

  const outputRe = /^\s*out\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{/gm;
  let outputMatch;
  while ((outputMatch = outputRe.exec(text)) !== null) {
    const block = readBlockAfter(text, outputMatch.index);
    metadata.outputs.push({
      name: outputMatch[1],
      label: readMetadataValue(block, "label"),
      role: readMetadataValue(block, "role"),
      description: readMetadataValue(block, "description"),
      hover: readMetadataValue(block, "hover")
    });
  }

  const stateRe = /^\s*state\s*\{/gm;
  let stateMatch;
  while ((stateMatch = stateRe.exec(text)) !== null) {
    const block = readBlockAfter(text, stateMatch.index);
    for (const line of block.split(/\r?\n/)) {
      const entryMatch = line.match(/^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+?)\s*$/);
      if (!entryMatch) continue;
      metadata.states.push({
        name: entryMatch[1],
        initialValue: entryMatch[2]
      });
    }
  }
  for (const match of text.matchAll(
    /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:=.+?\binitial\b\s+(.+?)\s*$/gm
  )) {
    metadata.states.push({
      name: match[1],
      initialValue: match[2]
    });
  }

  const functionRe = /((?:^[ \t]*#.*(?:\r?\n|$))*)^[ \t]*fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/gm;
  let functionMatch;
  while ((functionMatch = functionRe.exec(text)) !== null) {
    const doc = parseDocComment(functionMatch[1] || "");
    const returnExpr = readFunctionReturnExpression(text, functionMatch.index + functionMatch[0].length);
    const params = functionMatch[3]
      .split(",")
      .map((param) => param.trim())
      .filter(Boolean)
      .map((name) => ({
        name,
        description: doc.params.get(name) || ""
      }));
    metadata.functions.push({
      name: functionMatch[2],
      summary: doc.summary,
      params,
      returns: doc.returns,
      returnExpr,
      inferredReturnType: doc.returns ? "" : inferReturnTypeFromExpression(returnExpr)
    });
  }

  return metadata;
}

function workspaceRootFor(document) {
  const folder = vscode.workspace.getWorkspaceFolder(document.uri);
  if (folder) return folder.uri.fsPath;
  return path.dirname(document.uri.fsPath);
}

function pathExists(filePath) {
  return !!filePath && fs.existsSync(filePath);
}

function resolveDynCxprUseFromAncestors(startDir, usePath) {
  let cursor = startDir || ".";
  const normalized = usePath.endsWith(".cxpr") ? usePath : `${usePath}.cxpr`;
  for (;;) {
    const candidate = path.join(cursor, "libs", "dyn", "cxpr", normalized);
    if (pathExists(candidate)) return candidate;
    const parent = path.dirname(cursor);
    if (!parent || parent === cursor) break;
    cursor = parent;
  }
  return "";
}

function resolveCxprUsePath(workspaceRoot, usePath, currentFilePath = "") {
  const normalized = usePath.endsWith(".cxpr") ? usePath : `${usePath}.cxpr`;
  const currentDir = currentFilePath ? path.dirname(currentFilePath) : workspaceRoot;
  const relative = currentDir ? path.join(currentDir, normalized) : "";
  if (pathExists(relative)) return relative;
  if (usePath.startsWith("presets/") && path.basename(currentDir) === "presets") {
    const siblingPreset = path.join(path.dirname(currentDir), normalized);
    if (pathExists(siblingPreset)) return siblingPreset;
  }
  if (usePath.startsWith("indicators/")) {
    const dynPath = resolveDynCxprUseFromAncestors(currentDir, usePath);
    if (dynPath) return dynPath;
  }
  const workspacePath = workspaceRoot ? path.join(workspaceRoot, normalized) : "";
  if (pathExists(workspacePath)) return workspacePath;
  for (const root of additionalImportRoots) {
    const candidate = path.join(root, normalized);
    if (pathExists(candidate)) return candidate;
  }
  return "";
}

function resolveDynCxprDirectoryFromAncestors(startDir, usePath) {
  let cursor = startDir || ".";
  for (;;) {
    const candidate = path.join(cursor, "libs", "dyn", "cxpr", usePath);
    if (pathExists(candidate) && fs.statSync(candidate).isDirectory()) return candidate;
    const parent = path.dirname(cursor);
    if (!parent || parent === cursor) break;
    cursor = parent;
  }
  return "";
}

function resolveCxprUseDirectory(workspaceRoot, usePath, currentFilePath = "") {
  const currentDir = currentFilePath ? path.dirname(currentFilePath) : workspaceRoot;
  const relative = currentDir ? path.join(currentDir, usePath) : "";
  if (pathExists(relative) && fs.statSync(relative).isDirectory()) return relative;
  if (usePath.startsWith("indicators/") || usePath === "indicators") {
    const dynPath = resolveDynCxprDirectoryFromAncestors(currentDir, usePath);
    if (dynPath) return dynPath;
  }
  const workspacePath = workspaceRoot ? path.join(workspaceRoot, usePath) : "";
  if (pathExists(workspacePath) && fs.statSync(workspacePath).isDirectory()) return workspacePath;
  for (const root of additionalImportRoots) {
    const candidate = path.join(root, usePath);
    if (pathExists(candidate) && fs.statSync(candidate).isDirectory()) return candidate;
  }
  return "";
}

function collectCxprUses(text) {
  const aliases = new Map();
  const useRe = /^\s*use\s+([A-Za-z0-9_./-]+)(?:\s+as\s+([A-Za-z_][A-Za-z0-9_]*))?\s*$/gm;
  let match;
  while ((match = useRe.exec(text)) !== null) {
    const importPath = match[1];
    const alias = match[2] || importPath.split("/").pop();
    aliases.set(alias, importPath);
  }

  const groupUseRe = /^\s*use\s*\{([^}]*)\}\s*from\s+([A-Za-z0-9_./-]+)/gm;
  while ((match = groupUseRe.exec(text)) !== null) {
    const importBase = match[2].replace(/\/$/, "");
    for (const nameMatch of match[1].matchAll(/\b([A-Za-z_][A-Za-z0-9_]*)(?:\s+as\s+([A-Za-z_][A-Za-z0-9_]*))?\b/g)) {
      const name = nameMatch[1];
      const alias = nameMatch[2] || name;
      aliases.set(alias, `${importBase}/${name}`);
    }
  }

  const listUseRe = /^\s*use\s+([^{}\n]+?)\s+from\s+([A-Za-z0-9_./-]+)/gm;
  while ((match = listUseRe.exec(text)) !== null) {
    const importBase = match[2].replace(/\/$/, "");
    for (const rawName of match[1].split(",")) {
      const name = rawName.trim();
      if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) continue;
      aliases.set(name, `${importBase}/${name}`);
    }
  }
  return aliases;
}

function collectCxprUseEntries(document) {
  const entries = [];
  const lines = stripCommentsPreserveLayout(document.getText()).split(/\r?\n/);
  for (let line = 0; line < lines.length; line += 1) {
    const lineText = lines[line];
    let match = lineText.match(/^(\s*use\s+)([A-Za-z0-9_./-]+)(?:\s+as\s+([A-Za-z_][A-Za-z0-9_]*))?\s*$/);
    if (match) {
      const start = match[1].length;
      entries.push({
        path: match[2],
        range: new vscode.Range(line, start, line, start + match[2].length)
      });
      continue;
    }

    match = lineText.match(/^(\s*use\s*\{)([^}]*)\}(\s*from\s+)([A-Za-z0-9_./-]+)/);
    if (match) {
      const base = match[4].replace(/\/$/, "");
      const itemOffset = match[1].length;
      for (const itemMatch of match[2].matchAll(/\b[A-Za-z_][A-Za-z0-9_]*\b/g)) {
        const name = itemMatch[0];
        const start = itemOffset + itemMatch.index;
        entries.push({
          path: `${base}/${name}`,
          range: new vscode.Range(line, start, line, start + name.length)
        });
      }
      continue;
    }

    match = lineText.match(/^(\s*use\s+)([^{}\n]+?)(\s+from\s+)([A-Za-z0-9_./-]+)/);
    if (match) {
      const base = match[4].replace(/\/$/, "");
      const itemOffset = match[1].length;
      for (const itemMatch of match[2].matchAll(/\b[A-Za-z_][A-Za-z0-9_]*\b/g)) {
        const name = itemMatch[0];
        const start = itemOffset + itemMatch.index;
        entries.push({
          path: `${base}/${name}`,
          range: new vscode.Range(line, start, line, start + name.length)
        });
      }
    }
  }
  return entries;
}

function collectCxprUseSyntaxDiagnostics(lines) {
  const diagnostics = [];
  const ident = "[A-Za-z_][A-Za-z0-9_]*";
  const pathPattern = "[A-Za-z0-9_./-]+";
  const singleUse = new RegExp(
    `^\\s*use\\s+${pathPattern}(?:\\s+as\\s+${ident})?\\s*$`
  );
  const listUse = new RegExp(
    `^\\s*use\\s+${ident}(?:\\s*,\\s*${ident})*\\s+from\\s+${pathPattern}\\s*$`
  );
  const groupUse = new RegExp(
    `^\\s*use\\s*\\{\\s*${ident}(?:\\s*,\\s*${ident})*\\s*\\}\\s+from\\s+${pathPattern}\\s*$`
  );

  for (let line = 0; line < lines.length; line += 1) {
    const lineText = lines[line];
    if (!/^\s*use\b/.test(lineText)) continue;
    if (singleUse.test(lineText) || listUse.test(lineText) || groupUse.test(lineText)) continue;

    const missingComma = new RegExp(
      `^\\s*use\\s+${ident}\\s+${ident}(?:\\s|,)?.*\\s+from\\s+${pathPattern}\\s*$`
    ).test(lineText);
    diagnostics.push(new vscode.Diagnostic(
      new vscode.Range(line, 0, line, lineText.length),
      missingComma
        ? "Invalid CXPR use declaration: expected ',' between import names"
        : "Invalid CXPR use declaration",
      vscode.DiagnosticSeverity.Error
    ));
  }
  return diagnostics;
}

function collectCxprDeclaredNames(text) {
  const names = new Set();
  const code = stripCommentsPreserveLayout(text);
  for (const name of collectInputNames(code)) {
    names.add(name);
    const dot = name.indexOf(".");
    if (dot > 0) names.add(name.slice(0, dot));
  }
  for (const name of collectStateNames(code)) names.add(name);
  for (const name of collectCxprUses(code).keys()) names.add(name);
  for (const match of code.matchAll(/^\s*\$([A-Za-z_][A-Za-z0-9_]*)\s*=/gm)) names.add(`$${match[1]}`);
  for (const name of collectPublicCallParams(code)) names.add(name);
  for (const match of code.matchAll(/^\s*(?:out\s+|state\s+|update\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*(?:=|:=)/gm)) {
    names.add(match[1]);
  }
  for (const match of code.matchAll(/^\s*out\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s|$)/gm)) names.add(match[1]);
  return names;
}

function collectCxprFunctionNames(text, document = null) {
  const names = new Set(BUILTIN_FUNCTIONS.keys());
  const code = stripCommentsPreserveLayout(text);
  for (const name of collectCxprUses(code).keys()) names.add(name);
  for (const match of code.matchAll(/^\s*fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(/gm)) names.add(match[1]);

  // Imported models expose their function declarations directly (for example
  // `use ts/smoothing` exposes `ts_ema_alpha`).  Keep this lookup local to the
  // workspace so diagnostics match the build-time CXPR import behavior.
  if (document) {
    const visited = new Set();
    const collectImported = (source, currentFilePath) => {
      for (const usePath of collectCxprUses(source).values()) {
        const importedPath = resolveCxprUsePath(
          workspaceRootFor(document),
          usePath,
          currentFilePath
        );
        if (!importedPath || visited.has(importedPath)) continue;
        visited.add(importedPath);
        let importedSource;
        try {
          importedSource = fs.readFileSync(importedPath, "utf8");
        } catch (_err) {
          continue;
        }
        const importedCode = stripCommentsPreserveLayout(importedSource);
        for (const match of importedCode.matchAll(/^\s*fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(/gm)) {
          names.add(match[1]);
        }
        collectImported(importedSource, importedPath);
      }
    };
    collectImported(text, document.uri.fsPath);
  }
  return names;
}

function collectCxprFunctionParamScopes(lines) {
  const scopes = [];
  for (let line = 0; line < lines.length; line += 1) {
    const match = lines[line].match(
      /^\s*fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/
    );
    if (!match) continue;

    const params = new Set(
      match[2]
        .split(",")
        .map((param) => param.trim())
        .filter((param) => /^[A-Za-z_][A-Za-z0-9_]*$/.test(param))
    );
    let endLine = line;
    let depth = lineBraceDelta(lines[line]);
    while (depth > 0 && endLine + 1 < lines.length) {
      endLine += 1;
      depth += lineBraceDelta(lines[endLine]);
    }
    scopes.push({ name: match[1], startLine: line, endLine, params });
    line = endLine;
  }
  return scopes;
}

function expressionStartColumn(lineText) {
  const code = stripLineComment(lineText);
  if (/^\s*(?:use|in|model)\b/.test(code)) return -1;
  if (/^\s*fn\b/.test(code)) return -1;
  if (outputRoleDeclaration(code)) return -1;
  const assign = code.search(/(?<![<>=!])=(?!=)/);
  if (assign >= 0) return assign + 1;
  const outMatch = code.match(/^(\s*out\s+[A-Za-z_][A-Za-z0-9_]*\s+)/);
  if (outMatch && code.slice(outMatch[1].length).trim()) return outMatch[1].length;
  return -1;
}

function buildCxprDiagnostics(document) {
  if (!document || document.languageId !== "cxpr" || document.uri.scheme !== "file") return [];
  const diagnostics = [];
  const workspaceRoot = workspaceRootFor(document);
  const text = document.getText();
  const code = stripCommentsPreserveLayout(text);
  const lines = code.split(/\r?\n/);
  const declared = collectCxprDeclaredNames(code);
  const functions = collectCxprFunctionNames(code, document);
  const functionParamScopes = collectCxprFunctionParamScopes(lines);
  const documentBlockLines = collectDocumentBlockLines(lines);
  const constants = new Set([...declared].filter((name) => name.startsWith("$")));

  diagnostics.push(...collectCxprUseSyntaxDiagnostics(lines));

  for (const entry of collectCxprUseEntries(document)) {
    if (!resolveCxprUsePath(workspaceRoot, entry.path, document.uri.fsPath)) {
      diagnostics.push(new vscode.Diagnostic(
        entry.range,
        `CXPR use target not found: ${entry.path}`,
        vscode.DiagnosticSeverity.Error
      ));
    }
  }

  for (let line = 0; line < lines.length; line += 1) {
    if (documentBlockLines.has(line)) continue;
    const lineText = lines[line];
    const exprStart = expressionStartColumn(lineText);
    if (exprStart < 0) continue;
    const expr = stripStringsPreserveLayout(lineText.slice(exprStart));
    const localParams = functionParamScopes.find(
      (scope) => line >= scope.startLine && line <= scope.endLine
    )?.params;

    for (const fieldMatch of expr.matchAll(/\.\s*([A-Za-z_][A-Za-z0-9_]*)\b/g)) {
      const field = fieldMatch[1];
      const fieldStart = exprStart + fieldMatch.index + fieldMatch[0].lastIndexOf(field);
      const beforeDot = lineText.slice(0, exprStart + fieldMatch.index);
      const alias = resolvePropertySourceAlias(beforeDot, text);
      if (!alias) continue;

      const metadata = loadIndicatorMetadata(document, alias);
      if (!metadata || metadata.outputs.length === 0) continue;
      if (metadata.outputs.some((output) => output.name === field)) continue;

      const knownFields = metadata.outputs.map((output) => output.name).join(", ");
      diagnostics.push(new vscode.Diagnostic(
        new vscode.Range(line, fieldStart, line, fieldStart + field.length),
        `Unknown CXPR field '${field}' on ${alias} output. Known fields: ${knownFields}`,
        vscode.DiagnosticSeverity.Error
      ));
    }

    for (const paramMatch of expr.matchAll(/\$([A-Za-z_][A-Za-z0-9_]*)\b/g)) {
      const name = `$${paramMatch[1]}`;
      if (constants.has(name)) continue;
      const start = exprStart + paramMatch.index;
      diagnostics.push(new vscode.Diagnostic(
        new vscode.Range(line, start, line, start + name.length),
        `Unknown CXPR parameter: ${name}`,
        vscode.DiagnosticSeverity.Error
      ));
    }

    for (const match of expr.matchAll(/\b[A-Za-z_][A-Za-z0-9_]*\b/g)) {
      const word = match[0];
      if (KEYWORD_TOKENS.has(word)) continue;
      const start = exprStart + match.index;
      const end = start + word.length;
      const prev = prevNonSpace(lineText, start - 1);
      const next = nextNonSpace(lineText, end);
      if (prev === "." || prev === "$" || next === "=") continue;
      if (next === "(") {
        if (functions.has(word)) continue;
        diagnostics.push(new vscode.Diagnostic(
          new vscode.Range(line, start, line, end),
          `Unknown CXPR function or import: ${word}`,
          vscode.DiagnosticSeverity.Error
        ));
        continue;
      }
      if (declared.has(word) || localParams?.has(word)) continue;
      diagnostics.push(new vscode.Diagnostic(
        new vscode.Range(line, start, line, end),
        `Unknown CXPR reference: ${word}`,
        vscode.DiagnosticSeverity.Error
      ));
    }
  }

  return diagnostics;
}

function collectCxprCallBindings(text) {
  const bindings = new Map();
  const assignmentRe = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(/gm;
  let match;
  while ((match = assignmentRe.exec(text)) !== null) {
    bindings.set(match[1], match[2]);
  }
  return bindings;
}

function collectCxprRecordFields(block) {
  const fields = [];
  const code = block.split(/\r?\n/).map(stripLineComment).join("\n");
  for (const item of splitTopLevelArgs(code)) {
    const match = item.match(/^\s*([A-Za-z_][A-Za-z0-9_]*)(?:\s*[=:]\s*([\s\S]+))?\s*$/);
    if (!match) continue;
    fields.push({
      name: match[1],
      expression: (match[2] || match[1]).trim()
    });
  }
  return fields;
}

function collectCxprRecordBindings(text) {
  const records = new Map();
  const code = text.split(/\r?\n/).map(stripLineComment).join("\n");
  const assignmentRe = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*\{/gm;
  let match;
  while ((match = assignmentRe.exec(code)) !== null) {
    const fields = collectCxprRecordFields(readBlockAfter(code, match.index));
    records.set(match[1], { name: match[1], fields });
  }
  return records;
}

function resolveLocalRecordField(beforeDot, records) {
  const objectMatch = beforeDot.match(/([A-Za-z_][A-Za-z0-9_]*)\s*$/);
  if (!objectMatch) return null;
  return records.get(objectMatch[1]) || null;
}

function resolvePropertySourceAlias(beforeDot, text) {
  const objectMatch = beforeDot.match(/([A-Za-z_][A-Za-z0-9_]*)\s*$/);
  if (objectMatch) {
    const boundAlias = collectCxprCallBindings(text).get(objectMatch[1]);
    if (boundAlias) return boundAlias;
  }

  const callMatch = beforeDot.match(/([A-Za-z_][A-Za-z0-9_]*)\s*\([^()]*\)\s*$/);
  return callMatch ? callMatch[1] : "";
}

function loadIndicatorMetadata(document, alias) {
  const text = document.getText();
  const usePath = collectCxprUses(text).get(alias);
  if (!usePath) return null;
  const filePath = resolveCxprUsePath(workspaceRootFor(document), usePath, document.uri.fsPath);
  if (!filePath) return null;
  try {
    return parseCxprIndicatorMetadata(
      fs.readFileSync(filePath, "utf8"),
      filePath,
      workspaceRootFor(document)
    );
  } catch (_err) {
    return null;
  }
}

function loadDocumentMetadata(document) {
  return parseCxprIndicatorMetadata(
    document.getText(),
    document.uri.fsPath,
    workspaceRootFor(document)
  );
}

function loadImportedFunctionMetadata(document, functionName) {
  const text = document.getText();
  for (const usePath of collectCxprUses(text).values()) {
    const filePath = resolveCxprUsePath(workspaceRootFor(document), usePath, document.uri.fsPath);
    if (!filePath) continue;
    try {
      const metadata = parseCxprIndicatorMetadata(
        fs.readFileSync(filePath, "utf8"),
        filePath,
        workspaceRootFor(document)
      );
      const fn = metadata.functions.find((candidate) => candidate.name === functionName);
      if (fn) return { metadata, fn };
    } catch (_err) {
      continue;
    }
  }
  return null;
}

function cxprMetadataFileLabel(metadata) {
  if (!metadata || !metadata.filePath) return "";
  if (metadata.workspaceRoot) {
    const relative = path.relative(metadata.workspaceRoot, metadata.filePath);
    if (relative && !relative.startsWith("..") && !path.isAbsolute(relative)) {
      return relative.split(path.sep).join("/");
    }
  }
  const normalized = metadata.filePath.split(path.sep).join("/");
  const marker = "/libs/dyn/cxpr/";
  const markerIndex = normalized.indexOf(marker);
  if (markerIndex >= 0) return `libs/dyn/cxpr/${normalized.slice(markerIndex + marker.length)}`;
  return normalized;
}

function appendMetadataFileMarkdown(markdown, metadata) {
  const fileLabel = cxprMetadataFileLabel(metadata);
  if (fileLabel) markdown.appendMarkdown(`File: \`${fileLabel}\`\n\n`);
}

function appendIndicatorMarkdown(markdown, metadata, alias) {
  const title = metadata.label || metadata.name || alias;
  markdown.appendMarkdown(`**${title}**`);
  if (metadata.name && metadata.name !== title) markdown.appendMarkdown(` \`${metadata.name}\``);
  markdown.appendMarkdown("\n\n");
  appendMetadataFileMarkdown(markdown, metadata);
  if (metadata.summary) markdown.appendMarkdown(`${metadata.summary}\n\n`);
  if (metadata.sourceArg) markdown.appendMarkdown(`Source input: \`${metadata.sourceArg}\`\n\n`);
  if (metadata.params.length > 0) {
    markdown.appendMarkdown("Parameters:\n");
    for (const param of metadata.params) {
      const defaultText = param.defaultValue ? ` = \`${param.defaultValue}\`` : "";
      const description = param.description ? ` - ${param.description}` : "";
      markdown.appendMarkdown(`- \`${param.name}\`${defaultText}${description}\n`);
    }
    markdown.appendMarkdown("\n");
  }
  if (metadata.outputs.length > 0) {
    markdown.appendMarkdown("Outputs:\n");
    for (const output of metadata.outputs) {
      const label = output.label && output.label !== output.name ? ` (${output.label})` : "";
      const description = output.description ? ` - ${output.description}` : "";
      markdown.appendMarkdown(`- \`${output.name}\`${label}${description}\n`);
    }
  }
}

function appendFunctionMarkdown(markdown, metadata, fn) {
  markdown.appendMarkdown(`**${fn.name}**`);
  if (metadata.name) markdown.appendMarkdown(` \`${metadata.name}\``);
  markdown.appendMarkdown("\n\n");
  appendMetadataFileMarkdown(markdown, metadata);
  if (fn.summary) markdown.appendMarkdown(`${fn.summary}\n\n`);
  if (fn.params.length > 0) {
    markdown.appendMarkdown("Inputs:\n");
    for (const param of fn.params) {
      const description = param.description ? ` - ${param.description}` : "";
      markdown.appendMarkdown(`- \`${param.name}\` (positional or named: \`${param.name}=...\`)${description}\n`);
    }
    markdown.appendMarkdown("\n");
  }
  if (fn.returns) {
    markdown.appendMarkdown(`Returns: ${fn.returns}`);
  } else if (fn.inferredReturnType) {
    markdown.appendMarkdown(`Returns: \`${fn.inferredReturnType}\` (inferred)`);
  }
}

function appendBuiltinFunctionMarkdown(markdown, name, builtin) {
  markdown.appendMarkdown(`**${name}** \`builtin\`\n\n`);
  if (builtin.summary) markdown.appendMarkdown(`${builtin.summary}\n\n`);
  if (builtin.signature) markdown.appendMarkdown(`Signature: \`${builtin.signature}\`\n\n`);
  if (builtin.returns) markdown.appendMarkdown(`Returns: \`${builtin.returns}\``);
}

function appendParamMarkdown(markdown, metadata, param) {
  markdown.appendMarkdown(`**$${param.name}**`);
  if (metadata.name) markdown.appendMarkdown(` \`${metadata.name}\``);
  markdown.appendMarkdown("\n\n");
  appendMetadataFileMarkdown(markdown, metadata);
  if (param.description) markdown.appendMarkdown(`${param.description}\n\n`);
  if (param.defaultValue) markdown.appendMarkdown(`Default: \`${param.defaultValue}\``);
}

function appendInputMarkdown(markdown, name) {
  markdown.appendMarkdown(`**${name}** \`input\`\n\n`);
  markdown.appendMarkdown("External model input supplied by the caller or execution environment.\n\n");
  markdown.appendMarkdown(`Use \`${name}\` directly as the current value supplied for this evaluation.\n\n`);
  markdown.appendMarkdown("Declare required inputs in a top-level `in` line, `in { ... }` block, or scoped `in root { field }` block.");
}

function appendInDeclarationMarkdown(markdown, inputs) {
  markdown.appendMarkdown("**in** `declaration`\n\n");
  markdown.appendMarkdown("Declares external model inputs supplied by the caller or execution environment.\n\n");
  if (inputs.length === 0) return;

  markdown.appendMarkdown("Inputs:\n");
  for (const name of inputs) {
    markdown.appendMarkdown(`- \`${name}\`\n`);
  }
}

function appendStateMarkdown(markdown, metadata, state) {
  markdown.appendMarkdown(`**${state.name}**`);
  if (metadata.name) markdown.appendMarkdown(` \`${metadata.name} state\``);
  markdown.appendMarkdown("\n\n");
  appendMetadataFileMarkdown(markdown, metadata);
  markdown.appendMarkdown("Persistent state value carried between ticks.\n\n");
  if (state.initialValue) markdown.appendMarkdown(`Initial value: \`${state.initialValue}\``);
}

function appendOutputMarkdown(markdown, metadata, output) {
  const title = output.label || output.name;
  markdown.appendMarkdown(`**${title}**`);
  markdown.appendMarkdown(metadata.name ? ` \`${metadata.name}.${output.name}\`\n\n` : ` \`output\`\n\n`);
  appendMetadataFileMarkdown(markdown, metadata);
  if (output.description) markdown.appendMarkdown(`${output.description}\n\n`);
  if (output.hover) markdown.appendMarkdown(`${output.hover}\n\n`);
  if (output.role) markdown.appendMarkdown(`Role: \`${output.role}\``);
}

function appendRecordMarkdown(markdown, metadata, record) {
  markdown.appendMarkdown(`**${record.name}** \`record\``);
  if (metadata.name) markdown.appendMarkdown(` \`${metadata.name}\``);
  markdown.appendMarkdown("\n\n");
  appendMetadataFileMarkdown(markdown, metadata);
  if (record.fields.length === 0) return;
  markdown.appendMarkdown("Fields:\n");
  for (const field of record.fields) {
    const expression = field.expression ? ` = \`${field.expression}\`` : "";
    markdown.appendMarkdown(`- \`${field.name}\`${expression}\n`);
  }
}

function appendRecordFieldMarkdown(markdown, metadata, record, field) {
  markdown.appendMarkdown(`**${record.name}.${field.name}** \`record field\``);
  if (metadata.name) markdown.appendMarkdown(` \`${metadata.name}\``);
  markdown.appendMarkdown("\n\n");
  appendMetadataFileMarkdown(markdown, metadata);
  if (field.expression) markdown.appendMarkdown(`Expression: \`${field.expression}\``);
}

function appendOutDeclarationMarkdown(markdown, metadata, outputs) {
  markdown.appendMarkdown("**out** `declaration`\n\n");
  markdown.appendMarkdown("Declares exported model outputs.\n\n");
  if (outputs.length === 0) return;

  markdown.appendMarkdown("Outputs:\n");
  for (const output of outputs) {
    const label = output.label && output.label !== output.name ? ` (${output.label})` : "";
    const description = output.description ? ` - ${output.description}` : "";
    const role = output.role ? ` [${output.role}]` : "";
    markdown.appendMarkdown(`- \`${output.name}\`${label}${role}${description}\n`);
  }
  if (metadata.name) markdown.appendMarkdown(`\nModel: \`${metadata.name}\`\n\n`);
  appendMetadataFileMarkdown(markdown, metadata);
}

function appendUseDeclarationMarkdown(markdown, document, aliases) {
  markdown.appendMarkdown("**use** `import group`\n\n");
  markdown.appendMarkdown("Imports CXPR models/functions into this document.\n\n");
  if (aliases.length === 0) return;

  markdown.appendMarkdown("Imports:\n");
  for (const item of aliases) {
    const metadata = loadIndicatorMetadata(document, item.alias);
    const title = metadata && (metadata.label || metadata.name)
      ? metadata.label || metadata.name
      : item.alias;
    const description = metadata && metadata.summary ? ` - ${metadata.summary}` : "";
    const aliasText = item.alias !== item.name ? ` as \`${item.alias}\`` : "";
    markdown.appendMarkdown(`- \`${item.name}\`${aliasText} -> \`${item.path}\`${description}\n`);
    if (metadata && metadata.filePath) {
      markdown.appendMarkdown(`  File: \`${cxprMetadataFileLabel(metadata)}\`\n`);
    }
    if (title && title !== item.name && title !== item.alias) {
      markdown.appendMarkdown(`  ${title}\n`);
    }
  }
}

function appendUseFolderMarkdown(markdown, document, importPath) {
  markdown.appendMarkdown(`**${importPath}** \`import folder\`\n\n`);
  const folderPath = resolveCxprUseDirectory(
    workspaceRootFor(document),
    importPath,
    document.uri.fsPath
  );
  if (folderPath) {
    markdown.appendMarkdown(`Folder: \`${cxprMetadataFileLabel({ filePath: folderPath })}/\`\n\n`);
  }
  markdown.appendMarkdown("Imported symbols are resolved as `.cxpr` files below this folder.");
}

function isOutputDeclarationName(lineText, wordRange) {
  const match = lineText.match(/^(\s*out\s+)([A-Za-z_][A-Za-z0-9_]*)/);
  if (!match) return false;
  const start = match[1].length;
  const end = start + match[2].length;
  return wordRange.start.character >= start && wordRange.end.character <= end;
}

function declarationNamesFromLine(lineText, keyword) {
  const code = stripLineComment(lineText);
  const match = code.match(new RegExp(`^\\s*${keyword}\\b\\s*(.*)$`));
  if (!match) return [];
  const body = match[1].replace(/[{}]/g, " ").trim();
  if (!body) return [];
  return [...body.matchAll(/\b[A-Za-z_][A-Za-z0-9_]*\b/g)].map((candidate) => candidate[0]);
}

function outputNamesFromLine(lineText) {
  const roleDeclaration = outputRoleDeclaration(lineText);
  if (roleDeclaration) return [roleDeclaration.output];
  return declarationNamesFromLine(lineText, "out");
}

function inputNamesFromLine(lineText) {
  const code = stripLineComment(lineText);
  const structMatch = code.match(/^\s*in\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{([^}]*)\}/);
  if (structMatch) {
    return [...structMatch[2].matchAll(/\b[A-Za-z_][A-Za-z0-9_]*\b/g)]
      .map((candidate) => `${structMatch[1]}.${candidate[0]}`);
  }
  return declarationNamesFromLine(lineText, "in");
}

function declaredInputNameAtLine(lineText, wordRange, fallbackWord) {
  const code = stripLineComment(lineText);
  const structMatch = code.match(/^\s*in\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{/);
  if (!structMatch) return fallbackWord;
  const rootStart = code.indexOf(structMatch[1]);
  const rootEnd = rootStart + structMatch[1].length;
  if (wordRange.start.character >= rootStart && wordRange.end.character <= rootEnd) {
    return structMatch[1];
  }
  return `${structMatch[1]}.${fallbackWord}`;
}

function structInputRootAtDocumentLine(document, lineNumber) {
  let root = null;
  for (let line = 0; line <= lineNumber; line += 1) {
    const text = stripLineComment(document.lineAt(line).text);
    const startMatch = text.match(/^\s*in\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{/);
    if (startMatch) root = startMatch[1];
    if (root && line === lineNumber) return root;
    if (root && text.includes("}")) root = null;
  }
  return null;
}

function isDeclarationListValue(lineText, wordRange, keyword) {
  const code = stripLineComment(lineText);
  const match = code.match(new RegExp(`^(\\s*${keyword}\\b)(.*)$`));
  if (!match) return false;
  const bodyStart = match[1].length;
  if (wordRange.start.character < bodyStart) return false;
  const body = code.slice(bodyStart);
  const word = lineText.slice(wordRange.start.character, wordRange.end.character);
  for (const candidate of body.matchAll(/\b[A-Za-z_][A-Za-z0-9_]*\b/g)) {
    if (candidate[0] !== word) continue;
    const start = bodyStart + candidate.index;
    const end = start + candidate[0].length;
    if (wordRange.start.character >= start && wordRange.end.character <= end) return true;
  }
  return false;
}

function findTextLocation(filePath, patterns) {
  let text;
  try {
    text = fs.readFileSync(filePath, "utf8");
  } catch (_err) {
    return new vscode.Location(vscode.Uri.file(filePath), new vscode.Position(0, 0));
  }

  const lines = text.split(/\r?\n/);
  for (const pattern of patterns) {
    for (let line = 0; line < lines.length; line += 1) {
      const match = lines[line].match(pattern);
      if (!match) continue;
      const start = match.index + (match[1] ? match[0].indexOf(match[1]) : 0);
      return new vscode.Location(vscode.Uri.file(filePath), new vscode.Position(line, Math.max(start, 0)));
    }
  }

  return new vscode.Location(vscode.Uri.file(filePath), new vscode.Position(0, 0));
}

function findCxprNameLocation(filePath, name) {
  return findTextLocation(filePath, [
    new RegExp(`^\\s*model\\s+(${name})\\b`),
    new RegExp(`^\\s*name\\s+(${name})\\b`),
    new RegExp(`^\\s*fn\\s+(${name})\\b`)
  ]);
}

function findCxprOutputLocation(filePath, name) {
  return findTextLocation(filePath, [
    new RegExp(`^\\s*out\\s+(${name})\\b`)
  ]);
}

function findInputDefinition(document, name) {
  let inBlock = false;
  let structRoot = null;
  let wantedRoot = null;
  let wantedField = name;
  const dot = name.indexOf(".");
  if (dot > 0) {
    wantedRoot = name.slice(0, dot);
    wantedField = name.slice(dot + 1);
  }
  for (let line = 0; line < document.lineCount; line += 1) {
    const text = stripLineComment(document.lineAt(line).text);
    const inlineMatch = text.match(/^\s*in\s+([A-Za-z_][A-Za-z0-9_]*)\s*$/);
    if (!wantedRoot && inlineMatch && inlineMatch[1] === name) {
      const start = text.indexOf(name);
      return new vscode.Location(document.uri, new vscode.Position(line, start));
    }

    const inlineStructMatch = text.match(/^\s*in\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{([^}]*)\}/);
    if (inlineStructMatch && (!wantedRoot || inlineStructMatch[1] === wantedRoot)) {
      const wordRe = /\b[A-Za-z_][A-Za-z0-9_]*\b/g;
      let match;
      while ((match = wordRe.exec(inlineStructMatch[2])) !== null) {
        if (match[0] !== wantedField) continue;
        const bodyStart = text.indexOf(inlineStructMatch[2]);
        return new vscode.Location(document.uri, new vscode.Position(line, bodyStart + match.index));
      }
    }

    const structStartMatch = text.match(/^\s*in\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{/);
    if (structStartMatch) {
      inBlock = true;
      structRoot = structStartMatch[1];
    } else if (/^\s*in\s*\{/.test(text)) {
      inBlock = true;
      structRoot = null;
    }
    if (inBlock && /^\s*\}/.test(text)) {
      inBlock = false;
      structRoot = null;
      continue;
    }

    const wordRe = /\b[A-Za-z_][A-Za-z0-9_]*\b/g;
    if (inBlock) {
      let match;
      while ((match = wordRe.exec(text)) !== null) {
        if (match[0] === "in") continue;
        if (wantedRoot && structRoot !== wantedRoot) continue;
        if (match[0] !== wantedField) continue;
        return new vscode.Location(document.uri, new vscode.Position(line, match.index));
      }
    }
  }
  return null;
}

function findLocalFunctionDefinition(document, name) {
  const pattern = new RegExp(`^\\s*fn\\s+(${name})\\b`);
  for (let line = 0; line < document.lineCount; line += 1) {
    const text = stripLineComment(document.lineAt(line).text);
    const match = text.match(pattern);
    if (!match) continue;
    const start = match.index + match[0].indexOf(match[1]);
    return new vscode.Location(document.uri, new vscode.Position(line, Math.max(start, 0)));
  }
  return null;
}

class CxprHoverProvider {
  provideHover(document, position) {
    const paramRange = document.getWordRangeAtPosition(position, /\$[A-Za-z_][A-Za-z0-9_]*/);
    if (paramRange) {
      const paramName = document.getText(paramRange).slice(1);
      const metadata = loadDocumentMetadata(document);
      const param = metadata.params.find((candidate) => candidate.name === paramName);
      if (param) {
        const markdown = new vscode.MarkdownString();
        appendParamMarkdown(markdown, metadata, param);
        return new vscode.Hover(markdown, paramRange);
      }
    }

    const wordRange = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
    if (!wordRange) return null;

    const word = document.getText(wordRange);
    const lineText = document.lineAt(position.line).text;
    const text = document.getText();
    const beforeWord = lineText.slice(0, wordRange.start.character);
    const afterWord = lineText.slice(wordRange.end.character);
    const inputNames = collectInputNames(text);
    const localMetadata = loadDocumentMetadata(document);
    const recordBindings = collectCxprRecordBindings(text);
    const functionScope = collectCxprFunctionParamScopes(
      stripCommentsPreserveLayout(text).split(/\r?\n/)
    ).find((scope) => position.line >= scope.startLine && position.line <= scope.endLine);

    const outputRole = outputRoleDeclaration(lineText);
    if (
      outputRole &&
      wordRange.start.character === outputRole.roleStart &&
      wordRange.end.character === outputRole.roleEnd
    ) {
      const markdown = new vscode.MarkdownString();
      markdown.appendMarkdown(`**${outputRole.role}** \`output role\`\n\n`);
      markdown.appendMarkdown(`Semantic role for output \`${outputRole.output}\`.\n\n`);
      markdown.appendMarkdown(
        `Equivalent metadata: \`out ${outputRole.output} { role = "${outputRole.role}" }\`. ` +
        "Hosts can use the role without depending on the output name."
      );
      return new vscode.Hover(markdown, wordRange);
    }

    if (functionScope?.params.has(word)) {
      const markdown = new vscode.MarkdownString();
      markdown.appendMarkdown(`**${word}** \`${functionScope.name} parameter\`\n\n`);
      markdown.appendMarkdown(
        `Pass positionally or by name: \`${functionScope.name}(${word}=...)\``
      );
      return new vscode.Hover(markdown, wordRange);
    }

    if (word === "out" && beforeWord.trim().length === 0) {
      const localMetadata = loadDocumentMetadata(document);
      const names = outputNamesFromLine(lineText);
      const outputs = names.map((name) => (
        localMetadata.outputs.find((candidate) => candidate.name === name) || { name }
      ));
      const markdown = new vscode.MarkdownString();
      appendOutDeclarationMarkdown(markdown, localMetadata, outputs);
      return new vscode.Hover(markdown, wordRange);
    }

    if (word === "in" && beforeWord.trim().length === 0) {
      const names = inputNamesFromLine(lineText);
      if (names.length > 0) {
        const markdown = new vscode.MarkdownString();
        appendInDeclarationMarkdown(markdown, names);
        return new vscode.Hover(markdown, wordRange);
      }
    }

    if (word === "use" && beforeWord.trim().length === 0) {
      const aliases = groupedUseAliasesFromLine(lineText);
      if (aliases.length > 0) {
        const markdown = new vscode.MarkdownString();
        appendUseDeclarationMarkdown(markdown, document, aliases);
        return new vscode.Hover(markdown, wordRange);
      }
    }

    const useImportPath = useImportPathAtLine(lineText, wordRange);
    if (useImportPath) {
      const markdown = new vscode.MarkdownString();
      appendUseFolderMarkdown(markdown, document, useImportPath);
      return new vscode.Hover(markdown, wordRange);
    }

    if (isGroupedUseImportName(lineText, wordRange.start.character, wordRange.end.character)) {
      const metadata = loadIndicatorMetadata(document, word);
      if (metadata) {
        const markdown = new vscode.MarkdownString();
        appendIndicatorMarkdown(markdown, metadata, word);
        return new vscode.Hover(markdown, wordRange);
      }
    }

    if (nextNonSpace(afterWord, 0) === ".") {
      const record = recordBindings.get(word);
      if (record) {
        const markdown = new vscode.MarkdownString();
        appendRecordMarkdown(markdown, localMetadata, record);
        return new vscode.Hover(markdown, wordRange);
      }

      const alias = collectCxprCallBindings(text).get(word);
      if (alias) {
        const metadata = loadIndicatorMetadata(document, alias);
        if (metadata) {
          const markdown = new vscode.MarkdownString();
          appendIndicatorMarkdown(markdown, metadata, alias);
          return new vscode.Hover(markdown, wordRange);
        }
      }
    }

    if (beforeWord.endsWith(".")) {
      const inputName = fullInputNameAt(lineText, word, beforeWord);
      if (inputNames.has(inputName)) {
        const markdown = new vscode.MarkdownString();
        appendInputMarkdown(markdown, inputName);
        return new vscode.Hover(markdown, wordRange);
      }

      const record = resolveLocalRecordField(beforeWord.slice(0, -1), recordBindings);
      if (record) {
        const field = record.fields.find((candidate) => candidate.name === word);
        if (!field) return null;
        const markdown = new vscode.MarkdownString();
        appendRecordFieldMarkdown(markdown, localMetadata, record, field);
        return new vscode.Hover(markdown, wordRange);
      }

      const alias = resolvePropertySourceAlias(beforeWord.slice(0, -1), text);
      if (!alias) return null;
      const metadata = loadIndicatorMetadata(document, alias);
      if (!metadata) return null;
      const output = metadata.outputs.find((candidate) => candidate.name === word);
      if (!output) return null;
      const markdown = new vscode.MarkdownString();
      appendOutputMarkdown(markdown, metadata, output);
      return new vscode.Hover(markdown, wordRange);
    }

    if (inputNames.has(word) && !beforeWord.endsWith(".")) {
      const markdown = new vscode.MarkdownString();
      appendInputMarkdown(markdown, word);
      return new vscode.Hover(markdown, wordRange);
    }

    if (nextNonSpace(afterWord, 0) === "(") {
      const localMetadata = loadDocumentMetadata(document);
      const localFn = localMetadata.functions.find((candidate) => candidate.name === word);
      if (localFn) {
        const markdown = new vscode.MarkdownString();
        appendFunctionMarkdown(markdown, localMetadata, localFn);
        return new vscode.Hover(markdown, wordRange);
      }

      const metadata = loadIndicatorMetadata(document, word);
      if (metadata) {
        const markdown = new vscode.MarkdownString();
        appendIndicatorMarkdown(markdown, metadata, word);
        return new vscode.Hover(markdown, wordRange);
      }

      const imported = loadImportedFunctionMetadata(document, word);
      if (imported) {
        const markdown = new vscode.MarkdownString();
        appendFunctionMarkdown(markdown, imported.metadata, imported.fn);
        return new vscode.Hover(markdown, wordRange);
      }

      const builtin = BUILTIN_FUNCTIONS.get(word);
      if (builtin) {
        const markdown = new vscode.MarkdownString();
        appendBuiltinFunctionMarkdown(markdown, word, builtin);
        return new vscode.Hover(markdown, wordRange);
      }
    }

    if (isDeclarationListValue(lineText, wordRange, "out")) {
      const localMetadata = loadDocumentMetadata(document);
      const output = localMetadata.outputs.find((candidate) => candidate.name === word) || { name: word };
      const markdown = new vscode.MarkdownString();
      appendOutputMarkdown(markdown, localMetadata, output);
      return new vscode.Hover(markdown, wordRange);
    }

    if (isDeclarationListValue(lineText, wordRange, "in")) {
      const inputName = declaredInputNameAtLine(lineText, wordRange, word);
      const names = inputNamesFromLine(lineText);
      const markdown = new vscode.MarkdownString();
      appendInputMarkdown(markdown, names.includes(inputName) ? inputName : word);
      return new vscode.Hover(markdown, wordRange);
    }

    {
      const structRoot = structInputRootAtDocumentLine(document, position.line);
      const inputName = structRoot ? `${structRoot}.${word}` : "";
      if (inputName && inputNames.has(inputName)) {
        const markdown = new vscode.MarkdownString();
        appendInputMarkdown(markdown, inputName);
        return new vscode.Hover(markdown, wordRange);
      }
    }

    if (isOutputDeclarationName(lineText, wordRange)) {
      const output = localMetadata.outputs.find((candidate) => candidate.name === word);
      if (output && (output.description || output.hover || output.label || output.role)) {
        const markdown = new vscode.MarkdownString();
        appendOutputMarkdown(markdown, localMetadata, output);
        return new vscode.Hover(markdown, wordRange);
      }
    }

    const state = localMetadata.states.find((candidate) => candidate.name === word);
    if (state) {
      const markdown = new vscode.MarkdownString();
      appendStateMarkdown(markdown, localMetadata, state);
      return new vscode.Hover(markdown, wordRange);
    }

    return null;
  }
}

class CxprDefinitionProvider {
  provideDefinition(document, position) {
    const lineText = document.lineAt(position.line).text;
    const useMatch = lineText.match(/^(\s*use\s+)([A-Za-z0-9_./-]+)(?:\s+as\s+[A-Za-z_][A-Za-z0-9_]*)?\s*$/);
    if (useMatch) {
      const start = useMatch[1].length;
      const end = start + useMatch[2].length;
      if (position.character < start || position.character > end) return null;

      const targetPath = resolveCxprUsePath(
        workspaceRootFor(document),
        useMatch[2],
        document.uri.fsPath
      );
      if (!targetPath) return null;

      return new vscode.Location(
        vscode.Uri.file(targetPath),
        new vscode.Position(0, 0)
      );
    }

    const wordRange = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
    if (!wordRange) return null;
    const beforeWord = lineText.slice(0, wordRange.start.character);
    const afterWord = lineText.slice(wordRange.end.character);
    const word = document.getText(wordRange);
    const text = document.getText();

    if (isGroupedUseImportName(lineText, wordRange.start.character, wordRange.end.character)) {
      const usePath = collectCxprUses(text).get(word);
      if (!usePath) return null;
      const targetPath = resolveCxprUsePath(workspaceRootFor(document), usePath, document.uri.fsPath);
      if (targetPath) return findCxprNameLocation(targetPath, word);

      const fromBase = lineText.match(/\bfrom\s+([A-Za-z0-9_./-]+)/)?.[1] || "";
      const targetDirectory = fromBase
        ? resolveCxprUseDirectory(workspaceRootFor(document), fromBase, document.uri.fsPath)
        : "";
      if (!targetDirectory) return null;
      return fs.readdirSync(targetDirectory, { withFileTypes: true })
        .filter((entry) => entry.isFile() && entry.name.endsWith(".cxpr"))
        .sort((left, right) => left.name.localeCompare(right.name))
        .map((entry) => new vscode.Location(
          vscode.Uri.file(path.join(targetDirectory, entry.name)),
          new vscode.Position(0, 0)
        ));
    }

    const inputNames = collectInputNames(text);
    if (inputNames.has(word) && !beforeWord.endsWith(".")) {
      return findInputDefinition(document, word);
    }

    if (beforeWord.endsWith(".")) {
      const inputName = fullInputNameAt(lineText, word, beforeWord);
      if (inputNames.has(inputName)) return findInputDefinition(document, inputName);

      const alias = resolvePropertySourceAlias(beforeWord.slice(0, -1), text);
      if (!alias) return null;
      const metadata = loadIndicatorMetadata(document, alias);
      if (!metadata) return null;
      const output = metadata.outputs.find((candidate) => candidate.name === word);
      if (!output) return null;
      return findCxprOutputLocation(metadata.filePath, word);
    }

    if (nextNonSpace(afterWord, 0) === "(") {
      const localDefinition = findLocalFunctionDefinition(document, word);
      if (localDefinition) return localDefinition;

      const usePath = collectCxprUses(text).get(word);
      if (!usePath) return null;
      const targetPath = resolveCxprUsePath(workspaceRootFor(document), usePath, document.uri.fsPath);
      if (!targetPath) return null;
      return findCxprNameLocation(targetPath, word);
    }

    if (nextNonSpace(afterWord, 0) !== ".") return null;

    const alias = collectCxprCallBindings(text).get(word);
    if (!alias) return null;

    const usePath = collectCxprUses(text).get(alias);
    if (!usePath) return null;

    const targetPath = resolveCxprUsePath(workspaceRootFor(document), usePath, document.uri.fsPath);
    if (!targetPath) return null;
    return findCxprNameLocation(targetPath, alias);
  }
}

module.exports = {
  buildDocumentTokens,
  CxprDefinitionProvider,
  CxprHoverProvider,
  collectCxprUses,
  configureImportRoots,
  parseCxprIndicatorMetadata,
  resolveCxprUsePath
};
