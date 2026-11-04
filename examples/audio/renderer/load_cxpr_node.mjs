import { existsSync } from "node:fs"
import { isAbsolute, resolve } from "node:path"
import { pathToFileURL } from "node:url"

/**
 * Loads the optional cxpr-node adapter without coupling standalone CXPR to a
 * particular monorepo layout.
 *
 * @returns {Promise<Record<string, unknown>>} Loaded cxpr-node module.
 */
export async function loadCxprNode() {
  const configured = process.env.CXPR_NODE_MODULE ?? process.env.CXPR_NODE
  const monorepoModule = resolve(import.meta.dirname,
    "..", "..", "..", "..", "cxpr-node", "dist", "index.js")
  const candidates = [configured, "cxpr-node",
    existsSync(monorepoModule) ? monorepoModule : undefined].filter(Boolean)

  for (const candidate of candidates) {
    try {
      const specifier = isAbsolute(candidate) ? pathToFileURL(candidate).href : candidate
      return await import(specifier)
    } catch {
      // Try the next explicitly supported installation layout.
    }
  }

  throw new Error(
    "cxpr-node is required for --views and --generated-node. " +
    "Install/build cxpr-node, or set CXPR_NODE_MODULE to its dist/index.js file."
  )
}
