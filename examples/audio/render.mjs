#!/usr/bin/env node

import { spawnSync } from "node:child_process"
import { existsSync, readdirSync } from "node:fs"
import { dirname, join, resolve } from "node:path"
import { fileURLToPath } from "node:url"

const audioRoot = dirname(fileURLToPath(import.meta.url))
const examplesRoot = resolve(audioRoot, "..")
const renderer = join(audioRoot, "renderer", "render.mjs")

/**
 * Discovers renderable song directories from the audio examples tree.
 *
 * @returns {string[]} Sorted example names containing `models/song.cxpr`.
 */
function discoverExamples() {
  const songsRoot = join(audioRoot, "songs")
  return readdirSync(songsRoot, { withFileTypes: true })
    .filter((entry) => entry.isDirectory() &&
      existsSync(join(songsRoot, entry.name, "models", "song.cxpr")))
    .map((entry) => entry.name)
    .sort()
}

/**
 * Prints the canonical command-line interface.
 *
 * @param {string[]} availableExamples Discovered example names.
 */
function printUsage(availableExamples) {
  console.error("Usage: ./audio/render.mjs --example NAME --output FILE.wav [--frames=COUNT] [--sample-rate=44100]")
  console.error(`Examples: ${availableExamples.join(", ")}`)
}

/**
 * Reads a named CLI option in either `--name value` or `--name=value` form.
 *
 * @param {string[]} args Command-line arguments without node and script paths.
 * @param {string} name Option name without the leading dashes.
 * @returns {string | undefined} The supplied value.
 */
function optionValue(args, name) {
  const prefix = `--${name}=`
  const inline = args.find((argument) => argument.startsWith(prefix))
  if (inline) return inline.slice(prefix.length)
  const index = args.indexOf(`--${name}`)
  return index >= 0 ? args[index + 1] : undefined
}

/**
 * Removes options consumed by this CLI while preserving renderer options.
 *
 * @param {string[]} args Command-line arguments without node and script paths.
 * @returns {string[]} Arguments forwarded to the low-level renderer.
 */
function rendererOptions(args) {
  const forwarded = []
  for (let index = 0; index < args.length; index += 1) {
    const argument = args[index]
    if (argument === "--example" || argument === "--output") {
      index += 1
    } else if (!argument.startsWith("--example=") && !argument.startsWith("--output=")) {
      forwarded.push(argument)
    }
  }
  return forwarded
}

/**
 * Renders one validated example root.
 *
 * @param {string} exampleRoot Directory containing `models/song.cxpr`.
 * @param {string} output Destination WAV path.
 * @param {string[]} options Options forwarded to the low-level renderer.
 * @returns {number} Renderer exit status.
 */
function render(exampleRoot, output, options) {
  const result = spawnSync(process.execPath, [renderer, output,
    `--model=${join(exampleRoot, "models", "song.cxpr")}`,
    `--import-dir=${audioRoot}`,
    ...options], { stdio: "inherit" })
  if (result.error) throw result.error
  return result.status ?? 1
}

/**
 * Runs an audio example through the shared generated-C WAV renderer.
 *
 * The calling module identifies its own example root. Imports first resolve
 * relative to each CXPR module and then fall back to the shared examples root,
 * where the `audio` namespace lives.
 *
 * @param {string} entryUrl `import.meta.url` from the example entrypoint.
 * @param {string[]} argv Process arguments, including the output WAV path.
 * @returns {number} Renderer exit status suitable for `process.exitCode`.
 */
export function renderAudioExample(entryUrl, argv = process.argv) {
  const exampleRoot = dirname(fileURLToPath(entryUrl))
  const output = argv[2]
  if (!output) {
    console.error("Usage: node render.mjs OUTPUT.wav [--frames=COUNT] [--sample-rate=44100] [--session]")
    return 2
  }

  return render(exampleRoot, output, argv.slice(3))
}

/** Runs the shared `--example` command-line interface. */
function main() {
  const args = process.argv.slice(2)
  const example = optionValue(args, "example")
  const output = optionValue(args, "output")
  const availableExamples = discoverExamples()
  if (!example || !output || !availableExamples.includes(example)) {
    printUsage(availableExamples)
    return 2
  }

  const exampleRoot = join(audioRoot, "songs", example)
  if (!existsSync(join(exampleRoot, "models", "song.cxpr"))) {
    console.error(`Audio example has no models/song.cxpr: ${example}`)
    return 2
  }
  return render(exampleRoot, output, rendererOptions(args))
}

if (resolve(process.argv[1] ?? "") === fileURLToPath(import.meta.url)) {
  process.exitCode = main()
}

