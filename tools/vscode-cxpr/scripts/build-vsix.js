#!/usr/bin/env node

const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync } = require("child_process");

function fail(message) {
  console.error(message);
  process.exit(1);
}

function ensureDir(dirPath) {
  fs.mkdirSync(dirPath, { recursive: true });
}

function escapeXml(value) {
  return String(value)
    .replace(/&/g, "&amp;")
    .replace(/"/g, "&quot;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

function hasZip() {
  try {
    execFileSync("zip", ["-v"], { stdio: "ignore" });
    return true;
  } catch {
    return false;
  }
}

function copyFileIntoStage(srcRoot, stageRoot, relPath) {
  const srcPath = path.join(srcRoot, relPath);
  const dstPath = path.join(stageRoot, "extension", relPath);
  ensureDir(path.dirname(dstPath));
  fs.copyFileSync(srcPath, dstPath);
}

function copyExternalFileIntoStage(stageRoot, srcPath, relPath) {
  const dstPath = path.join(stageRoot, "extension", relPath);
  ensureDir(path.dirname(dstPath));
  fs.copyFileSync(srcPath, dstPath);
}

function copyCxprTreeIntoStage(stageRoot, srcRoot, relRoot) {
  if (!fs.existsSync(srcRoot)) return 0;
  let copied = 0;
  for (const entry of fs.readdirSync(srcRoot, { withFileTypes: true })) {
    const srcPath = path.join(srcRoot, entry.name);
    const relPath = path.join(relRoot, entry.name);
    if (entry.isDirectory()) copied += copyCxprTreeIntoStage(stageRoot, srcPath, relPath);
    else if (entry.isFile() && entry.name.endsWith(".cxpr")) {
      copyExternalFileIntoStage(stageRoot, srcPath, relPath);
      copied++;
    }
  }
  return copied;
}

function bundle(entryPath, outputPath, external = []) {
  const esbuild = path.join(__dirname, "..", "node_modules", ".bin", "esbuild");
  if (!fs.existsSync(esbuild)) {
    fail("esbuild not found. Run npm install in tools/vscode-cxpr and retry. https://esbuild.github.io/getting-started/");
  }
  ensureDir(path.dirname(outputPath));
  const args = [entryPath, "--bundle", "--platform=node", "--format=cjs", `--outfile=${outputPath}`];
  for (const name of external) args.push(`--external:${name}`);
  execFileSync(esbuild, args, { stdio: "inherit" });
}

function collectFiles(rootDir) {
  const out = [];
  const skipDirs = new Set(["dist", ".vscode", "test", "scripts", "node_modules"]);

  function walk(currentDir, relDir = "") {
    const entries = fs.readdirSync(currentDir, { withFileTypes: true });
    for (const entry of entries) {
      if (entry.name === ".gitignore") continue;
      const relPath = relDir ? path.join(relDir, entry.name) : entry.name;
      const fullPath = path.join(currentDir, entry.name);

      if (entry.isDirectory()) {
        if (skipDirs.has(entry.name)) continue;
        walk(fullPath, relPath);
        continue;
      }
      if (!entry.isFile()) continue;
      out.push(relPath);
    }
  }

  walk(rootDir);
  return out.sort();
}

function buildManifest(pkg) {
  const extensionId = `${pkg.publisher}.${pkg.name}`;
  const description = pkg.description || pkg.displayName || pkg.name;
  const categories = Array.isArray(pkg.categories) ? pkg.categories.join(",") : "";

  return `<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Id="${escapeXml(extensionId)}" Version="${escapeXml(pkg.version)}" Language="en-US" Publisher="${escapeXml(pkg.publisher)}" />
    <DisplayName>${escapeXml(pkg.displayName || pkg.name)}</DisplayName>
    <Description xml:space="preserve">${escapeXml(description)}</Description>
    <Categories>${escapeXml(categories)}</Categories>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="${escapeXml(pkg.engines.vscode)}" />
    </Properties>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code" Version="${escapeXml(pkg.engines.vscode)}" />
  </Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/README.md" Addressable="true" />
  </Assets>
</PackageManifest>
`;
}

function buildContentTypes() {
  return `<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="json" ContentType="application/json" />
  <Default Extension="js" ContentType="application/javascript" />
  <Default Extension="md" ContentType="text/markdown" />
  <Default Extension="txt" ContentType="text/plain" />
  <Default Extension="xml" ContentType="text/xml" />
  <Default Extension="vsixmanifest" ContentType="text/xml" />
</Types>
`;
}

function main() {
  if (!hasZip()) {
    fail("zip CLI not found. Install Info-ZIP/zip and retry. See: https://infozip.sourceforge.net/");
  }

  const rootDir = path.resolve(__dirname, "..");
  const pkgPath = path.join(rootDir, "package.json");
  if (!fs.existsSync(pkgPath)) fail(`Missing package.json at ${pkgPath}`);

  const pkg = JSON.parse(fs.readFileSync(pkgPath, "utf8"));
  if (!pkg.name || !pkg.publisher || !pkg.version || !pkg.engines || !pkg.engines.vscode) {
    fail("package.json must include name, publisher, version and engines.vscode");
  }
  if (pkg.contributes?.semanticTokenScopes &&
      !Array.isArray(pkg.contributes.semanticTokenScopes)) {
    fail("package.json contributes.semanticTokenScopes must be an array");
  }

  const distDir = path.join(rootDir, "dist");
  ensureDir(distDir);

  const outputPath = path.join(distDir, `${pkg.name}-${pkg.version}.vsix`);
  const stageRoot = fs.mkdtempSync(path.join(os.tmpdir(), "cxpr-vsix-"));
  ensureDir(path.join(stageRoot, "extension"));

  for (const relPath of collectFiles(rootDir)) {
    copyFileIntoStage(rootDir, stageRoot, relPath);
  }

  bundle(
    path.join(rootDir, "extension.js"),
    path.join(stageRoot, "extension", "extension.js"),
    ["vscode"]
  );

  const serverPath = path.resolve(rootDir, "..", "language-server", "dist", "server.js");
  if (!fs.existsSync(serverPath)) {
    fail("CXPR language server is not built. Run npm install && npm run build in libs/cxpr/tools/language-server. See its README.md");
  }
  copyExternalFileIntoStage(stageRoot, serverPath, path.join("server", "server.js"));

  const executableName = process.platform === "win32"
    ? "cxpr_document_tooling.exe"
    : "cxpr_document_tooling";
  const cxprRoot = path.resolve(rootDir, "..", "..");
  const toolingCandidates = [
    process.env.CXPR_DOCUMENT_TOOLING,
    path.join(cxprRoot, "build", executableName),
    path.resolve(cxprRoot, "..", "..", "build", "libs", "cxpr", executableName),
    path.resolve(cxprRoot, "..", "..", "build", "libs", "cxpr", "Release", executableName)
  ];
  const toolingPath = toolingCandidates.find((candidate) => candidate && fs.existsSync(candidate));
  if (!toolingPath) {
    fail(`CXPR document tooling is not built. Build cxpr_document_tooling or set CXPR_DOCUMENT_TOOLING. Searched: ${toolingCandidates.filter(Boolean).join(", ")}`);
  }
  const stagedToolingPath = path.join("tooling", executableName);
  copyExternalFileIntoStage(stageRoot, toolingPath, stagedToolingPath);
  fs.chmodSync(path.join(stageRoot, "extension", stagedToolingPath), 0o755);

  const dynCxprRoot = path.resolve(cxprRoot, "..", "dyn", "cxpr");
  const bundledLibraryCount = copyCxprTreeIntoStage(
    stageRoot, dynCxprRoot, path.join("library", "dyn", "cxpr"));
  if (bundledLibraryCount) console.log(`Bundled ${bundledLibraryCount} CXPR library sources`);

  const sharedThemePath = path.resolve(rootDir, "..", "theme", "cxpr-color-profile.json");
  if (fs.existsSync(sharedThemePath)) {
    copyExternalFileIntoStage(stageRoot, sharedThemePath, path.join("theme", "cxpr-color-profile.json"));
  }

  fs.writeFileSync(path.join(stageRoot, "extension.vsixmanifest"), buildManifest(pkg), "utf8");
  fs.writeFileSync(path.join(stageRoot, "[Content_Types].xml"), buildContentTypes(), "utf8");

  if (fs.existsSync(outputPath)) {
    fs.rmSync(outputPath, { force: true });
  }

  execFileSync("zip", ["-qr", outputPath, "."], { cwd: stageRoot, stdio: "inherit" });
  fs.rmSync(stageRoot, { recursive: true, force: true });

  console.log(outputPath);
}

main();
