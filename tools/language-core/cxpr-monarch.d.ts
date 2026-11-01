export interface CxprMonarchLanguage {
  defaultToken: string;
  keywords: readonly string[];
  tokenizer: Record<string, any[]>;
}
export function createCxprMonarchLanguage(): CxprMonarchLanguage;
