// Shared test helpers (plan 01-05).

/**
 * Minimal JSONC stripper: removes line comments (double-slash to end of
 * line) and block comments (slash-star to star-slash) so JSON.parse can
 * consume a .jsonc file. LIMITATION: a naive stripper — it does not
 * protect comment-marker sequences inside JSON string VALUES. The
 * wrangler.jsonc we parse contains none today (verified); if one is ever
 * needed, switch to a proper tokenizer.
 */
export function parseJsonc(text: string): unknown {
  const stripped = text
    .replace(/\/\*[\s\S]*?\*\//g, "") // block comments
    .replace(/^[ \t]*\/\/.*$/gm, ""); // line comments (line-leading only)
  return JSON.parse(stripped);
}
