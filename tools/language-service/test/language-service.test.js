const assert = require("node:assert/strict")
const path = require("node:path")
const test = require("node:test")
const { LanguageService } = require("../dist")

const workspace = {
  async readFile() { return "" },
  importDir() { return "" },
  async resolveImport() { return null }
}

test("generic editor consumer only depends on the neutral service contract", async () => {
  const calls = []
  const analyzer = {
    async analyzeDocument() { return { ok: true, diagnostics: [], outline: [], folds: [], tokens: [] } },
    async resolveAt() { return { name: "max", kind: "function", builtin: true, type: "number", signature: "max(left, right)", documentation: "Returns the greater value." } },
    async completeAt() { return [{ name: "max", kind: "function", builtin: true, signature: "max(left, right)" }] },
    async queryFunctions() { return [] }
  }
  const service = new LanguageService(analyzer, workspace)
  const editor = {
    async update(cxprService) {
      calls.push(await cxprService.getCompletions("value = ma", { line: 0, column: 10 }, "memory.cxpr"))
      calls.push(await cxprService.getHover("value = max(1, 2)", { line: 0, column: 9 }, "memory.cxpr"))
    }
  }
  await editor.update(service)
  assert.equal(calls[0][0].label, "max")
  assert.match(calls[1].markdown, /max\(left, right\)/)
  assert.match(calls[1].markdown, /Returns the greater value\./)
  assert.equal(JSON.stringify(calls).includes("monaco"), false)
})

test("service accepts an async analyzer", async () => {
  let resumed = false
  const analyzer = {
    async analyzeDocument() { await Promise.resolve(); resumed = true; return { ok: true, diagnostics: [] } },
    async resolveAt() { return null }, async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const service = new LanguageService(analyzer, workspace)
  assert.deepEqual(await service.getDiagnostics("model demo", path.join("tmp", "demo.cxpr")), [])
  assert.equal(resumed, true)
})

test("diagnoses the from namespace when grouped imports cannot be resolved", async () => {
  const analyzer = {
    async analyzeDocument() { return { ok: true, diagnostics: [], functions: [] } },
    async resolveAt() { return null }, async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const importWorkspace = {
    async readFile() { return "" }, importDir() { return "/workspace" },
    async resolveImport(spec) {
      return spec === "stereo.effects.gain" ? { path: "/workspace/gain.cxpr" } : null
    }
  }
  const diagnostics = await new LanguageService(analyzer, importWorkspace).getDiagnostics(
    "use chamber, gain from stereo.effects\n", "/workspace/song.cxpr")
  assert.deepEqual(diagnostics, [{
    severity: 1,
    source: "cxpr",
    message: "Namespace 'stereo.effects' does not contain: chamber",
    range: {
      start: { line: 0, character: 23 },
      end: { line: 0, character: 37 }
    }
  }])
})

test("diagnoses a structured model result used directly as a scalar", async () => {
  const analyzer = {
    async analyzeDocument() {
      return { ok: true, diagnostics: [], functions: [{ name: "adsr", builtin: false }],
        sourceTokens: [{ type: "function", line: 0, start: 8, length: 4 }] }
    },
    async resolveAt() {
      return { name: "adsr", kind: "model", builtin: false, type: "struct",
        typedSignature: "adsr(...) -> { value: number }", outputs: [{ name: "value", type: "number" }] }
    },
    async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const service = new LanguageService(analyzer, workspace)
  const diagnostics = await service.getDiagnostics(
    "shape = adsr()\nout sample = 0.5 * shape\n", "stacked_bass.cxpr")
  assert.equal(diagnostics.length, 1)
  assert.match(diagnostics[0].message, /shape\.value/)
  assert.deepEqual(diagnostics[0].range, {
    start: { line: 1, character: 19 }, end: { line: 1, character: 24 }
  })
})

test("structured output syntax overrides stale imported scalar metadata", async () => {
  const imported = "model adsr\nvalue = 1\nout { value }\n"
  const analyzer = {
    async analyzeDocument(source) {
      return source === imported
        ? { ok: true, outline: [
          { kind: "model", name: "adsr", startLine: 0, startChar: 0, endLine: 0, endChar: 10 },
          { kind: "output", name: "value", startLine: 2, startChar: 6, endLine: 2, endChar: 11 }
        ] }
        : { ok: true, diagnostics: [], functions: [] }
    },
    async resolveAt(source) {
      if (source === imported || source.includes("__cxpr_probe")) return {
        name: "adsr", kind: "function", type: "number",
        outputs: [{ name: "value", type: "number" }],
        definition: { startLine: 0, startChar: 6, endLine: 0, endChar: 10 }
      }
      return { name: "adsr", kind: "unresolved", imports: ["envelopes.adsr"] }
    },
    async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const importWorkspace = {
    async readFile() { return imported }, importDir() { return "/workspace" },
    async resolveImport() { return { path: "/workspace/envelopes/adsr.cxpr", source: imported } }
  }
  const service = new LanguageService(analyzer, importWorkspace)
  const diagnostics = await service.getDiagnostics(
    "shape = adsr()\nout sample = shape * 0.5\n", "/workspace/stacked_bass.cxpr")
  assert.equal(diagnostics.length, 1)
  assert.match(diagnostics[0].message, /shape\.value/)
})

test("sum hover preserves the record shape of collection elements", async () => {
  const gainSource = "model gain\nout { left, right }\n"
  const consumer = "mix = sum([source |> gain(level=0.5)])\n"
  const analyzer = {
    async analyzeDocument(source) {
      return source === gainSource ? { ok: true, outline: [
        { kind: "model", name: "gain", startLine: 0, startChar: 0, endLine: 0, endChar: 10 },
        { kind: "output", name: "left", startLine: 1, startChar: 6, endLine: 1, endChar: 10 },
        { kind: "output", name: "right", startLine: 1, startChar: 12, endLine: 1, endChar: 17 }
      ] } : { ok: true }
    },
    async resolveAt(source, _name, pos) {
      if (source === consumer && pos.column <= 10) return {
        name: "sum", kind: "function", builtin: true, type: "number",
        signature: "sum(value, ...values)", documentation: "Sums values."
      }
      if (source === consumer) return { name: "gain", kind: "unresolved", imports: ["stereo.effects.gain"] }
      return { name: "gain", kind: "function", type: "number",
        outputs: [{ name: "left", type: "number" }],
        definition: { startLine: 0, startChar: 6, endLine: 0, endChar: 10 } }
    },
    async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const sumWorkspace = {
    async readFile() { return gainSource }, importDir() { return "/workspace" },
    async resolveImport() { return { path: "/workspace/stereo/effects/gain.cxpr", source: gainSource } }
  }
  const hover = await new LanguageService(analyzer, sumWorkspace)
    .getHover(consumer, { line: 0, column: 7 }, "/workspace/room.cxpr")
  assert.match(hover.markdown, /sum\(value, \.\.\.values\) -> \{ left: number, right: number \}/)
  assert.match(hover.markdown, /Returns `struct`/)
})

test("host requirement diagnostics underline the complete requirement function", async () => {
  const analyzer = {
    async analyzeDocument() {
      return { ok: true, diagnostics: [{ severity: 1,
        message: "Dyn cannot bind resample source 'close' at interval '5m'", line: 4, column: 12 }] }
    },
    async resolveAt() { return null }, async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const service = new LanguageService(analyzer, workspace)
  const diagnostics = await service.getDiagnostics("", "strategy.cxpr")
  assert.deepEqual(diagnostics[0].range, {
    start: { line: 4, character: 12 }, end: { line: 4, character: 20 }
  })
})

test("definition searches exported symbols inside imported modules", async () => {
  const analyzer = {
    async analyzeDocument() { return { ok: true } },
    async resolveAt(source) {
      return source.includes("fn ts_window_span")
        ? { name: "ts_window_span", kind: "function", builtin: false,
          signature: "ts_window_span(left, right)",
          definition: { startLine: 5, startChar: 3, endLine: 5, endChar: 17 } }
        : { name: "ts_window_span", kind: "unresolved", builtin: false,
          imports: ["ts/pivots", "ts/window"] }
    },
    async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const requested = []
  const importWorkspace = {
    async readFile() { return "fn ts_window_span(left, right) = left + right" },
    importDir() { return "/workspace" },
    async resolveImport(spec) {
      requested.push(spec)
      return spec === "ts/window" ? { path: "/workspace/ts/window.cxpr" } : null
    }
  }
  const service = new LanguageService(analyzer, importWorkspace)
  assert.deepEqual(await service.getDefinitions("", { line: 20, column: 12 },
    "/workspace/trendline.cxpr"), [{
    uri: "/workspace/ts/window.cxpr",
    range: { start: { line: 5, character: 3 }, end: { line: 5, character: 17 } },
    source: "fn ts_window_span(left, right) = left + right"
  }])
  assert.deepEqual(requested, ["ts/pivots", "ts/window"])
})

test("import hover resolves the declaration so compiled typed signatures survive", async () => {
  const importedSource = "model resonance\nin mono\nout low, band, high\n"
  const analyzer = {
    async analyzeDocument(source) {
      return source === importedSource
        ? { ok: true, outline: [{ kind: "model", name: "resonance",
          startLine: 0, startChar: 0, endLine: 0, endChar: 15 }] }
        : { ok: true, outline: [] }
    },
    async resolveAt(source) {
      if (source === importedSource) {
        return { name: "resonance", kind: "function", builtin: false,
          signature: "resonance(mono)",
          typedSignature: "resonance(mono: number) -> { low: number, band: number, high: number }",
          definition: { startLine: 0, startChar: 6, endLine: 0, endChar: 15 } }
      }
      return { name: "resonance", kind: "unresolved", builtin: false,
        imports: ["mono.filters.resonance"] }
    },
    async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const importWorkspace = {
    async readFile() { return importedSource },
    importDir() { return "/workspace" },
    async resolveImport() { return { path: "/workspace/resonance.cxpr" } }
  }

  const hover = await new LanguageService(analyzer, importWorkspace).getHover(
    "use resonance from mono.filters\ntone = resonance(mono=sample)",
    { line: 1, column: 9 }, "/workspace/piano_section.cxpr")

  assert.match(hover.markdown,
    /resonance\(mono: number\) -> \{ low: number, band: number, high: number \}/)
  assert.doesNotMatch(hover.markdown, /`resonance\(mono\)`/)
})

test("each dotted import namespace token shows its folder", async () => {
  const analyzer = {
    async analyzeDocument() { return { ok: true } },
    async resolveAt() { return null },
    async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const service = new LanguageService(analyzer, workspace)
  const source = "use chamber, gain from stereo.effects"

  const stereo = await service.getHover(source, { line: 0, column: 25 }, "room.cxpr")
  const effects = await service.getHover(source, { line: 0, column: 32 }, "room.cxpr")

  assert.match(stereo.markdown, /Folder: `stereo\/`/)
  assert.deepEqual(stereo.range, {
    start: { line: 0, character: 23 }, end: { line: 0, character: 29 }
  })
  assert.match(effects.markdown, /Folder: `stereo\/effects\/`/)
  assert.deepEqual(effects.range, {
    start: { line: 0, character: 30 }, end: { line: 0, character: 37 }
  })
})

test("definition on a grouped import namespace resolves its exported models", async () => {
  const sources = {
    "/workspace/modules/instruments/native/lead.cxpr": "model lead\nout value = 1\n",
    "/workspace/modules/instruments/native/bass.cxpr": "model bass\nout value = 1\n"
  }
  const analyzer = {
    async analyzeDocument(source) {
      const model = source.match(/^model\s+(\w+)/)?.[1]
      return { ok: true, outline: model ? [{ kind: "model", name: model,
        startLine: 0, startChar: 0, endLine: 0, endChar: 10 }] : [] }
    },
    async resolveAt(source) {
      const model = source.match(/^model\s+(\w+)/)?.[1]
      return model ? { name: model, kind: "model", definition: {
        startLine: 0, startChar: 6, endLine: 0, endChar: 6 + model.length
      } } : null
    },
    async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const importWorkspace = {
    async readFile(file) { return sources[file] }, importDir() { return "/workspace/modules" },
    async resolveImport(spec) {
      const file = `/workspace/modules/${spec.replaceAll(".", "/")}.cxpr`
      return sources[file] ? { path: file, source: sources[file] } : null
    }
  }
  const definitions = await new LanguageService(analyzer, importWorkspace).getDefinitions(
    "use lead, bass from instruments.native", { line: 0, column: 35 }, "/workspace/song.cxpr")

  assert.deepEqual(definitions.map((item) => [item.uri, item.range]), [
    ["/workspace/modules/instruments/native/lead.cxpr", {
      start: { line: 0, character: 6 }, end: { line: 0, character: 10 }
    }],
    ["/workspace/modules/instruments/native/bass.cxpr", {
      start: { line: 0, character: 6 }, end: { line: 0, character: 10 }
    }]
  ])
})

test("model result field diagnostics reject fields on scalars and unknown record fields", async () => {
  const analyzer = {
    async analyzeDocument(source) {
      if (source.startsWith("model adsr")) return { ok: true, outline: [{
        kind: "model", name: "adsr", startLine: 0, startChar: 0, endLine: 0, endChar: 10
      }] }
      if (source.startsWith("model filter")) return { ok: true, outline: [{
        kind: "model", name: "filter", startLine: 0, startChar: 0, endLine: 0, endChar: 12
      }] }
      return { ok: true, diagnostics: [], functions: [] }
    },
    async resolveAt(source, _name, pos) {
      if (source.startsWith("model adsr")) return {
        name: "adsr", kind: "function", type: "number", outputs: [{ name: "value", type: "number" }],
        definition: { startLine: 0, startChar: 6, endLine: 0, endChar: 10 }
      }
      if (source.startsWith("model filter")) return {
        name: "filter", kind: "function", type: "struct",
        outputs: [{ name: "low", type: "number" }, { name: "high", type: "number" }],
        definition: { startLine: 0, startChar: 6, endLine: 0, endChar: 12 }
      }
      const token = source.split("\n")[pos.line]?.slice(pos.column)
      return { name: token?.startsWith("adsr") ? "adsr" : "filter", kind: "unresolved",
        imports: [token?.startsWith("adsr") ? "envelopes.adsr" : "filters.filter"] }
    },
    async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const sources = {
    "envelopes.adsr": "model adsr\nout value\n",
    "filters.filter": "model filter\nout low, high\n"
  }
  const importWorkspace = {
    async readFile(path) { return sources[path] }, importDir() { return "/workspace" },
    async resolveImport(spec) { return { path: spec, source: sources[spec] } }
  }
  const source = [
    "shape = adsr(gate=1)",
    "tone = filter(source=1)",
    "sample = shape.valuex + tone.middle + tone.low"
  ].join("\n")
  const diagnostics = await new LanguageService(analyzer, importWorkspace)
    .getDiagnostics(source, "voice.cxpr")

  assert.deepEqual(diagnostics.map((item) => [item.message, item.range]), [
    ["Cannot access field 'valuex' on scalar result 'shape'", {
      start: { line: 2, character: 15 }, end: { line: 2, character: 21 }
    }],
    ["Unknown field 'middle' on result 'tone'", {
      start: { line: 2, character: 29 }, end: { line: 2, character: 35 }
    }]
  ])
})

test("model result field diagnostics accept nested record roots", async () => {
  const analyzer = {
    async analyzeDocument(source) {
      if (source.startsWith("model sequencer")) return { ok: true, outline: [
        { kind: "model", name: "sequencer", startLine: 0, startChar: 0, endLine: 0, endChar: 15 },
        { kind: "output", name: "drums", startLine: 1, startChar: 0, endLine: 1, endChar: 40 },
        { kind: "output", name: "bass", startLine: 1, startChar: 42, endLine: 1, endChar: 79 }
      ] }
      return { ok: true, diagnostics: [], functions: [] }
    },
    async resolveAt(source) {
      if (source.startsWith("model sequencer")) return {
        name: "sequencer", kind: "function", type: "unknown",
        definition: { startLine: 0, startChar: 6, endLine: 0, endChar: 15 }
      }
      return { name: "sequencer", kind: "unresolved", imports: ["sequencer"] }
    },
    async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const sequencer = "model sequencer\nout { drums = { kick_trigger = true }, bass = { age = 0, frequency = 55 } }\n"
  const importWorkspace = {
    async readFile() { return sequencer }, importDir() { return "/workspace" },
    async resolveImport() { return { path: "/workspace/sequencer.cxpr", source: sequencer } }
  }
  const diagnostics = await new LanguageService(analyzer, importWorkspace).getDiagnostics(
    "seq = sequencer(t=0)\nsample = seq.bass.age + seq.drums.kick_trigger\n",
    "/workspace/song.cxpr")

  assert.deepEqual(diagnostics, [])
})

test("model result hover resolves a nested output leaf", async () => {
  const sequencer = [
    "model sequencer",
    "out { drums = { kick_level = 1, snare_level = 0.8 }, duration = 1 }"
  ].join("\n")
  const analyzer = {
    async analyzeDocument(source) {
      return source === sequencer ? { ok: true, outline: [
        { kind: "model", name: "sequencer", startLine: 0, startChar: 0, endLine: 0, endChar: 15 },
        { kind: "output", name: "drums", startLine: 1, startChar: 6, endLine: 1, endChar: 54 },
        { kind: "output", name: "duration", startLine: 1, startChar: 56, endLine: 1, endChar: 68 }
      ] } : { ok: true, diagnostics: [], functions: [] }
    },
    async resolveAt(source) {
      return source === sequencer
        ? { name: "sequencer", kind: "function", type: "unknown",
          definition: { startLine: 0, startChar: 6, endLine: 0, endChar: 15 } }
        : { name: "sequencer", kind: "unresolved", imports: ["sequencer"] }
    },
    async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const importWorkspace = {
    async readFile() { return sequencer }, importDir() { return "/workspace" },
    async resolveImport() { return { path: "/workspace/sequencer.cxpr", source: sequencer } }
  }
  const source = "model song\n\nseq = sequencer(t=0)\nsample = seq.drums.snare_level\n"
  const hover = await new LanguageService(analyzer, importWorkspace)
    .getHover(source, { line: 3, column: 25 }, "/workspace/song.cxpr")

  assert.match(hover.markdown, /\*\*snare_level\*\*/)
  assert.match(hover.markdown, /Output of `sequencer`/)
  assert.deepEqual(hover.range, {
    start: { line: 3, character: 19 }, end: { line: 3, character: 30 }
  })

  const groupHover = await new LanguageService(analyzer, importWorkspace)
    .getHover(source, { line: 3, column: 15 }, "/workspace/song.cxpr")
  assert.match(groupHover.markdown, /\*\*drums\*\*/)
  assert.match(groupHover.markdown, /Returns `struct`/)
  assert.match(groupHover.markdown, /`snare_level: number`/)
  assert.deepEqual(groupHover.range, {
    start: { line: 3, character: 13 }, end: { line: 3, character: 18 }
  })
})

test("model result field diagnostics do not match a nested field as a root binding", async () => {
  const voice = "model voice\nout sample = 0\n"
  const analyzer = {
    async analyzeDocument(source) {
      return source === voice ? { ok: true, outline: [
        { kind: "model", name: "voice", startLine: 0, startChar: 0, endLine: 0, endChar: 11 },
        { kind: "output", name: "sample", startLine: 1, startChar: 4, endLine: 1, endChar: 10 }
      ] } : { ok: true, diagnostics: [], functions: [] }
    },
    async resolveAt(source) {
      return source === voice
        ? { name: "voice", kind: "function", type: "number",
          outputs: [{ name: "sample", type: "number" }] }
        : { name: "voice", kind: "unresolved", imports: ["voice"] }
    },
    async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const importWorkspace = {
    async readFile() { return voice }, importDir() { return "/workspace" },
    async resolveImport() { return { path: "/workspace/voice.cxpr", source: voice } }
  }
  const diagnostics = await new LanguageService(analyzer, importWorkspace).getDiagnostics([
    "stab = voice()",
    "sample = seq.stab.frequency + stab"
  ].join("\n"), "song.cxpr")

  assert.deepEqual(diagnostics, [])
})
