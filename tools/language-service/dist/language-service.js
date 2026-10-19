"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.LanguageService = void 0;
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
    const signature = resolution.signature ? `\n\n\`${resolution.signature}\`` : "";
    const type = resolution.type && resolution.type !== "unknown"
        ? `\n\nReturns \`${resolution.type}\`.` : "";
    const documentation = resolution.documentation ? `\n\n${resolution.documentation}` : "";
    const source = sourceUri ? `\n\nSource: \`${sourceUri.replaceAll("`", "\\`")}\`` : "";
    return title + signature + type + documentation + source;
}
function importHoverMarkdown(name, uri, outline) {
    const model = outline.find((item) => item.kind === "model")?.name;
    const exports = outline.filter((item) => item.kind === "function").map((item) => item.name);
    const module = model ? `\n\nModule: \`${model}\`.` : "";
    const exported = exports.length
        ? `\n\nExports:\n${exports.map((item) => `- \`${item}\``).join("\n")}` : "";
    return `**${name}** \`CXPR import\`${module}${exported}\n\nSource: \`${uri.replaceAll("`", "\\`")}\``;
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
    constructor(analyzer, workspace) {
        this.analyzer = analyzer;
        this.workspace = workspace;
    }
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
        const resolution = await this.analyzer.resolveAt(source, name, pos);
        if (!resolution)
            return null;
        if (resolution.kind !== "unresolved")
            return { markdown: hoverMarkdown(resolution) };
        if (/^\s*use\b/.test(source.split("\n")[pos.line] || "")) {
            const module = await this.resolveImportedModule(name, resolution);
            if (module) {
                return { markdown: importHoverMarkdown(resolution.name, module.uri, module.outline) };
            }
        }
        const imported = await this.resolveImported(name, resolution);
        return imported ? { markdown: hoverMarkdown(imported.resolution, imported.uri) } : null;
    }
    async getDefinitions(source, pos, name) {
        const resolution = await this.analyzer.resolveAt(source, name, pos);
        if (!resolution)
            return [];
        if (resolution.definition)
            return [{ uri: name, range: range(resolution.definition) }];
        if (resolution.kind !== "unresolved")
            return [];
        const imported = await this.resolveImported(name, resolution);
        return imported?.resolution.definition
            ? [{ uri: imported.uri, range: range(imported.resolution.definition) }]
            : [];
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
            const probeLine = importedSource.split("\n").length;
            const probePrefix = "__cxpr_probe = ";
            const probedSource = `${importedSource}\n${probePrefix}${unresolved.name}`;
            const declaration = await this.analyzer.resolveAt(probedSource, target.path, { line: probeLine, column: probePrefix.length });
            if (!declaration?.definition || declaration.name !== unresolved.name || declaration.kind === "unresolved")
                continue;
            return { uri: target.path, resolution: declaration };
        }
        return null;
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
