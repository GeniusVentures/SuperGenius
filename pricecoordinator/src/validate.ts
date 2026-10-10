// Request validation (SRVC-08) — allowlist-only (ASVS V5). Runs BEFORE any
// cache or DO interaction. Error code tokens per D-08.
import { MAX_IDS_PER_REQUEST } from "./envelope";

export type ParsedPricesRequest =
  | { ok: true; ids: string[]; currency: string }
  | { ok: false; status: 400; code: "invalid_request"; message: string };

const ID_RE = /^[a-z0-9-]+$/;
const VS_RE = /^[a-z]{2,10}$/;

export function parsePricesRequest(url: URL): ParsedPricesRequest {
  const idsRaw = url.searchParams.get("ids");
  const vs = url.searchParams.get("vs");

  if (idsRaw === null || idsRaw.length === 0) {
    return bad("missing or empty 'ids' query parameter");
  }
  const tokens = idsRaw.split(",").filter((t) => t.length > 0);
  if (tokens.length === 0) {
    return bad("'ids' contained no non-empty tokens");
  }
  for (const t of tokens) {
    if (!ID_RE.test(t)) {
      return bad(`invalid id '${t}': ids must match [a-z0-9-]+`);
    }
  }
  // Dedupe (order is irrelevant downstream — canonicalization happens later).
  const ids = [...new Set(tokens)];
  if (ids.length > MAX_IDS_PER_REQUEST) {
    return bad(`too many ids: ${ids.length} > ${MAX_IDS_PER_REQUEST}`);
  }

  if (vs === null || !VS_RE.test(vs)) {
    return bad("missing or invalid 'vs' query parameter (lowercase letters, 2-10 chars)");
  }

  return { ok: true, ids, currency: vs };
}

function bad(message: string): ParsedPricesRequest {
  return { ok: false, status: 400, code: "invalid_request", message };
}
