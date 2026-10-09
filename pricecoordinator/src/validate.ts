// Request validation (SRVC-08) — two stages, both running BEFORE any cache
// or DO interaction (ASVS V5): (1) well-formedness (format, dedupe, caps),
// then (2) allowlist membership — anti-proxy-abuse hardening so the endpoint
// cannot be used as a general-purpose CoinGecko proxy. Error code tokens
// per D-08.
import { MAX_IDS_PER_REQUEST } from "./envelope";

export type ParsedPricesRequest =
  | { ok: true; ids: string[]; currency: string }
  | { ok: false; status: 400; code: "invalid_request"; message: string };

const ID_RE = /^[a-z0-9-]+$/;
const VS_RE = /^[a-z]{2,10}$/;

/**
 * Allowlist enforcement (SRVC-08 hardening). Plain wrangler vars — public
 * config, deliberately NOT secrets (D-04 scope); plain env, no store
 * bindings (SRVC-07). Default-closed: unset/empty vars restrict to
 * genius-ai/usd — unconfigured can never mean unrestricted.
 */
export const DEFAULT_ALLOWED_IDS = "genius-ai";
export const DEFAULT_ALLOWED_VS = "usd";

export interface AllowedLists {
  ids: ReadonlySet<string>;
  vs: ReadonlySet<string>;
}

/** Structural env shape — do NOT import Env from index.ts (circular). */
interface AllowlistEnv {
  ALLOWED_IDS?: string;
  ALLOWED_VS?: string;
}

function parseList(raw: string | undefined, fallback: string): ReadonlySet<string> {
  const tokens = (raw ?? "")
    .split(",")
    .map((t) => t.trim().toLowerCase())
    .filter((t) => t.length > 0);
  // Fail closed: unset OR whitespace/commas-only → the default constant.
  return tokens.length > 0 ? new Set(tokens) : new Set([fallback]);
}

export function resolveAllowlist(env: AllowlistEnv): AllowedLists {
  return {
    ids: parseList(env.ALLOWED_IDS, DEFAULT_ALLOWED_IDS),
    vs: parseList(env.ALLOWED_VS, DEFAULT_ALLOWED_VS),
  };
}

export function parsePricesRequest(
  url: URL,
  allowed: AllowedLists = {
    ids: new Set([DEFAULT_ALLOWED_IDS]),
    vs: new Set([DEFAULT_ALLOWED_VS]),
  },
): ParsedPricesRequest {
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

  // Stage 2 — allowlist membership (anti-proxy-abuse, SRVC-08). Format
  // validation has already run above; well-formed but disallowed tokens are
  // rejected here with the same 400 invalid_request shape (D-08).
  for (const t of ids) {
    if (!allowed.ids.has(t)) {
      return bad(`id '${t}' is not allowed on this endpoint`);
    }
  }
  if (!allowed.vs.has(vs)) {
    return bad(`currency '${vs}' is not allowed on this endpoint`);
  }

  return { ok: true, ids, currency: vs };
}

function bad(message: string): ParsedPricesRequest {
  return { ok: false, status: 400, code: "invalid_request", message };
}
