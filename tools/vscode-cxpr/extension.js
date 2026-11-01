const path = require("path");
const vscode = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client;

async function activate(context) {
  const watcher = vscode.workspace.createFileSystemWatcher("**/*.cxpr");
  let outputChannel;
  try {
    outputChannel = vscode.window.createOutputChannel("CXPR Language Server", { log: true });
  } catch (_error) {
    outputChannel = vscode.window.createOutputChannel("CXPR Language Server");
  }
  const serverModule = context.asAbsolutePath(path.join("server", "server.js"));
  const configuration = vscode.workspace.getConfiguration("cxprLanguage");
  const clientOptions = {
    documentSelector: [{ language: "cxpr", scheme: "file" }],
    synchronize: { configurationSection: "cxprLanguage", fileEvents: watcher },
    initializationOptions: {
      transport: configuration.get("analyzer.transport", "wasm"),
      cli: configuration.get("tooling.executable", ""),
      wasmAnalyzerModule: context.asAbsolutePath(path.join("runtime", "wasm-analyzer.js")),
      wasmFactoryModule: context.asAbsolutePath(path.join("runtime", "cxpr_wasm.js")),
      wasmPath: context.asAbsolutePath(path.join("runtime", "cxpr_wasm.wasm"))
    }
  };
  // Older VS Code releases expose OutputChannel, not LogOutputChannel.
  // LanguageClient only accepts the latter as its outputChannel option.
  if (typeof outputChannel.error === "function") clientOptions.outputChannel = outputChannel;

  client = new LanguageClient(
    "cxprLanguageServer",
    "CXPR Language Server",
    {
      run: { module: serverModule, transport: TransportKind.stdio },
      debug: { module: serverModule, transport: TransportKind.stdio }
    },
    clientOptions
  );
  context.subscriptions.push(watcher, outputChannel, client);
  outputChannel.appendLine(`Starting CXPR language server: ${serverModule}`);
  try {
    await client.start();
    outputChannel.appendLine("CXPR language server is ready.");
  } catch (error) {
    const detail = error instanceof Error ? error.stack || error.message : String(error);
    outputChannel.appendLine(`CXPR language server failed to start: ${detail}`);
    outputChannel.show(true);
    vscode.window.showErrorMessage("CXPR language server failed to start. See Output → CXPR Language Server.");
  }
}

async function deactivate() {
  if (client) await client.stop();
  client = undefined;
}

module.exports = { activate, deactivate };
