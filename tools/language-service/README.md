# @cxpr/language-service

Editor-neutral, publishable cxpr language-service owned by the cxpr source tree. It depends only on
the injected `IAnalyzer` and `IWorkspace` contracts. LSP, VS Code and Monaco adapters belong to
their consumers and are deliberately not dependencies of this package.

The retained parity corpus exercises diagnostics, completions, hover, definitions, symbols and
semantic tokens directly against the authoritative C-backed service. The former JavaScript parity
runtime was removed after the transport cutover.
