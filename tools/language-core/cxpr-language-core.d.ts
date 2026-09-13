export const CXPR_TOKEN_TYPES: readonly string[];
export const CXPR_TYPE_NAMES: readonly string[];
export const CXPR_DEFAULT_TOKEN_COLORS: Record<string, string>;
export const CXPR_KEYWORDS: readonly string[];
export const CXPR_OPERATOR_TOKENS: readonly string[];
export const CXPR_PUNCTUATION_CHARS: readonly string[];
export const CXPR_SINGLE_CHAR_OPERATORS: readonly string[];
export const CXPR_NUMBER_PATTERN: RegExp;

export interface CxprUseAlias {
  name: string;
  alias: string;
  path: string;
}

export interface ZeroBasedRange {
  start?: {
    character?: number;
  };
  end?: {
    character?: number;
  };
  startColumn?: number;
  endColumn?: number;
}

export function nextNonSpace(text: string, index: number): string;
export function prevNonSpace(text: string, index: number): string;
export function stripLineComment(lineText: string): string;
export function isGroupedUseImportName(text: string, tokenStart: number, tokenEnd: number): boolean;
export function isUseImportPathSegment(text: string, tokenStart: number, tokenEnd: number): boolean;
export function groupedUseAliasesFromLine(lineText: string): CxprUseAlias[];
export function useImportPathAtLine(lineText: string, range: ZeroBasedRange): string;
