import type { IAnalyzer, ICxprLanguageService, IWorkspace, LanguageCompletion, LanguageDefinition, LanguageDiagnostic, LanguageDocumentSymbol, LanguageFoldingRange, LanguageHover, LanguageSemanticToken, Position } from "./contracts";
export declare class LanguageService implements ICxprLanguageService {
    private readonly analyzer;
    readonly workspace: IWorkspace;
    constructor(analyzer: IAnalyzer, workspace: IWorkspace);
    getDiagnostics(source: string, name: string): Promise<LanguageDiagnostic[]>;
    getCompletions(source: string, pos: Position, name: string): Promise<LanguageCompletion[]>;
    getHover(source: string, pos: Position, name: string): Promise<LanguageHover | null>;
    getDefinitions(source: string, pos: Position, name: string): Promise<LanguageDefinition[]>;
    getDocumentSymbols(source: string, name: string): Promise<LanguageDocumentSymbol[]>;
    getFoldingRanges(source: string, name: string): Promise<LanguageFoldingRange[]>;
    getSemanticTokens(source: string, name: string): Promise<LanguageSemanticToken[]>;
    private resolveImported;
    private resolveImportedModule;
}
