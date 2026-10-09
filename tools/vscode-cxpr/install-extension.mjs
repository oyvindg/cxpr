#!/usr/bin/env node

import { readFileSync } from "node:fs"
import { fileURLToPath } from "node:url"
import { spawnSync } from "node:child_process"

const rootUrl = new URL("./", import.meta.url)
const pkg = JSON.parse(readFileSync(new URL("package.json", rootUrl), "utf8"))
const extensionId = `${pkg.publisher}.${pkg.name}`
const vsixPath = fileURLToPath(
  new URL(`dist/${pkg.name}-${pkg.version}.vsix`, rootUrl)
)
const cli = process.env.CXPR_VSCODE_CLI || "code"

spawnSync(cli, ["--uninstall-extension", extensionId], {
  shell: process.platform === "win32",
  stdio: "ignore"
})

const result = spawnSync(cli, ["--install-extension", vsixPath, "--force"], {
  shell: process.platform === "win32",
  stdio: "inherit"
})

if (result.error) {
  console.error(`Unable to run the VS Code CLI '${cli}': ${result.error.message}`)
  console.error("Install VS Code or configure its CLI: https://code.visualstudio.com/docs/setup/setup-overview")
  console.error("You can also set CXPR_VSCODE_CLI to the executable path.")
  process.exitCode = 1
} else {
  process.exitCode = result.status ?? 1
}
