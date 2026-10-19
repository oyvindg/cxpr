export interface Position { line: number; column: number }
export interface LanguagePosition { line: number; character: number }
export interface LanguageRange { start: LanguagePosition; end: LanguagePosition }

export interface Diagnostic { severity?: number; message: string; line?: number; column?: number }
export interface OutlineItem {
  kind: string; name: string; value?: string
  startLine: number; startChar: number; endLine: number; endChar: number
}
export interface SemanticToken { line: number; start: number; length: number; type: string }
export interface FunctionInfo {
  name: string; kind?: string; builtin: boolean; minArgs?: number; maxArgs?: number
  signature?: string; type?: string; documentation?: string
  parameters?: Array<{ name: string; type: string }>
}
export interface Fold { startLine: number; startChar: number; endLine: number; endChar: number }
export interface SourceRequirement {
  kind: "resample"; source: string; interval: string; durationNs: string; resultType: string
  range: { startLine: number; startChar: number; endLine: number; endChar: number }
}
export interface Analysis {
  ok: boolean; diagnostics?: Diagnostic[]; outline?: OutlineItem[]; tokens?: SemanticToken[]
  sourceTokens?: SemanticToken[]; requirements?: SourceRequirement[]
  functions?: FunctionInfo[]; folds?: Fold[]; error?: string; line?: number; column?: number
}
export interface SymbolResolution {
  name: string; kind: string; type?: string; builtin?: boolean; signature?: string
  minArgs?: number; maxArgs?: number; documentation?: string
  imports?: string[]
  definition?: { startLine: number; startChar: number; endLine: number; endChar: number }
}
export interface CompletionCandidate extends FunctionInfo {}
export interface ImportTarget { path: string; source?: string }

export interface IAnalyzer {
  analyzeDocument(source: string, name: string): Promise<Analysis>
  resolveAt(source: string, name: string, pos: Position): Promise<SymbolResolution | null>
  completeAt(source: string, name: string, pos: Position): Promise<CompletionCandidate[]>
  queryFunctions(): Promise<FunctionInfo[]>
}
export interface IWorkspace {
  readFile(path: string): Promise<string>
  importDir(): string
  resolveImport(spec: string, from: string): Promise<ImportTarget | null>
}

export interface LanguageDiagnostic { severity: number; source: "cxpr"; message: string; range: LanguageRange }
export interface LanguageCompletion {
  label: string; kind: string; detail?: string; insertText: string; replaceRange?: LanguageRange
}
export interface LanguageHover { markdown: string; range?: LanguageRange }
export interface LanguageSemanticToken extends SemanticToken {}
export interface LanguageDefinition { uri: string; range: LanguageRange }
export interface LanguageDocumentSymbol { name: string; detail: string; kind: string; range: LanguageRange; selectionRange: LanguageRange }
export interface LanguageFoldingRange { startLine: number; startCharacter: number; endLine: number; endCharacter: number; kind: "region" }

export interface ICxprLanguageService {
  getDiagnostics(source: string, name: string): Promise<LanguageDiagnostic[]>
  getCompletions(source: string, pos: Position, name: string): Promise<LanguageCompletion[]>
  getHover(source: string, pos: Position, name: string): Promise<LanguageHover | null>
  getDefinitions(source: string, pos: Position, name: string): Promise<LanguageDefinition[]>
  getDocumentSymbols(source: string, name: string): Promise<LanguageDocumentSymbol[]>
  getFoldingRanges(source: string, name: string): Promise<LanguageFoldingRange[]>
  getSemanticTokens(source: string, name: string): Promise<LanguageSemanticToken[]>
}
