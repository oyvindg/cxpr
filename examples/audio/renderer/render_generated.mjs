import { closeSync, openSync, unlinkSync, writeSync } from "node:fs"
import { resolve } from "node:path"

import { loadCxprNode } from "./load_cxpr_node.mjs"

const SAMPLE_RATE = 44_100
const CHANNELS = 2
const BYTES_PER_SAMPLE = 2
const BLOCK_FRAMES = 4_096

/** @param {number} frames @returns {Buffer} */
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

/** @param {number} sample @param {number} frame @param {number} channel @returns {number} */
function pcm16(sample, frame, channel) {
  if (!Number.isFinite(sample) || Math.abs(sample) > 1) {
    throw new Error(`Invalid sample at frame ${frame}, channel ${channel}`)
  }
  return Math.round(sample * 32_767)
}

function channels(output) {
  if (output.stereo_left && output.stereo_right)
    return { left: output.stereo_left, right: output.stereo_right }
  if (output.mono) return { left: output.mono, right: output.mono }
  return { left: output.left, right: output.right }
}

/**
 * Renders a trusted generated artifact through cxpr-node.
 * @param {string} artifactPath Compiled shared-library artifact.
 * @param {string} outputPath Destination WAV path.
 * @param {number | undefined} frameLimit Optional smoke-test frame limit.
 */
async function render(artifactPath, outputPath, frameLimit) {
  const { loadGeneratedModel } = await loadCxprNode()
  using model = loadGeneratedModel({ artifactPath })
  using session = model.newSession()
  const initial = session.tickBatch({ t: new Float64Array([0]) })
  const initialChannels = channels(initial)
  const duration = Number(initial.duration[0])
  if (!Number.isFinite(duration) || duration <= 0) {
    throw new Error("The generated song model returned an invalid duration")
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
        ? session.tickBatch({ t: times.subarray(1) })
        : session.tickBatch({ t: times })
      const offset = firstFrame === 0 ? 1 : 0
      const outputChannels = channels(output)
      if (firstFrame === 0) {
        block.writeInt16LE(pcm16(initialChannels.left[0], 0, 0), used)
        block.writeInt16LE(pcm16(initialChannels.right[0], 0, 1), used + 2)
        used += 4
      }
      for (let index = offset; index < count; index += 1) {
        const outputIndex = index - offset
        const frame = firstFrame + index
        block.writeInt16LE(pcm16(outputChannels.left[outputIndex], frame, 0), used)
        block.writeInt16LE(pcm16(outputChannels.right[outputIndex], frame, 1), used + 2)
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
  console.log(`${outputPath}: ${frames} frames, generated-node, audio duration ${(frames / SAMPLE_RATE).toFixed(6)} s, render time ${renderSeconds.toFixed(3)} s`)
}

const outputPath = process.argv[2]
const artifactOption = process.argv.find((argument) => argument.startsWith("--artifact="))
const frameOption = process.argv.find((argument) => argument.startsWith("--frames="))
const sampleRateOption = process.argv.find((argument) => argument.startsWith("--sample-rate="))
const artifactPath = artifactOption ? resolve(artifactOption.slice("--artifact=".length)) : undefined
const frameLimit = frameOption === undefined ? undefined : Number(frameOption.slice(9))
const requestedSampleRate = sampleRateOption === undefined
  ? SAMPLE_RATE
  : Number(sampleRateOption.slice("--sample-rate=".length))
if (!outputPath || !artifactPath) {
  console.error("Usage: node render_generated.mjs OUTPUT.wav --artifact=FILE [--frames=COUNT]")
  process.exitCode = 2
} else if (frameLimit !== undefined && (!Number.isInteger(frameLimit) || frameLimit <= 0)) {
  console.error("--frames must be a positive integer")
  process.exitCode = 2
} else if (requestedSampleRate !== SAMPLE_RATE) {
  console.error(`--generated-node currently requires the model default sample rate (${SAMPLE_RATE})`)
  process.exitCode = 2
} else {
  await render(artifactPath, resolve(outputPath), frameLimit)
}
