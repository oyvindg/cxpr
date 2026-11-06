/** Formatting options understood by the CXPR formatter. */
export interface CxprFormattingOptions {
  /** Number of columns in one indentation level. */
  tabSize?: number
  /** Use spaces when true and tabs when false. */
  insertSpaces?: boolean
}

/** Contract for deterministic, editor-neutral CXPR source formatting. */
export interface ICxprFormatter {
  /**
   * Formats a complete CXPR document while preserving its newline convention.
   * @param source Complete CXPR source text.
   * @param options Optional indentation settings supplied by the editor.
   * @returns Formatted CXPR source text.
   */
  format(source: string, options?: CxprFormattingOptions): string
}

/** Applies the indentation conventions used by the legacy CXPR VSIX. */
export class CxprFormatter implements ICxprFormatter {
  /** @inheritdoc */
  format(source: string, options: CxprFormattingOptions = {}): string {
    const newline = source.includes("\r\n") ? "\r\n" : "\n"
    const finalNewline = /\r?\n$/.test(source)
    const lines = this.normalizeRecordLayout(this.normalizeAssignmentLayout(
      this.normalizeInlineParentheses(this.normalizeVerticalCallLists(this.normalizeVerticalArithmetic(
        this.normalizeOutputLists(this.joinBareOutput(
          source.replace(/\r\n/g, "\n").split("\n"))))))))
    if (finalNewline) lines.pop()
    const size = Number.isInteger(options.tabSize) && (options.tabSize ?? 0) > 0
      ? options.tabSize as number : 4
    const indent = options.insertSpaces === false ? "\t" : " ".repeat(size)
    let blockDepth = 0
    const groupingStack: Array<{ delimiter: string; visual: boolean }> = []
    let previousContinues = false
    let inBlockComment = false

    const output = lines.map((rawLine) => {
      const text = this.normalizeStatementSpacing(rawLine.trim())
      if (!text) {
        previousContinues = false
        return ""
      }
      const shaped = this.codeShape(text, inBlockComment)
      inBlockComment = shaped.inBlockComment
      const shape = shaped.code.trim()
      const blockCloses = shape.match(/^\}+/)?.[0].length ?? 0
      const lineBlockDepth = Math.max(0, blockDepth - blockCloses)
      const lineGroupingDepth = this.groupingDepthAfterLeadingClosers(groupingStack, shape)
      const continuationDepth = lineGroupingDepth === 0 &&
        (previousContinues || this.startsContinuation(shape)) ? 1 : 0
      const formatted = `${indent.repeat(lineBlockDepth + lineGroupingDepth + continuationDepth)}${text}`
      blockDepth = Math.max(0, blockDepth + this.count("{", shape) - this.count("}", shape))
      this.updateGroupingStack(groupingStack, shape)
      previousContinues = this.endsContinuation(shape)
      return formatted
    })
    return this.collapseBlankLines(this.normalizeNamedArgumentLayout(output, indent)).join(newline) +
      (finalNewline ? newline : "")
  }

  /**
   * Limits vertical whitespace to one empty physical line.
   * @param lines Formatted source lines.
   * @returns Lines with repeated empty lines collapsed.
   */
  private collapseBlankLines(lines: string[]): string[] {
    const result: string[] = []
    for (const line of lines) {
      if (line === "" && result.at(-1) === "") continue
      result.push(line)
    }
    return result
  }

  /**
   * Replaces strings and comments with inert characters before delimiter analysis.
   * @param line Trimmed source line.
   * @param startsInBlockComment Whether a block comment started on an earlier line.
   * @returns Delimiter-safe code and block-comment state for the next line.
   */
  private codeShape(line: string, startsInBlockComment: boolean): { code: string; inBlockComment: boolean } {
    let code = ""
    let quote: string | null = null
    let escaped = false
    let inBlockComment = startsInBlockComment
    for (let index = 0; index < line.length; index += 1) {
      const character = line[index]
      const next = line[index + 1]
      if (inBlockComment) {
        if (character === "*" && next === "/") {
          inBlockComment = false
          code += "  "
          index += 1
        } else code += " "
        continue
      }
      if (quote) {
        if (escaped) escaped = false
        else if (character === "\\") escaped = true
        else if (character === quote) quote = null
        code += " "
        continue
      }
      if (character === '"' || character === "'") {
        quote = character
        code += "x"
        continue
      }
      if (character === "/" && next === "*") {
        inBlockComment = true
        code += "  "
        index += 1
        continue
      }
      if (character === "#" || (character === "/" && next === "/")) break
      code += character
    }
    return { code, inBlockComment }
  }

  /** @param character Delimiter to count. @param text Safe source text. @returns Occurrence count. */
  private count(character: string, text: string): number {
    return [...text].filter((candidate) => candidate === character).length
  }

  /**
   * Calculates indentation after applying closing delimiters at the start of a line.
   * @param stack Active grouping delimiters from preceding lines.
   * @param text Delimiter-safe source line.
   * @returns Visual grouping depth for the current line.
   */
  private groupingDepthAfterLeadingClosers(
    stack: Array<{ delimiter: string; visual: boolean }>, text: string): number {
    const remaining = [...stack]
    for (const character of text.match(/^[\])]+/)?.[0] ?? "") remaining.pop()
    return remaining.filter((entry) => entry.visual).length
  }

  /**
   * Tracks grouping delimiters across lines without double-indenting direct call-wrapped lists.
   * @param stack Mutable active grouping stack.
   * @param text Delimiter-safe source line.
   */
  private updateGroupingStack(
    stack: Array<{ delimiter: string; visual: boolean }>, text: string): void {
    for (let index = 0; index < text.length; index += 1) {
      const character = text[index]
      if (character === "(") stack.push({ delimiter: character, visual: true })
      else if (character === "[") {
        const previous = text.slice(0, index).trimEnd().at(-1)
        stack.push({ delimiter: character, visual: previous !== "(" })
      } else if (character === ")" || character === "]") stack.pop()
    }
  }

  /** @param text Safe source line. @returns Whether the line starts a continuation. */
  private startsContinuation(text: string): boolean {
    return /^(?:and\b|or\b|\|>|[+*/%?:]|-(?!>))/.test(text)
  }

  /** @param text Safe source line. @returns Whether the next line continues this expression. */
  private endsContinuation(text: string): boolean {
    return /(?:=|\b(?:and|or)|[+\-*/%?:]|&&|\|\||\|>)\s*$/.test(text) &&
      !/(?:==|!=|<=|>=)\s*$/.test(text)
  }

  /** @param text Trimmed statement text. @returns Text with canonical keyword spacing. */
  private normalizeStatementSpacing(text: string): string {
    let result = ""
    let segment = ""
    let quote: string | null = null
    for (let index = 0; index < text.length; index += 1) {
      const character = text[index]
      const next = text[index + 1]
      if (!quote && (character === "#" || (character === "/" && next === "/"))) {
        result += this.normalizeKeywordSpacing(segment) + text.slice(index)
        return result
      }
      if (character === '"' || character === "'") {
        if (!quote) {
          result += this.normalizeKeywordSpacing(segment)
          segment = character
          quote = character
          continue
        }
        if (quote === character && text[index - 1] !== "\\") {
          result += segment + character
          segment = ""
          quote = null
          continue
        }
      }
      segment += character
    }
    return result + (quote ? segment : this.normalizeKeywordSpacing(segment))
  }

  /** @param code String/comment-free code. @returns Code with canonical keyword spacing. */
  private normalizeKeywordSpacing(code: string): string {
    return code
      .replace(/^(model|use|in|out|state|param|fn|assert|return)\s+/, "$1 ")
      .replace(/^(update|initial)\s+state\s+/, "$1 state ")
      .replace(/\s+\b(and|or|in|from|as)\b\s+/g, " $1 ")
      .replace(/\bnot\b\s+/g, "not ")
  }

  /** @param lines Source lines. @returns Lines with a bare `out` joined to its value list. */
  private joinBareOutput(lines: string[]): string[] {
    const result: string[] = []
    for (let index = 0; index < lines.length; index += 1) {
      if (lines[index].trim() !== "out") {
        result.push(lines[index])
        continue
      }
      let valueLine = index + 1
      while (valueLine < lines.length && lines[valueLine].trim() === "") valueLine += 1
      if (valueLine >= lines.length) {
        result.push(lines[index])
        continue
      }
      result.push(`${lines[index].trimEnd()} ${lines[valueLine].trim()}`)
      index = valueLine
    }
    return result
  }

  /**
   * Collapses comma-separated output declarations split across physical lines.
   * @param lines Source lines after bare-output normalization.
   * @returns Lines with simple output-name lists written on one line.
   */
  private normalizeOutputLists(lines: string[]): string[] {
    const result: string[] = []
    for (let index = 0; index < lines.length; index += 1) {
      const first = lines[index].match(/^\s*out\s+([A-Za-z_][A-Za-z0-9_]*)\s*,\s*$/)
      if (!first) {
        result.push(lines[index])
        continue
      }
      const names = [first[1]]
      let cursor = index + 1
      let comment = ""
      let complete = false
      while (cursor < lines.length) {
        const next = lines[cursor].match(
          /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(,?)\s*(#.*)?$/)
        if (!next) break
        names.push(next[1])
        comment = next[3] || ""
        cursor += 1
        if (!next[2]) {
          complete = true
          break
        }
        if (comment) break
      }
      if (!complete) {
        result.push(lines[index])
        continue
      }
      const indentation = lines[index].match(/^\s*/)?.[0] || ""
      result.push(`${indentation}out ${names.join(", ")}${comment ? ` ${comment}` : ""}`)
      index = cursor - 1
    }
    return result
  }

  /**
   * Expands a call-wrapped list when its first value starts on the following line.
   * @param lines Source lines after output normalization.
   * @returns Lines with top-level list values written one per line.
   */
  private normalizeVerticalCallLists(lines: string[]): string[] {
    const result: string[] = []
    for (let index = 0; index < lines.length; index += 1) {
      const shaped = this.codeShape(lines[index], false).code
      const open = shaped.lastIndexOf("[")
      if (open < 0 || shaped.slice(open + 1).trim() || !/\(\s*$/.test(shaped.slice(0, open))) {
        result.push(lines[index])
        continue
      }

      let depth = 1
      let closeLine = -1
      let closeColumn = -1
      for (let cursor = index + 1; cursor < lines.length && closeLine < 0; cursor += 1) {
        const continuation = this.codeShape(lines[cursor], false).code
        for (let column = 0; column < continuation.length; column += 1) {
          if (continuation[column] === "[") depth += 1
          else if (continuation[column] === "]" && --depth === 0) {
            closeLine = cursor
            closeColumn = column
            break
          }
        }
      }
      if (closeLine < 0) {
        result.push(lines[index])
        continue
      }

      const content = lines.slice(index + 1, closeLine).concat(
        lines[closeLine].slice(0, closeColumn)).join("\n").trim()
      const values = this.splitRecordFields(content)
      const suffix = lines[closeLine].slice(closeColumn + 1).trim()
      result.push(lines[index].trimEnd())
      values.forEach((value, valueIndex) =>
        result.push(`${value}${valueIndex + 1 < values.length ? "," : ""}`))
      result.push(`]${suffix}`)
      index = closeLine
    }
    return result
  }

  /**
   * Collapses parenthesized expressions whose opening line already contains content.
   * A line ending at the unmatched opening parenthesis retains hanging layout intent.
   * @param lines Source lines after output normalization.
   * @returns Lines with inline parenthesized expressions joined.
   */
  private normalizeInlineParentheses(lines: string[]): string[] {
    const result: string[] = []
    for (let index = 0; index < lines.length; index += 1) {
      const shaped = this.codeShape(lines[index], false).code
      const openings: number[] = []
      for (let column = 0; column < shaped.length; column += 1) {
        if (shaped[column] === "(") openings.push(column)
        else if (shaped[column] === ")") openings.pop()
      }
      const unmatched = openings.at(-1)
      const openingContent = unmatched === undefined ? "" : shaped.slice(unmatched + 1).trim()
      if (unmatched === undefined || !openingContent || /^[\[{]$/.test(openingContent)) {
        result.push(lines[index])
        continue
      }
      let depth = openings.length
      let cursor = index + 1
      const parts = [lines[index].trimEnd()]
      while (cursor < lines.length && depth > 0) {
        const continuation = this.codeShape(lines[cursor], false).code
        depth += this.count("(", continuation) - this.count(")", continuation)
        parts.push(lines[cursor].trim())
        cursor += 1
      }
      if (depth > 0) {
        result.push(lines[index])
        continue
      }
      result.push(parts.join(" "))
      index = cursor - 1
    }
    return result
  }

  /**
   * Expands arithmetic groups that deliberately place content after an opening-parenthesis newline.
   * @param lines Source lines after output normalization.
   * @returns Lines with top-level additive operands split vertically.
   */
  private normalizeVerticalArithmetic(lines: string[]): string[] {
    const result: string[] = []
    for (let index = 0; index < lines.length; index += 1) {
      const opening = lines[index].match(/^(.*[+\-*/%]\s*)\(\s*$/)
      if (!opening) {
        result.push(lines[index])
        continue
      }
      let depth = 1
      let closeLine = -1
      let closeColumn = -1
      for (let cursor = index + 1; cursor < lines.length && closeLine < 0; cursor += 1) {
        const shaped = this.codeShape(lines[cursor], false).code
        for (let column = 0; column < shaped.length; column += 1) {
          if (shaped[column] === "(") depth += 1
          else if (shaped[column] === ")" && --depth === 0) {
            closeLine = cursor
            closeColumn = column
            break
          }
        }
      }
      if (closeLine < 0) {
        result.push(lines[index])
        continue
      }
      const content = lines.slice(index + 1, closeLine).concat(
        lines[closeLine].slice(0, closeColumn)).join(" ").trim()
      const terms = this.splitTopLevelTerms(content)
      if (terms.length < 2) {
        result.push(lines[index])
        continue
      }
      const suffix = lines[closeLine].slice(closeColumn + 1).trim()
      result.push(lines[index].trimEnd(), ...terms, `)${suffix ? ` ${suffix}` : ""}`)
      index = closeLine
    }
    return result
  }

  /**
   * Splits an expression at its lowest-precedence top-level binary operators.
   * @param expression Parenthesized arithmetic expression without its outer delimiters.
   * @returns Operands with each separator retained on the preceding operand.
   */
  private splitTopLevelTerms(expression: string): string[] {
    const precedenceGroups = [
      ["or", "||"], ["and", "&&"], ["==", "!=", "<=", ">=", "<", ">"],
      ["+", "-"], ["*", "/", "%"]
    ]
    for (const operators of precedenceGroups) {
      const terms = this.splitAtTopLevelOperators(expression, operators)
      if (terms.length > 1) return terms
    }
    return expression.trim() ? [expression.trim()] : []
  }

  /**
   * Splits at one operator-precedence group while ignoring nested delimiters.
   * @param expression Expression to split.
   * @param operators Operators belonging to one precedence group.
   * @returns Terms with separators retained on the preceding term.
   */
  private splitAtTopLevelOperators(expression: string, operators: string[]): string[] {
    const terms: string[] = []
    let start = 0
    let depth = 0
    let quote: string | null = null
    for (let index = 0; index < expression.length; index += 1) {
      const character = expression[index]
      if (quote) {
        if (character === quote && expression[index - 1] !== "\\") quote = null
      } else if (character === '"' || character === "'") quote = character
      else if (character === "(" || character === "[" || character === "{") depth += 1
      else if (character === ")" || character === "]" || character === "}") depth -= 1
      else if (depth === 0) {
        const operator = operators.find((candidate) => expression.startsWith(candidate, index) &&
          /\s/.test(expression[index - 1] || "") &&
          /\s/.test(expression[index + candidate.length] || ""))
        if (!operator) continue
        const leading = operator === "and" || operator === "or" || operator === "&&" || operator === "||"
        terms.push(expression.slice(start, leading ? index : index + operator.length).trim())
        start = leading ? index : index + operator.length
        index += operator.length - 1
      }
    }
    const tail = expression.slice(start).trim()
    if (tail) terms.push(tail)
    return terms
  }

  /**
   * Collapses continued assignments when their right-hand side starts inline.
   * An assignment ending at `=` keeps its explicit vertical layout.
   * @param lines Source lines after parenthesis normalization.
   * @returns Lines with inline assignment continuations joined.
   */
  private normalizeAssignmentLayout(lines: string[]): string[] {
    const result: string[] = []
    for (let index = 0; index < lines.length; index += 1) {
      const shaped = this.codeShape(lines[index], false).code.trim()
      if (/^[$A-Za-z_][$A-Za-z0-9_]*\s*=\s*$/.test(shaped) && index + 1 < lines.length) {
        const parts: string[] = []
        let cursor = index + 1
        let previous = ""
        do {
          const next = lines[cursor].trim()
          if (!next) break
          parts.push(next)
          previous = this.codeShape(lines[cursor], false).code.trim()
          cursor += 1
        } while (cursor < lines.length && (this.endsContinuation(previous) ||
          this.startsContinuation(this.codeShape(lines[cursor], false).code.trim())))
        const expression = parts.join(" ")
        const commentStart = expression.indexOf("#")
        const code = commentStart >= 0 ? expression.slice(0, commentStart).trimEnd() : expression
        const comment = commentStart >= 0 ? expression.slice(commentStart).trim() : ""
        const terms = this.splitTopLevelTerms(code)
        if (terms.length > 1) {
          if (comment) terms[terms.length - 1] += ` ${comment}`
          result.push(lines[index].trimEnd(), ...terms)
          index = cursor - 1
          continue
        }
      }
      if (/\|>\s*$/.test(shaped)) {
        result.push(lines[index])
        continue
      }
      if (!/^[$A-Za-z_][$A-Za-z0-9_]*\s*=\s*\S/.test(shaped) ||
          !this.endsContinuation(shaped)) {
        result.push(lines[index])
        continue
      }
      const parts = [lines[index].trimEnd()]
      let cursor = index + 1
      let previous = shaped
      while (cursor < lines.length && (this.endsContinuation(previous) ||
          this.startsContinuation(this.codeShape(lines[cursor], false).code.trim()))) {
        const next = lines[cursor].trim()
        if (!next) break
        parts.push(next)
        previous = this.codeShape(lines[cursor], false).code.trim()
        cursor += 1
      }
      result.push(parts.join(" "))
      index = cursor - 1
    }
    return result
  }

  /**
   * Uses the opening brace layout as the record's formatting intent.
   * @param lines Unformatted document lines.
   * @returns Lines with multiline records expanded and inline records collapsed.
   */
  private normalizeRecordLayout(lines: string[]): string[] {
    const result: string[] = []
    for (let index = 0; index < lines.length; index += 1) {
      const line = lines[index]
      const open = line.indexOf("{")
      if (open < 0 || !this.isRecordOpening(line, open)) {
        result.push(line)
        continue
      }
      let depth = 0
      let closeLine = -1
      let closeColumn = -1
      for (let cursor = index; cursor < lines.length && closeLine < 0; cursor += 1) {
        const start = cursor === index ? open : 0
        for (let column = start; column < lines[cursor].length; column += 1) {
          if (lines[cursor][column] === "{") depth += 1
          else if (lines[cursor][column] === "}" && --depth === 0) {
            closeLine = cursor
            closeColumn = column
            break
          }
        }
      }
      if (closeLine <= index) {
        result.push(line)
        continue
      }
      const prefix = line.slice(0, open + 1).trimEnd()
      const firstContent = line.slice(open + 1).trim()
      const contentParts = [line.slice(open + 1)]
      for (let cursor = index + 1; cursor < closeLine; cursor += 1)
        contentParts.push(lines[cursor])
      contentParts.push(lines[closeLine].slice(0, closeColumn))
      const content = contentParts.join("\n").trim()
      const suffix = lines[closeLine].slice(closeColumn + 1).trim()
      if (firstContent) {
        result.push(`${prefix} ${content.replace(/\s+/g, " ")} }${suffix ? ` ${suffix}` : ""}`)
      } else if (!this.hasPackedRecordFields(content)) {
        result.push(...lines.slice(index, closeLine + 1))
      } else {
        const fields = this.splitRecordFields(content)
        result.push(prefix)
        fields.forEach((field, fieldIndex) => {
          const rendered = this.renderRecordField(field)
          rendered[rendered.length - 1] += fieldIndex + 1 < fields.length ? "," : ""
          result.push(...rendered)
        })
        result.push(`}${suffix ? ` ${suffix}` : ""}`)
      }
      index = closeLine
    }
    return result
  }

  /**
   * Detects whether a vertical record has multiple top-level fields on one physical line.
   * @param content Record body with its original line breaks.
   * @returns True when field splitting is required.
   */
  private hasPackedRecordFields(content: string): boolean {
    let depth = 0
    let quote: string | null = null
    let inLineComment = false
    for (let index = 0; index < content.length; index += 1) {
      const character = content[index]
      const next = content[index + 1]
      if (character === "\n") {
        inLineComment = false
        continue
      }
      if (inLineComment) continue
      if (quote) {
        if (character === quote && content[index - 1] !== "\\") quote = null
        continue
      }
      if (character === '"' || character === "'") quote = character
      else if (character === "#" || (character === "/" && next === "/")) inLineComment = true
      else if (character === "{" || character === "(" || character === "[") depth += 1
      else if (character === "}" || character === ")" || character === "]") depth -= 1
      else if (character === "," && depth === 0) {
        const lineEnd = content.indexOf("\n", index + 1)
        const remainder = content.slice(index + 1, lineEnd < 0 ? content.length : lineEnd)
        if (remainder.trim()) return true
      }
    }
    return false
  }

  /** @param line Candidate opening line. @param open Brace column. @returns Whether it opens a record expression. */
  private isRecordOpening(line: string, open: number): boolean {
    const prefix = line.slice(0, open).trim()
    return prefix === "out" || prefix === "state" || prefix.endsWith("=")
  }

  /** @param content Record body. @returns Fields split only at top-level commas. */
  private splitRecordFields(content: string): string[] {
    const fields: string[] = []
    let start = 0
    let depth = 0
    let quote: string | null = null
    for (let index = 0; index < content.length; index += 1) {
      const character = content[index]
      if (quote) {
        if (character === quote && content[index - 1] !== "\\") quote = null
      } else if (character === '"' || character === "'") quote = character
      else if (character === "{" || character === "(" || character === "[") depth += 1
      else if (character === "}" || character === ")" || character === "]") depth -= 1
      else if (character === "," && depth === 0) {
        fields.push(content.slice(start, index).trim().replace(/\s+/g, " "))
        start = index + 1
      }
    }
    const tail = content.slice(start).trim().replace(/\s+/g, " ")
    if (tail) fields.push(tail)
    return fields
  }

  /**
   * Expands a record-valued field so nested records follow their parent's vertical layout.
   * @param field Normalized record field text.
   * @returns One or more field lines without the parent's trailing comma.
   */
  private renderRecordField(field: string): string[] {
    const open = field.indexOf("{")
    if (open < 0 || !this.isRecordOpening(field, open)) return [field]

    let depth = 0
    let close = -1
    let quote: string | null = null
    for (let index = open; index < field.length; index += 1) {
      const character = field[index]
      if (quote) {
        if (character === quote && field[index - 1] !== "\\") quote = null
      } else if (character === '"' || character === "'") quote = character
      else if (character === "{") depth += 1
      else if (character === "}" && --depth === 0) {
        close = index
        break
      }
    }
    if (close < 0 || field.slice(close + 1).trim()) return [field]

    const children = this.splitRecordFields(field.slice(open + 1, close))
    if (!children.length) return [field]
    const lines = [field.slice(0, open + 1).trimEnd()]
    children.forEach((child, childIndex) => {
      const rendered = this.renderRecordField(child)
      rendered[rendered.length - 1] += childIndex + 1 < children.length ? "," : ""
      lines.push(...rendered)
    })
    lines.push("}")
    return lines
  }

  /**
   * Formats multiline calls using the opening parenthesis as intent.
   * @param lines Already indented document lines.
   * @param indent One editor indentation unit.
   * @returns Lines with consistent inline or hanging named arguments.
   */
  private normalizeNamedArgumentLayout(lines: string[], indent: string): string[] {
    const result: string[] = []
    for (let index = 0; index < lines.length; index += 1) {
      const line = lines[index]
      const match = [...line.matchAll(/\b([A-Za-z_][A-Za-z0-9_]*)\s*\(/g)].at(-1)
      if (!match || match.index === undefined) {
        result.push(line)
        continue
      }
      const open = line.indexOf("(", match.index + match[1].length)
      let depth = 0
      let closeLine = -1
      let closeColumn = -1
      for (let cursor = index; cursor < lines.length && closeLine < 0; cursor += 1) {
        const start = cursor === index ? open : 0
        for (let column = start; column < lines[cursor].length; column += 1) {
          if (lines[cursor][column] === "(") depth += 1
          else if (lines[cursor][column] === ")" && --depth === 0) {
            closeLine = cursor
            closeColumn = column
            break
          }
        }
      }
      if (closeLine <= index) {
        result.push(line)
        continue
      }
      const openingContent = line.slice(open + 1).trim()
      const closingContent = lines[closeLine].slice(0, closeColumn).trim()
      if (openingContent === "[" && closingContent === "]") {
        result.push(...lines.slice(index, closeLine + 1))
        index = closeLine
        continue
      }
      const parts = [line.slice(open + 1)]
      for (let cursor = index + 1; cursor < closeLine; cursor += 1) parts.push(lines[cursor])
      parts.push(lines[closeLine].slice(0, closeColumn))
      const content = parts.join("\n").trim()
      const args = this.splitCallArguments(content)
      if (!args.length) {
        result.push(line)
        continue
      }
      const prefix = line.slice(0, open + 1).trimEnd()
      const suffix = lines[closeLine].slice(closeColumn + 1).trim()
      if (line.slice(open + 1).trim()) {
        result.push(`${prefix}${args.join(", ")})${suffix ? ` ${suffix}` : ""}`)
      } else {
        const functionColumn = match.index
        const argumentIndent = `${" ".repeat(functionColumn)}${indent}`
        result.push(prefix)
        args.forEach((argument, argumentIndex) => result.push(
          `${argumentIndent}${argument}${argumentIndex + 1 < args.length ? "," : ""}`))
        result.push(`${" ".repeat(functionColumn)})${suffix ? ` ${suffix}` : ""}`)
      }
      index = closeLine
    }
    return result
  }

  /** @param content Call body. @returns Arguments split at top-level commas. */
  private splitCallArguments(content: string): string[] {
    return this.splitRecordFields(content)
  }
}
