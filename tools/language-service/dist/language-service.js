"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.LanguageService = void 0;
const CxprFormatter_1 = require("./CxprFormatter");
function range(item) {
    return {
        start: { line: item.startLine, character: item.startChar },
        end: { line: item.endLine, character: item.endChar }
    };
}
function candidateKind(candidate) {
    if (candidate.kind)
        return candidate.kind;
    return candidate.signature || candidate.minArgs !== undefined ? "function" : "variable";
}
function completionDetail(candidate) {
    if (candidate.signature)
        return candidate.signature;
    return candidate.builtin ? "CXPR builtin" : `CXPR ${candidate.kind || "symbol"}`;
}
function hoverMarkdown(resolution, sourceUri) {
    const title = `**${resolution.name}** \`CXPR ${resolution.builtin ? "builtin" : resolution.kind}\``;
    const displaySignature = resolution.typedSignature ?? resolution.signature;
    const signature = displaySignature ? `\n\n\`${displaySignature}\`` : "";
    const type = resolution.type && resolution.type !== "unknown"
        ? `\n\nReturns \`${resolution.type}\`.` : "";
    const provenance = resolution.provenance
        ? `\n\nInput provenance: \`${resolution.provenance}\`.` : "";
    const documentation = resolution.documentation ? `\n\n${resolution.documentation}` : "";
    const inputs = resolution.inputs?.length
        ? `\n\n**Inputs**\n${resolution.inputs.map((item) => `- \`${item.name}: ${item.type}\` _(${item.provenance})_`).join("\n")}` : "";
    const outputs = resolution.outputs?.length
        ? `\n\n**Outputs**\n${resolution.outputs.map((item) => `- \`${item.name}: ${item.type}\``).join("\n")}` : "";
    const source = sourceUri ? `\n\nSource: \`${sourceUri.replaceAll("`", "\\`")}\`` : "";
    return title + signature + type + provenance + documentation + inputs + outputs + source;
}
function importHoverMarkdown(name, uri, outline) {
    const model = outline.find((item) => item.kind === "model")?.name;
    const exports = outline.filter((item) => item.kind === "function").map((item) => item.name);
    const module = model ? `\n\nModule: \`${model}\`.` : "";
    const exported = exports.length
        ? `\n\nExports:\n${exports.map((item) => `- \`${item}\``).join("\n")}` : "";
    return `**${name}** \`CXPR import\`${module}${exported}\n\nSource: \`${uri.replaceAll("`", "\\`")}\``;
}
function namespaceHover(source, pos) {
    const line = source.split("\n")[pos.line] || "";
    const from = line.match(/\bfrom\s+([A-Za-z_][A-Za-z0-9_.]*)/);
    if (!from || from.index === undefined)
        return null;
    const namespace = from[1];
    const start = from.index + from[0].lastIndexOf(namespace);
    const offset = pos.column - start;
    if (offset < 0 || offset > namespace.length)
        return null;
    const segments = namespace.split(".");
    let segmentStart = 0;
    for (let index = 0; index < segments.length; index += 1) {
        const segment = segments[index];
        const segmentEnd = segmentStart + segment.length;
        if (offset >= segmentStart && offset <= segmentEnd) {
            const folder = segments.slice(0, index + 1).join("/") + "/";
            return {
                markdown: `**${segment}** \`CXPR namespace\`\n\nFolder: \`${folder}\``,
                range: {
                    start: { line: pos.line, character: start + segmentStart },
                    end: { line: pos.line, character: start + segmentEnd }
                }
            };
        }
        segmentStart = segmentEnd + 1;
    }
    return null;
}
function overlaps(left, right) {
    return left.line === right.line && left.start < right.start + right.length &&
        right.start < left.start + left.length;
}
function diagnosticLength(message) {
    const symbol = message.match(/unknown (?:symbol|function) '([^']+)'/i)?.[1];
    const requirement = message.match(/cannot bind ([A-Za-z_][A-Za-z0-9_]*)/i)?.[1];
    return Math.max(1, symbol?.length ?? requirement?.length ?? 1);
}
function sourceTokenLength(source, line, character, message) {
    const tail = source.split("\n")[line]?.slice(character) ?? "";
    if (/numeric literals are not supported/i.test(message)) {
        return tail.match(/^0[bBxX][A-Za-z0-9_]*/)?.[0].length ?? 1;
    }
    return tail.match(/^[$A-Za-z_][$A-Za-z0-9_]*/)?.[0].length ?? 1;
}
class LanguageService {
    analyzer;
    workspace;
    formatter;
    constructor(analyzer, workspace, formatter = new CxprFormatter_1.CxprFormatter()) {
        this.analyzer = analyzer;
        this.workspace = workspace;
        this.formatter = formatter;
    }
    /** @inheritdoc */
    formatDocument(source, options) {
        return this.formatter.format(source, options);
    }
    /** @inheritdoc */
    async getDiagnostics(source, name) {
        const analysis = await this.analyzer.analyzeDocument(source, name);
        const diagnostics = (analysis.diagnostics || []).map((item) => {
            const line = item.line || 0;
            const character = item.column || 0;
            return { severity: item.severity || 1, source: "cxpr", message: item.message,
                range: { start: { line, character },
                    end: { line, character: character + diagnosticLength(item.message) } } };
        });
        if (!analysis.ok) {
            const line = Math.max(0, (analysis.line || 1) - 1);
            const character = Math.max(0, (analysis.column || 1) - 1);
            diagnostics.push({ severity: 1, source: "cxpr", message: analysis.error || "CXPR parse failed",
                range: { start: { line, character },
                    end: { line, character: character + sourceTokenLength(source, line, character, analysis.error || "") } } });
        }
        const lines = source.split("\n");
        for (const fn of analysis.functions || []) {
            if (fn.builtin)
                continue;
            const token = (analysis.sourceTokens || []).find((candidate) => candidate.type === "function" && lines[candidate.line]?.slice(candidate.start, candidate.start + candidate.length) === fn.name);
            if (!token)
                continue;
            const resolution = await this.analyzer.resolveAt(source, name, { line: token.line, column: token.start });
            if (resolution && resolution.kind !== "unresolved")
                continue;
            const imported = resolution ? await this.resolveImported(name, resolution) : null;
            if (imported)
                continue;
            diagnostics.push({
                severity: 1,
                source: "cxpr",
                message: `Expression references unknown function '${fn.name}'`,
                range: {
                    start: { line: token.line, character: token.start },
                    end: { line: token.line, character: token.start + token.length }
                }
            });
        }
        diagnostics.push(...await this.getImportDiagnostics(source, name));
        diagnostics.push(...await this.getModelResultFieldDiagnostics(source, name));
        return diagnostics;
    }
    /**
     * Validate grouped `use` namespaces against the active workspace module roots.
     * @param source Complete CXPR source text.
     * @param name Source file name used as the import-resolution origin.
     * @returns Diagnostics whose ranges cover unresolved `from` namespaces.
     */
    async getImportDiagnostics(source, name) {
        const diagnostics = [];
        const lines = source.split("\n");
        const groupedImport = /^\s*use\s+(.+?)\s+from\s+([A-Za-z_][A-Za-z0-9_.]*)\s*(?:#.*)?$/;
        for (let line = 0; line < lines.length; line += 1) {
            const match = lines[line].match(groupedImport);
            if (!match || match.index === undefined)
                continue;
            const imports = match[1].split(",").map((item) => item.trim())
                .filter((item) => /^[A-Za-z_][A-Za-z0-9_]*$/.test(item));
            if (imports.length === 0)
                continue;
            const namespace = match[2];
            const resolved = await Promise.all(imports.map((item) => this.workspace.resolveImport(`${namespace}.${item}`, name)));
            const missing = imports.filter((_item, index) => !resolved[index]);
            if (missing.length === 0)
                continue;
            const start = match.index + match[0].lastIndexOf(namespace);
            diagnostics.push({
                severity: 1,
                source: "cxpr",
                message: `Namespace '${namespace}' does not contain: ${missing.join(", ")}`,
                range: {
                    start: { line, character: start },
                    end: { line, character: start + namespace.length }
                }
            });
        }
        return diagnostics;
    }
    /** Validate fields read from local bindings initialized by model calls. */
    async getModelResultFieldDiagnostics(source, name) {
        const lines = source.split("\n");
        const calls = new Map();
        const diagnostics = [];
        const assignment = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(/;
        for (let line = 0; line < lines.length; line += 1) {
            const match = lines[line].match(assignment);
            if (!match || match.index === undefined)
                continue;
            calls.set(match[1], {
                functionName: match[2], line,
                column: match.index + match[0].lastIndexOf(match[2])
            });
        }
        for (const [binding, call] of calls) {
            let imported = false;
            let resolution = await this.analyzer.resolveAt(source, name, { line: call.line, column: call.column });
            if (!resolution)
                continue;
            if (resolution.kind === "unresolved") {
                imported = true;
                resolution = (await this.resolveImported(name, resolution))?.resolution ?? resolution;
            }
            if (resolution.kind === "unresolved")
                continue;
            const explicitStruct = resolution.type === "struct" ||
                /^\s*[^-]+->\s*\{/.test(resolution.typedSignature || "");
            if (!imported && !explicitStruct)
                continue;
            // Model metadata flattens nested record leaves (for example `bass.age`).
            // Field access at this level must be checked against the record root.
            const fields = new Set((resolution.outputs || []).map((output) => output.name.split(".")[0]));
            if (fields.size === 0)
                continue;
            const scalar = resolution.type !== "struct" &&
                !/^\s*[^-]+->\s*\{/.test(resolution.typedSignature || "") && fields.size === 1;
            const access = new RegExp(`(?<![A-Za-z0-9_.])${binding}\\.([A-Za-z_][A-Za-z0-9_]*)`, "g");
            for (let line = 0; line < lines.length; line += 1) {
                for (const match of lines[line].matchAll(access)) {
                    const field = match[1];
                    if (!scalar && fields.has(field))
                        continue;
                    const character = (match.index || 0) + binding.length + 1;
                    diagnostics.push({
                        severity: 1,
                        source: "cxpr",
                        message: scalar
                            ? `Cannot access field '${field}' on scalar result '${binding}'`
                            : `Unknown field '${field}' on result '${binding}'`,
                        range: {
                            start: { line, character },
                            end: { line, character: character + field.length }
                        }
                    });
                }
            }
            if (!scalar) {
                const reference = new RegExp(`\\b${binding}\\b`, "g");
                const numericOperatorBefore = /(?:[+\-*/%]|<=|>=|==|!=|<|>)\s*$/;
                const numericOperatorAfter = /^\s*(?:[+\-*/%]|<=|>=|==|!=|<|>)/;
                for (let line = call.line; line < lines.length; line += 1) {
                    for (const match of lines[line].matchAll(reference)) {
                        const character = match.index ?? 0;
                        const after = lines[line].slice(character + binding.length);
                        if (after.startsWith("."))
                            continue;
                        if (line === call.line && character < lines[line].indexOf("="))
                            continue;
                        const before = lines[line].slice(0, character);
                        if (!numericOperatorBefore.test(before) && !numericOperatorAfter.test(after))
                            continue;
                        diagnostics.push({
                            severity: 1,
                            source: "cxpr",
                            message: `Model result '${binding}' is a struct and cannot be used as a scalar; select one of: ${[...fields].map((field) => `${binding}.${field}`).join(", ")}`,
                            range: {
                                start: { line, character },
                                end: { line, character: character + binding.length }
                            }
                        });
                    }
                }
            }
        }
        return diagnostics;
    }
    async getCompletions(source, pos, name) {
        const candidates = await this.analyzer.completeAt(source, name, pos);
        return candidates.map((candidate) => ({
            label: candidate.name,
            kind: candidateKind(candidate),
            detail: completionDetail(candidate),
            insertText: candidate.name
        }));
    }
    async getHover(source, pos, name) {
        if (/^\s*use\b/.test(source.split("\n")[pos.line] || "")) {
            const namespace = namespaceHover(source, pos);
            if (namespace)
                return namespace;
        }
        let resolution = await this.analyzer.resolveAt(source, name, pos);
        const resultField = await this.getModelResultFieldHover(source, pos, name);
        if (resultField)
            return resultField;
        if (!resolution)
            return null;
        if (resolution.builtin && resolution.name === "sum") {
            resolution = await this.inferSumResult(source, pos, name, resolution);
        }
        if (resolution.kind !== "unresolved")
            return { markdown: hoverMarkdown(resolution) };
        if (/^\s*use\b/.test(source.split("\n")[pos.line] || "")) {
            const module = await this.resolveImportedModule(name, resolution);
            if (module) {
                return { markdown: importHoverMarkdown(resolution.name, module.uri, module.outline) };
            }
        }
        const imports = resolution.imports?.length
            ? resolution.imports : this.importSpecsForSymbol(source, resolution.name);
        const imported = await this.resolveImported(name, { ...resolution, imports });
        return imported ? { markdown: hoverMarkdown(imported.resolution, imported.uri) } : null;
    }
    /**
     * Resolves a field read from a local binding initialized by an imported model call.
     * This source-level fallback remains available when an earlier parse error prevents
     * the analyzer from retaining the binding and import relationship.
     * @param source Complete consumer source.
     * @param pos Hover position on the result field.
     * @param name Consumer document name.
     * @returns Hover metadata for a declared model output, or null when not applicable.
     */
    async getModelResultFieldHover(source, pos, name) {
        const line = source.split("\n")[pos.line] || "";
        const access = /\b([A-Za-z_][A-Za-z0-9_]*)((?:\.[A-Za-z_][A-Za-z0-9_]*)+)/g;
        let binding = "";
        let field = "";
        let fieldStart = -1;
        let fieldLength = 0;
        for (const match of line.matchAll(access)) {
            const segments = match[2].slice(1).split(".");
            let start = (match.index || 0) + match[1].length + 1;
            for (let index = 0; index < segments.length; index += 1) {
                const segment = segments[index];
                if (pos.column >= start && pos.column <= start + segment.length) {
                    binding = match[1];
                    field = segments.slice(0, index + 1).join(".");
                    fieldStart = start;
                    fieldLength = segment.length;
                    break;
                }
                start += segment.length + 1;
            }
            if (binding)
                break;
        }
        if (!binding)
            return null;
        const assignment = new RegExp(`^[ \\t]*${binding.replace(/[.*+?^${}()|[\\]\\]/g, "\\$&")}[ \\t]*=[ \\t]*([A-Za-z_][A-Za-z0-9_]*)[ \\t]*\\(`, "m");
        const call = source.match(assignment);
        if (!call || call.index === undefined)
            return null;
        const prefix = source.slice(0, call.index);
        const callLine = prefix.split("\n").length - 1;
        const callText = source.split("\n")[callLine] || "";
        const callColumn = callText.indexOf(call[1]);
        let resolution = await this.analyzer.resolveAt(source, name, {
            line: callLine, column: Math.max(0, callColumn)
        });
        if (!resolution)
            return null;
        if (resolution.kind === "unresolved") {
            const imports = resolution.imports?.length
                ? resolution.imports : this.importSpecsForSymbol(source, call[1]);
            resolution = (await this.resolveImported(name, { ...resolution, imports }))?.resolution ?? resolution;
        }
        const output = resolution.outputs?.find((candidate) => candidate.name === field);
        const nested = (resolution.outputs || []).filter((candidate) => candidate.name.startsWith(`${field}.`));
        if (!output && nested.length === 0)
            return null;
        const leafName = field.split(".").at(-1) || field;
        const nestedOutputs = nested.map((candidate) => ({
            ...candidate, name: candidate.name.slice(field.length + 1)
        }));
        return {
            markdown: hoverMarkdown({
                name: leafName,
                kind: "output",
                type: output?.type || "struct",
                outputs: nestedOutputs.length ? nestedOutputs : undefined,
                documentation: `Output of \`${call[1]}\`.`
            }),
            range: {
                start: { line: pos.line, character: fieldStart },
                end: { line: pos.line, character: fieldStart + fieldLength }
            }
        };
    }
    /**
     * Finds import specifications that can provide a symbol.
     * @param source Complete consumer source.
     * @param symbol Imported symbol name.
     * @returns Candidate module specifications in source order.
     */
    importSpecsForSymbol(source, symbol) {
        const specs = [];
        for (const line of source.split("\n")) {
            const direct = line.match(/^\s*use\s+([A-Za-z_][A-Za-z0-9_]*)\s*$/);
            if (direct?.[1] === symbol)
                specs.push(symbol);
            const grouped = line.match(/^\s*use\s+(.+?)\s+from\s+([A-Za-z_][A-Za-z0-9_.]*)\s*$/);
            if (!grouped)
                continue;
            const names = grouped[1].split(",").map((candidate) => candidate.trim());
            if (names.includes(symbol))
                specs.push(`${grouped[2]}.${symbol}`);
        }
        return specs;
    }
    /**
     * Infers the shape-preserving return of a sum call from model calls in its arguments.
     * @param source Complete consumer source.
     * @param pos Hover position on the sum function.
     * @param name Consumer document name.
     * @param fallback Static builtin metadata used when no record shape is found.
     * @returns Contextual sum metadata with a structured return when applicable.
     */
    async inferSumResult(source, pos, name, fallback) {
        const lines = source.split("\n");
        const lineStart = lines.slice(0, pos.line).reduce((offset, line) => offset + line.length + 1, 0);
        const line = lines[pos.line] || "";
        const tokenStart = line.lastIndexOf("sum", pos.column);
        if (tokenStart < 0)
            return fallback;
        const open = source.indexOf("(", lineStart + tokenStart + 3);
        if (open < 0)
            return fallback;
        let depth = 1;
        let close = open + 1;
        let quote = null;
        for (; close < source.length && depth > 0; close += 1) {
            const character = source[close];
            if (quote) {
                if (character === quote && source[close - 1] !== "\\")
                    quote = null;
                continue;
            }
            if (character === '"' || character === "'")
                quote = character;
            else if (character === "(")
                depth += 1;
            else if (character === ")")
                depth -= 1;
        }
        if (depth !== 0)
            return fallback;
        const bodyStart = open + 1;
        const body = source.slice(bodyStart, close - 1);
        for (const match of body.matchAll(/\b([A-Za-z_][A-Za-z0-9_]*)\s*\(/g)) {
            const absolute = bodyStart + (match.index ?? 0);
            const prefix = source.slice(0, absolute);
            const callLine = prefix.split("\n").length - 1;
            const callColumn = absolute - (prefix.lastIndexOf("\n") + 1);
            let candidate = await this.analyzer.resolveAt(source, name, { line: callLine, column: callColumn });
            if (!candidate || candidate.builtin)
                continue;
            if (candidate.kind === "unresolved") {
                candidate = (await this.resolveImported(name, candidate))?.resolution ?? candidate;
            }
            if (candidate.kind === "unresolved" || candidate.type !== "struct")
                continue;
            const outputs = candidate.outputs || [];
            if (!outputs.length)
                continue;
            const shape = outputs.map((output) => `${output.name}: ${output.type}`).join(", ");
            return {
                ...fallback,
                type: "struct",
                outputs,
                typedSignature: `sum(value, ...values) -> { ${shape} }`
            };
        }
        return fallback;
    }
    async getDefinitions(source, pos, name) {
        const namespaceDefinitions = await this.getNamespaceDefinitions(source, pos, name);
        if (namespaceDefinitions.length)
            return namespaceDefinitions;
        const resolution = await this.analyzer.resolveAt(source, name, pos);
        if (!resolution)
            return [];
        if (resolution.definition)
            return [{ uri: name, range: range(resolution.definition) }];
        if (resolution.kind !== "unresolved")
            return [];
        const imported = await this.resolveImported(name, resolution);
        return imported?.resolution.definition
            ? [{ uri: imported.uri, range: range(imported.resolution.definition), source: imported.source }]
            : [];
    }
    /**
     * Resolves a grouped import namespace to the declarations exported through it.
     * @param source Complete consumer source.
     * @param pos Cursor position within the dotted namespace after `from`.
     * @param name Consumer document name used as the import-resolution origin.
     * @returns Definition locations for the imported symbols, or an empty list outside a namespace.
     */
    async getNamespaceDefinitions(source, pos, name) {
        const line = source.split("\n")[pos.line] || "";
        const grouped = line.match(/^\s*use\s+(.+?)\s+from\s+([A-Za-z_][A-Za-z0-9_.]*)\s*(?:#.*)?$/);
        if (!grouped || grouped.index === undefined)
            return [];
        const namespace = grouped[2];
        const start = grouped.index + grouped[0].lastIndexOf(namespace);
        if (pos.column < start || pos.column > start + namespace.length)
            return [];
        const imports = grouped[1].split(",").map((item) => item.trim())
            .filter((item) => /^[A-Za-z_][A-Za-z0-9_]*$/.test(item));
        const definitions = await Promise.all(imports.map(async (symbol) => {
            const imported = await this.resolveImported(name, {
                name: symbol, kind: "unresolved", imports: [`${namespace}.${symbol}`]
            });
            return imported?.resolution.definition
                ? { uri: imported.uri, range: range(imported.resolution.definition), source: imported.source }
                : null;
        }));
        return definitions.filter((item) => item !== null);
    }
    async getDocumentSymbols(source, name) {
        const analysis = await this.analyzer.analyzeDocument(source, name);
        return (analysis.outline || []).map((item) => ({
            name: item.name || item.kind, detail: item.value || "", kind: item.kind,
            range: range(item), selectionRange: range(item)
        }));
    }
    async getFoldingRanges(source, name) {
        const analysis = await this.analyzer.analyzeDocument(source, name);
        return (analysis.folds || []).map((item) => ({ startLine: item.startLine, startCharacter: item.startChar,
            endLine: item.endLine, endCharacter: item.endChar, kind: "region" }));
    }
    async getSemanticTokens(source, name) {
        const analysis = await this.analyzer.analyzeDocument(source, name);
        const semantic = analysis.tokens || [];
        const tokens = (analysis.sourceTokens || []).filter((sourceToken) => !semantic.some((token) => overlaps(sourceToken, token))).concat(semantic);
        return tokens.sort((left, right) => left.line - right.line || left.start - right.start);
    }
    async resolveImported(from, unresolved) {
        for (const spec of unresolved.imports || []) {
            const target = await this.workspace.resolveImport(spec, from);
            if (!target)
                continue;
            const importedSource = target.source ?? await this.workspace.readFile(target.path);
            const analysis = await this.analyzer.analyzeDocument(importedSource, target.path);
            const exported = (analysis.outline || []).find((item) => item.name === unresolved.name && (item.kind === "model" || item.kind === "function"));
            if (exported) {
                const line = importedSource.split("\n")[exported.startLine] || "";
                const declarationColumn = line.indexOf(unresolved.name, exported.startChar);
                const declaration = await this.analyzer.resolveAt(importedSource, target.path, {
                    line: exported.startLine,
                    column: declarationColumn >= 0 ? declarationColumn : exported.startChar
                });
                if (declaration?.definition && declaration.name === unresolved.name &&
                    declaration.kind !== "unresolved") {
                    return {
                        uri: target.path,
                        source: importedSource,
                        resolution: this.withImportedOutputs(declaration, analysis.outline || [], importedSource)
                    };
                }
            }
            const probeLine = importedSource.split("\n").length;
            const probePrefix = "__cxpr_probe = ";
            const probedSource = `${importedSource}\n${probePrefix}${unresolved.name}`;
            const declaration = await this.analyzer.resolveAt(probedSource, target.path, { line: probeLine, column: probePrefix.length });
            if (!declaration?.definition || declaration.name !== unresolved.name || declaration.kind === "unresolved")
                continue;
            if (declaration.kind !== "function" && declaration.kind !== "model")
                continue;
            return {
                uri: target.path,
                source: importedSource,
                resolution: this.withImportedOutputs(declaration, analysis.outline || [], importedSource)
            };
        }
        return null;
    }
    /**
     * Adds model output shape retained by document analysis to an imported declaration.
     * @param resolution Imported model declaration resolution.
     * @param outline Imported document outline.
     * @param source Imported CXPR source.
     * @returns Resolution enriched with scalar or structured output metadata.
     */
    withImportedOutputs(resolution, outline, source) {
        const structuredSyntax = /^\s*out\s*\{/m.test(source);
        const anonymousOutput = outline.some((item) => item.kind === "anonymousOutput") ||
            /^\s*out\s+[^\s{][^=\n]*$/m.test(source);
        const outlinedOutputs = outline.filter((item) => item.kind === "output")
            .map((item) => ({ name: item.name, type: "number" }));
        const declaredNames = [];
        for (const match of source.matchAll(/^\s*out\s+(?!\{)([^=\n]+?)(?:\s*=|\s*$)/gm)) {
            for (const candidate of match[1].split(",")) {
                const name = candidate.trim().match(/^[A-Za-z_][A-Za-z0-9_]*/)?.[0];
                if (name)
                    declaredNames.push(name);
            }
        }
        for (const match of source.matchAll(/^\s*out\s*\{([\s\S]*?)\}/gm)) {
            for (const candidate of match[1].split(/[,\n]/)) {
                const name = candidate.trim().match(/^[A-Za-z_][A-Za-z0-9_]*/)?.[0];
                if (name)
                    declaredNames.push(name);
            }
        }
        const recordLeaves = this.recordOutputLeaves(source);
        const declaredOutputs = [...new Set(recordLeaves.length ? recordLeaves : declaredNames)]
            .map((name) => ({ name, type: "number" }));
        // The analyzer outline is parser-backed and preserves top-level fields of
        // nested records. The regex fallback stops at the first nested `}` and may
        // collect inner fields while dropping later roots such as `bass`.
        const outputs = recordLeaves.length
            ? declaredOutputs
            : outlinedOutputs.length ? outlinedOutputs
                : resolution.outputs?.length ? resolution.outputs : declaredOutputs;
        if (!outputs.length)
            return anonymousOutput ? { ...resolution, type: "number" } : resolution;
        const structured = structuredSyntax || outputs.length > 1;
        return { ...resolution, type: structured ? "struct" : "number", outputs };
    }
    /**
     * Extracts leaf paths from a structured `out { ... }` declaration.
     * @param source Complete imported model source.
     * @returns Dotted output paths in declaration order.
     */
    recordOutputLeaves(source) {
        const match = /\bout\s*\{/.exec(source);
        if (!match)
            return [];
        const leaves = [];
        this.parseOutputRecord(source, match.index + match[0].length - 1, "", leaves);
        return leaves;
    }
    /**
     * Parses one output record while skipping scalar expressions and nested delimiters.
     * @param source Complete imported model source.
     * @param open Index of the record's opening brace.
     * @param prefix Dotted parent path, or empty for the output root.
     * @param leaves Mutable destination for discovered scalar leaf paths.
     * @returns Index immediately after the record's closing brace.
     */
    parseOutputRecord(source, open, prefix, leaves) {
        let index = open + 1;
        while (index < source.length) {
            while (index < source.length && /[\s,]/.test(source[index]))
                index += 1;
            if (source[index] === "}")
                return index + 1;
            const name = source.slice(index).match(/^[A-Za-z_][A-Za-z0-9_]*/)?.[0];
            if (!name) {
                index += 1;
                continue;
            }
            index += name.length;
            while (/\s/.test(source[index] || ""))
                index += 1;
            const path = prefix ? `${prefix}.${name}` : name;
            if (source[index] !== "=") {
                leaves.push(path);
                while (index < source.length && source[index] !== "," && source[index] !== "}")
                    index += 1;
                continue;
            }
            index += 1;
            while (/\s/.test(source[index] || ""))
                index += 1;
            if (source[index] === "{") {
                index = this.parseOutputRecord(source, index, path, leaves);
                continue;
            }
            leaves.push(path);
            let round = 0;
            let square = 0;
            while (index < source.length) {
                const character = source[index];
                if (character === "(")
                    round += 1;
                else if (character === ")")
                    round -= 1;
                else if (character === "[")
                    square += 1;
                else if (character === "]")
                    square -= 1;
                else if (round === 0 && square === 0 && (character === "," || character === "}"))
                    break;
                index += 1;
            }
        }
        return index;
    }
    async resolveImportedModule(from, unresolved) {
        for (const spec of unresolved.imports || []) {
            const moduleName = spec.replace(/\.cxpr$/, "").split("/").at(-1);
            if (moduleName !== unresolved.name)
                continue;
            const target = await this.workspace.resolveImport(spec, from);
            if (!target)
                continue;
            const importedSource = target.source ?? await this.workspace.readFile(target.path);
            const analysis = await this.analyzer.analyzeDocument(importedSource, target.path);
            if (!analysis.ok)
                continue;
            return { uri: target.path, outline: analysis.outline || [] };
        }
        return null;
    }
}
exports.LanguageService = LanguageService;
