#!/usr/bin/env node

const fs = require("fs");
const path = require("path");
const { buildSync } = require("esbuild");

const output = path.join(__dirname, "dist", "server.js");
fs.mkdirSync(path.dirname(output), { recursive: true });
buildSync({
  entryPoints: [path.join(__dirname, "lsp-entry.js")],
  outfile: output,
  bundle: true,
  platform: "node",
  format: "cjs",
  target: "node18"
});
fs.chmodSync(output, 0o755);
console.log(output);
