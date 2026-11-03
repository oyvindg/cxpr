import { closeSync, openSync, readFileSync, unlinkSync, writeSync } from "node:fs"
import { dirname, join, resolve } from "node:path"
import { fileURLToPath } from "node:url"

import { compileModel } from "../../../../cxpr-node/dist/index.js"

const SAMPLE_RATE = 44_100
const CHANNELS = 2
const BYTES_PER_SAMPLE = 2
const BLOCK_FRAMES = 4_096
const here = dirname(fileURLToPath(import.meta.url))
const modelOption = process.argv.find((argument) => argument.startsWith("--model="))
const importOption = process.argv.find((argument) => argument.startsWith("--import-dir="))
const modelPath = modelOption ? resolve(modelOption.slice("--model=".length)) : undefined

function wavHeader(frames) {
  const dataBytes = frames * CHANNELS * BYTES_PER_SAMPLE
  const header = Buffer.alloc(44)
  header.write("RIFF", 0)
  header.writeUInt32LE(36 + dataBytes, 4)
  header.write("WAVEfmt ", 8)
  header.writeUInt32LE(16, 16)
  header.writeUInt16LE(1, 20)
  header.writeUInt16LE(CHANNELS, 22)
  header.writeUInt32LE(SAMPLE_RATE, 24)
  header.writeUInt32LE(SAMPLE_RATE * CHANNELS * BYTES_PER_SAMPLE, 28)
  header.writeUInt16LE(CHANNELS * BYTES_PER_SAMPLE, 32)
  header.writeUInt16LE(16, 34)
  header.write("data", 36)
  header.writeUInt32LE(dataBytes, 40)
  return header
}

function pcm16(sample, frame, channel) {
  if (!Number.isFinite(sample) || Math.abs(sample) > 1) {
    throw new Error(`Invalid sample at frame ${frame}, channel ${channel}`)
  }
  return Math.round(sample * 32_767)
}

function render(outputPath, frameLimit, importDir) {
  const source = readFileSync(modelPath, "utf8")
  using model = compileModel(source, { name: modelPath, importDir })
  using session = model.newSession()
  const initial = session.tickViews({ t: new Float64Array([0]) })
  const outputDuration = initial.duration[0]
  const duration = Number(outputDuration)
  if (!Number.isFinite(duration) || duration <= 0) {
    throw new Error("The song model returned an invalid duration")
  }

  const songFrames = Math.round(duration * SAMPLE_RATE)
  const frames = frameLimit === undefined ? songFrames : Math.min(songFrames, frameLimit)
  const renderStarted = performance.now()
  const file = openSync(outputPath, "w")
  const block = Buffer.alloc(BLOCK_FRAMES * CHANNELS * BYTES_PER_SAMPLE)
  let used = 0
  try {
    writeSync(file, wavHeader(frames))
    for (let firstFrame = 0; firstFrame < frames; firstFrame += BLOCK_FRAMES) {
      const count = Math.min(BLOCK_FRAMES, frames - firstFrame)
      const times = new Float64Array(count)
      for (let index = 0; index < count; index += 1) {
        times[index] = (firstFrame + index) / SAMPLE_RATE
      }
      const output = firstFrame === 0
        ? session.tickViews({ t: times.subarray(1) })
        : session.tickViews({ t: times })
      const offset = firstFrame === 0 ? 1 : 0
      if (firstFrame === 0) {
        block.writeInt16LE(pcm16(initial.left[0], 0, 0), used)
        block.writeInt16LE(pcm16(initial.right[0], 0, 1), used + 2)
        used += 4
      }
      for (let index = offset; index < count; index += 1) {
        const outputIndex = index - offset
        const frame = firstFrame + index
        block.writeInt16LE(pcm16(output.left[outputIndex], frame, 0), used)
        block.writeInt16LE(pcm16(output.right[outputIndex], frame, 1), used + 2)
        used += 4
      }
      writeSync(file, block.subarray(0, used))
      used = 0
    }
  } catch (error) {
    closeSync(file)
    unlinkSync(outputPath)
    throw error
  }
  closeSync(file)
  const renderSeconds = (performance.now() - renderStarted) / 1_000
  console.log(`${outputPath}: ${frames} frames, audio duration ${(frames / SAMPLE_RATE).toFixed(6)} s, render time ${renderSeconds.toFixed(3)} s`)
}

const outputPath = process.argv[2]
const frameOption = process.argv.find((argument) => argument.startsWith("--frames="))
const importDir = importOption
  ? resolve(importOption.slice("--import-dir=".length))
  : modelPath ? dirname(modelPath) : undefined
const frameLimit = frameOption === undefined ? undefined : Number(frameOption.slice(9))
if (!outputPath || !modelPath || !importDir) {
  console.error("Usage: node render_session.mjs OUTPUT.wav --model=FILE [--import-dir=DIR] [--frames=COUNT]")
  process.exitCode = 2
} else if (frameLimit !== undefined && (!Number.isInteger(frameLimit) || frameLimit <= 0)) {
  console.error("--frames must be a positive integer")
  process.exitCode = 2
} else {
  render(resolve(outputPath), frameLimit, importDir)
}
