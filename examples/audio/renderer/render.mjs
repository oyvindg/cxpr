import { createHash } from "node:crypto"
import { existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, statSync } from "node:fs"
import { tmpdir } from "node:os"
import { dirname, join, resolve } from "node:path"
import { spawnSync } from "node:child_process"
import { fileURLToPath } from "node:url"

const here = dirname(fileURLToPath(import.meta.url))
const cxprRoot = resolve(here, "..", "..", "..")
const modelOption = process.argv.find((argument) => argument.startsWith("--model="))
const importOption = process.argv.find((argument) => argument.startsWith("--import-dir="))
const modelPath = modelOption ? resolve(modelOption.slice("--model=".length)) : undefined
const importDir = importOption
  ? resolve(importOption.slice("--import-dir=".length))
  : modelPath ? dirname(modelPath) : undefined
const hostPath = join(here, "render_native.c")
const nativeCompileFlags = [
  "-std=c11",
  "-O3",
  // Generated models diagnose non-finite samples explicitly and never inspect
  // errno or floating-point traps. Tell the C optimizer so repeated pure math
  // expressions can be folded and shared without weakening value semantics.
  "-fno-math-errno",
  "-fno-trapping-math"
]

function run(command, args) {
  const result = spawnSync(command, args, { stdio: "inherit" })
  if (result.error) throw result.error
  if (result.status !== 0) process.exit(result.status ?? 1)
}

function findCodegen() {
  const configured = process.env.CXPR_MODEL_CODEGEN
  const candidates = [
    configured,
    // Prefer the standalone CXPR build because it is guaranteed to match the
    // source tree containing this renderer. The parent Dynasty build may lag
    // behind while CXPR is developed as a submodule.
    join(cxprRoot, "build", "cxpr_model_codegen"),
    resolve(cxprRoot, "..", "..", "build", "libs", "cxpr", "cxpr_model_codegen")
  ]
  const codegen = candidates.find((candidate) => candidate && existsSync(candidate))
  if (!codegen) {
    throw new Error(
      "cxpr_model_codegen was not found; build it or set CXPR_MODEL_CODEGEN"
    )
  }
  return codegen
}

function nativeCacheKey(codegen) {
  const hash = createHash("sha256")
  const sources = readdirSync(importDir, { recursive: true })
    .filter((path) => path.endsWith(".cxpr"))
    .sort()
  hash.update(process.env.CC ?? "cc")
  hash.update(nativeCompileFlags.join(" "))
  // The import tree can be shared by several root songs. Include the selected
  // root explicitly so one example can never reuse another example's binary.
  hash.update(modelPath)
  hash.update(readFileSync(hostPath))
  hash.update(`${statSync(codegen).size}:${statSync(codegen).mtimeMs}`)
  for (const path of sources) {
    hash.update(path)
    hash.update(readFileSync(join(importDir, path)))
  }
  return hash.digest("hex")
}

const outputPath = process.argv[2]
const options = process.argv.slice(3).filter((option) =>
  !option.startsWith("--model=") && !option.startsWith("--import-dir="))
if (!outputPath || !modelPath || !importDir) {
  console.error("Usage: node render.mjs OUTPUT.wav --model=FILE [--import-dir=DIR] [--frames=COUNT] [--session]")
  process.exitCode = 2
} else if (options.includes("--session")) {
  run(process.execPath, [join(here, "render_session.mjs"), outputPath,
    `--model=${modelPath}`, `--import-dir=${importDir}`,
    ...options.filter((option) => option !== "--session")])
} else {
  const codegen = findCodegen()
  const cacheDir = join(tmpdir(), "cxpr-native-audio-cache")
  mkdirSync(cacheDir, { recursive: true })
  const rendererPath = join(cacheDir, nativeCacheKey(codegen))
  const buildDir = mkdtempSync(join(tmpdir(), "cxpr-native-song-"))
  try {
    if (!existsSync(rendererPath)) {
      const generatedPath = join(buildDir, "song.gen.c")
      run(codegen, [
        "--model", modelPath,
        "--import-dir", importDir,
        "--output", generatedPath
      ])
      run(process.env.CC ?? "cc", [
        ...nativeCompileFlags,
        "-I", join(cxprRoot, "include"),
        "-I", buildDir,
        hostPath,
        "-lm",
        "-o", rendererPath
      ])
    }
    run(rendererPath, [resolve(outputPath), ...options])
  } finally {
    rmSync(buildDir, { recursive: true, force: true })
  }
}
