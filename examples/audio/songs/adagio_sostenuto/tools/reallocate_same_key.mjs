#!/usr/bin/env node

import { readFileSync, writeFileSync } from "node:fs"
import { dirname, resolve } from "node:path"
import { fileURLToPath } from "node:url"

const here = dirname(fileURLToPath(import.meta.url))
const sequencerPath = resolve(here, "..", "models", "arrangement", "sequencer.cxpr")
const source = readFileSync(sequencerPath, "utf8")
const voicePattern = /v(\d+) = \{\s*at = \[([^\]]+)\],\s*note = \[([^\]]+)\]\s*\}/g
const events = []

for (const match of source.matchAll(voicePattern)) {
  const originalVoice = Number(match[1])
  const times = match[2].split(",").map(Number)
  const notes = match[3].split(",").map(Number)
  if (times.length !== notes.length) {
    throw new Error(`v${originalVoice} has mismatched at/note lengths`)
  }
  times.forEach((time, index) => events.push({ time, note: notes[index], originalVoice, index }))
}

if (events.length === 0) throw new Error("No sequencer voice events found")
events.sort((left, right) => left.time - right.time || left.note - right.note ||
  left.originalVoice - right.originalVoice || left.index - right.index)

const voices = Array.from({ length: 12 }, (_, index) => ({
  index,
  note: undefined,
  lastTime: Number.NEGATIVE_INFINITY,
  events: [],
}))

for (const simultaneous of Map.groupBy(events, (event) => event.time).values()) {
  if (simultaneous.length > voices.length) {
    throw new Error(`${simultaneous.length} simultaneous events exceed ${voices.length} voices`)
  }
  const used = new Set()
  const assignments = new Map()
  for (const event of simultaneous) {
    const voice = voices.find((candidate) =>
      !used.has(candidate.index) && candidate.note === event.note)
    if (voice) {
      assignments.set(event, voice)
      used.add(voice.index)
    }
  }
  for (const event of simultaneous) {
    let voice = assignments.get(event)
    if (!voice) {
      voice = voices
        .filter((candidate) => !used.has(candidate.index))
        .sort((left, right) => left.lastTime - right.lastTime || left.index - right.index)[0]
      used.add(voice.index)
    }
    voice.events.push(event)
    voice.note = event.note
    voice.lastTime = event.time
  }
}

let rewritten = source
for (const voice of voices) {
  const times = voice.events.map((event) => event.time.toFixed(9)).join(", ")
  const notes = voice.events.map((event) => event.note).join(", ")
  const replacement = `v${voice.index} = {\n    at = [${times}],\n    note = [${notes}]\n}`
  const pattern = new RegExp(`v${voice.index} = \\{\\s*at = \\[([^\\]]+)\\],\\s*note = \\[([^\\]]+)\\]\\s*\\}`)
  if (!pattern.test(rewritten)) throw new Error(`Could not replace v${voice.index}`)
  rewritten = rewritten.replace(pattern, replacement)
}

writeFileSync(sequencerPath, rewritten)
console.log(`Reallocated ${events.length} events across ${voices.length} same-key-affine voices`)
