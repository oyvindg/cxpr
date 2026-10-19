#!/usr/bin/env node

const lsp = require("vscode-languageserver/node");
const { createLanguageServer } = require("./lsp-server");

const connection = lsp.createConnection(
  new lsp.StreamMessageReader(process.stdin),
  new lsp.StreamMessageWriter(process.stdout),
  lsp.ProposedFeatures.all
);
createLanguageServer(connection).listen();
