const path = require("path");
const { fileURLToPath, pathToFileURL } = require("url");

let workspaceRoots = [];

class Position {
  constructor(line, character) { this.line = line; this.character = character; }
}

class Range {
  constructor(startOrLine, startCharacterOrEnd, endLine, endCharacter) {
    if (typeof startOrLine === "number") {
      this.start = new Position(startOrLine, startCharacterOrEnd);
      this.end = new Position(endLine, endCharacter);
    } else {
      this.start = startOrLine;
      this.end = startCharacterOrEnd;
    }
  }
}

class MarkdownString {
  constructor(value = "") { this.value = value; this.isTrusted = false; }
  appendMarkdown(value) { this.value += value; return this; }
}

class Hover {
  constructor(contents, range) {
    this.contents = { kind: "markdown", value: contents?.value ?? String(contents || "") };
    if (range) this.range = range;
  }
}

class Location {
  constructor(uri, rangeOrPosition) {
    this.uri = typeof uri === "string" ? uri : uri.toString();
    this.range = rangeOrPosition?.start
      ? rangeOrPosition
      : new Range(rangeOrPosition, rangeOrPosition);
  }
}

class Uri {
  static file(filePath) {
    const fsPath = path.resolve(filePath);
    return { fsPath, scheme: "file", toString: () => pathToFileURL(fsPath).toString() };
  }
}

function configureWorkspaceRoots(roots) {
  workspaceRoots = (roots || []).map((root) => path.resolve(root));
}

function asDocument(document) {
  const fsPath = fileURLToPath(document.uri);
  const uri = { fsPath, scheme: "file", toString: () => document.uri };
  return {
    uri,
    languageId: document.languageId,
    get lineCount() { return document.lineCount; },
    getText(range) { return document.getText(range); },
    lineAt(line) { return { text: document.getText({ start: { line, character: 0 }, end: { line: line + 1, character: 0 } }).replace(/\r?\n$/, "") }; },
    getWordRangeAtPosition(position, pattern = /[A-Za-z_][A-Za-z0-9_]*/) {
      const text = this.lineAt(position.line).text;
      const flags = pattern.flags.replace("g", "");
      const re = new RegExp(pattern.source, `${flags}g`);
      for (const match of text.matchAll(re)) {
        const start = match.index;
        const end = start + match[0].length;
        if (position.character >= start && position.character <= end) {
          return new Range(position.line, start, position.line, end);
        }
      }
      return undefined;
    }
  };
}

const workspace = {
  getWorkspaceFolder(uri) {
    const root = workspaceRoots.find((candidate) => uri.fsPath === candidate || uri.fsPath.startsWith(`${candidate}${path.sep}`));
    return root ? { uri: Uri.file(root) } : undefined;
  },
  getConfiguration() { return { get: (_name, fallback) => fallback }; }
};

module.exports = {
  Diagnostic: class Diagnostic {}, DiagnosticSeverity: {}, Hover, Location,
  MarkdownString, Position, Range, Uri, asDocument, configureWorkspaceRoots,
  workspace
};
