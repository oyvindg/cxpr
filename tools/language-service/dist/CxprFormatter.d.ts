/** Formatting options understood by the CXPR formatter. */
export interface CxprFormattingOptions {
    /** Number of columns in one indentation level. */
    tabSize?: number;
    /** Use spaces when true and tabs when false. */
    insertSpaces?: boolean;
}
/** Contract for deterministic, editor-neutral CXPR source formatting. */
export interface ICxprFormatter {
    /**
     * Formats a complete CXPR document while preserving its newline convention.
     * @param source Complete CXPR source text.
     * @param options Optional indentation settings supplied by the editor.
     * @returns Formatted CXPR source text.
     */
    format(source: string, options?: CxprFormattingOptions): string;
}
/** Applies the indentation conventions used by the legacy CXPR VSIX. */
export declare class CxprFormatter implements ICxprFormatter {
    /** @inheritdoc */
    format(source: string, options?: CxprFormattingOptions): string;
    /**
     * Limits vertical whitespace to one empty physical line.
     * @param lines Formatted source lines.
     * @returns Lines with repeated empty lines collapsed.
     */
    private collapseBlankLines;
    /**
     * Replaces strings and comments with inert characters before delimiter analysis.
     * @param line Trimmed source line.
     * @param startsInBlockComment Whether a block comment started on an earlier line.
     * @returns Delimiter-safe code and block-comment state for the next line.
     */
    private codeShape;
    /** @param character Delimiter to count. @param text Safe source text. @returns Occurrence count. */
    private count;
    /** @param text Safe source line. @returns Whether the line starts a continuation. */
    private startsContinuation;
    /** @param text Safe source line. @returns Whether the next line continues this expression. */
    private endsContinuation;
    /** @param text Trimmed statement text. @returns Text with canonical keyword spacing. */
    private normalizeStatementSpacing;
    /** @param code String/comment-free code. @returns Code with canonical keyword spacing. */
    private normalizeKeywordSpacing;
    /** @param lines Source lines. @returns Lines with a bare `out` joined to its value list. */
    private joinBareOutput;
    /**
     * Collapses comma-separated output declarations split across physical lines.
     * @param lines Source lines after bare-output normalization.
     * @returns Lines with simple output-name lists written on one line.
     */
    private normalizeOutputLists;
    /**
     * Collapses parenthesized expressions whose opening line already contains content.
     * A line ending at the unmatched opening parenthesis retains hanging layout intent.
     * @param lines Source lines after output normalization.
     * @returns Lines with inline parenthesized expressions joined.
     */
    private normalizeInlineParentheses;
    /**
     * Expands arithmetic groups that deliberately place content after an opening-parenthesis newline.
     * @param lines Source lines after output normalization.
     * @returns Lines with top-level additive operands split vertically.
     */
    private normalizeVerticalArithmetic;
    /**
     * Splits an expression at its lowest-precedence top-level binary operators.
     * @param expression Parenthesized arithmetic expression without its outer delimiters.
     * @returns Operands with each separator retained on the preceding operand.
     */
    private splitTopLevelTerms;
    /**
     * Splits at one operator-precedence group while ignoring nested delimiters.
     * @param expression Expression to split.
     * @param operators Operators belonging to one precedence group.
     * @returns Terms with separators retained on the preceding term.
     */
    private splitAtTopLevelOperators;
    /**
     * Collapses continued assignments when their right-hand side starts inline.
     * An assignment ending at `=` keeps its explicit vertical layout.
     * @param lines Source lines after parenthesis normalization.
     * @returns Lines with inline assignment continuations joined.
     */
    private normalizeAssignmentLayout;
    /**
     * Uses the opening brace layout as the record's formatting intent.
     * @param lines Unformatted document lines.
     * @returns Lines with multiline records expanded and inline records collapsed.
     */
    private normalizeRecordLayout;
    /** @param line Candidate opening line. @param open Brace column. @returns Whether it opens a record expression. */
    private isRecordOpening;
    /** @param content Record body. @returns Fields split only at top-level commas. */
    private splitRecordFields;
    /**
     * Formats multiline calls using the opening parenthesis as intent.
     * @param lines Already indented document lines.
     * @param indent One editor indentation unit.
     * @returns Lines with consistent inline or hanging named arguments.
     */
    private normalizeNamedArgumentLayout;
    /** @param content Call body. @returns Arguments split at top-level commas. */
    private splitCallArguments;
}
