import type { IAnalyzer, ICxprLanguageService, IWorkspace, LanguageCompletion, LanguageDefinition, LanguageDiagnostic, LanguageDocumentSymbol, LanguageFoldingRange, LanguageHover, LanguageSemanticToken, Position } from "./contracts";
import { type ICxprFormatter, type CxprFormattingOptions } from "./CxprFormatter";
export declare class LanguageService implements ICxprLanguageService {
    private readonly analyzer;
    readonly workspace: IWorkspace;
    private readonly formatter;
    constructor(analyzer: IAnalyzer, workspace: IWorkspace, formatter?: ICxprFormatter);
    /** @inheritdoc */
    formatDocument(source: string, options?: CxprFormattingOptions): string;
    /** @inheritdoc */
    getDiagnostics(source: string, name: string): Promise<LanguageDiagnostic[]>;
    /**
     * Validate grouped `use` namespaces against the active workspace module roots.
     * @param source Complete CXPR source text.
     * @param name Source file name used as the import-resolution origin.
     * @returns Diagnostics whose ranges cover unresolved `from` namespaces.
     */
    private getImportDiagnostics;
    /** Validate fields read from local bindings initialized by model calls. */
    private getModelResultFieldDiagnostics;
    getCompletions(source: string, pos: Position, name: string): Promise<LanguageCompletion[]>;
    getHover(source: string, pos: Position, name: string): Promise<LanguageHover | null>;
    /**
     * Resolves a field read from a local binding initialized by an imported model call.
     * This source-level fallback remains available when an earlier parse error prevents
     * the analyzer from retaining the binding and import relationship.
     * @param source Complete consumer source.
     * @param pos Hover position on the result field.
     * @param name Consumer document name.
     * @returns Hover metadata for a declared model output, or null when not applicable.
     */
    private getModelResultFieldHover;
    /**
     * Finds import specifications that can provide a symbol.
     * @param source Complete consumer source.
     * @param symbol Imported symbol name.
     * @returns Candidate module specifications in source order.
     */
    private importSpecsForSymbol;
    /**
     * Infers the shape-preserving return of a sum call from model calls in its arguments.
     * @param source Complete consumer source.
     * @param pos Hover position on the sum function.
     * @param name Consumer document name.
     * @param fallback Static builtin metadata used when no record shape is found.
     * @returns Contextual sum metadata with a structured return when applicable.
     */
    private inferSumResult;
    getDefinitions(source: string, pos: Position, name: string): Promise<LanguageDefinition[]>;
    /**
     * Resolves a grouped import namespace to the declarations exported through it.
     * @param source Complete consumer source.
     * @param pos Cursor position within the dotted namespace after `from`.
     * @param name Consumer document name used as the import-resolution origin.
     * @returns Definition locations for the imported symbols, or an empty list outside a namespace.
     */
    private getNamespaceDefinitions;
    getDocumentSymbols(source: string, name: string): Promise<LanguageDocumentSymbol[]>;
    getFoldingRanges(source: string, name: string): Promise<LanguageFoldingRange[]>;
    getSemanticTokens(source: string, name: string): Promise<LanguageSemanticToken[]>;
    private resolveImported;
    /**
     * Adds model output shape retained by document analysis to an imported declaration.
     * @param resolution Imported model declaration resolution.
     * @param outline Imported document outline.
     * @param source Imported CXPR source.
     * @returns Resolution enriched with scalar or structured output metadata.
     */
    private withImportedOutputs;
    /**
     * Extracts leaf paths from a structured `out { ... }` declaration.
     * @param source Complete imported model source.
     * @returns Dotted output paths in declaration order.
     */
    private recordOutputLeaves;
    /**
     * Parses one output record while skipping scalar expressions and nested delimiters.
     * @param source Complete imported model source.
     * @param open Index of the record's opening brace.
     * @param prefix Dotted parent path, or empty for the output root.
     * @param leaves Mutable destination for discovered scalar leaf paths.
     * @returns Index immediately after the record's closing brace.
     */
    private parseOutputRecord;
    private resolveImportedModule;
}
