// Worker entrypoint — real router (plan 01-02, SRVC-01/SRVC-08, D-08).
// Plan 01-03: the DO call site routes through the PriceCoordinator Durable
// Object — one instance per currency via idFromName (D-05) — which owns
// coalescing, SQL persistence, freshness gates, stale-serve, and hold-off.
// Plan 01-04: a caches.default read-through tier sits AHEAD of the DO
// (SRVC-04) with canonical keys and FRESH-ONLY admission.
export { PriceCoordinator } from "./coordinator";

import { parsePricesRequest } from "./validate";
import { UpstreamError } from "./upstream";
import type { PriceEnvelope } from "./envelope";

export interface Env {
  PRICE_COORDINATOR: DurableObjectNamespace;
  COINGECKO_API_KEY?: string; // optional (D-04) — read only in upstream.ts
}

const JSON_HEADERS = { "content-type": "application/json; charset=utf-8" };

function errorResponse(
  status: number,
  code: string,
  message: string,
  extra?: Record<string, unknown>,
): Response {
  return new Response(
    JSON.stringify({ error: { code, message, ...extra } }),
    { status, headers: JSON_HEADERS },
  );
}

/**
 * Canonical cache key: sorted + deduped ids + vs on the fixed origin/path
 * (Landmine 6 — `ids=b,a` and `ids=a,b,a` must address the same entry).
 */
function canonicalCacheKey(ids: string[], currency: string): string {
  const key = new URL("https://token.gnus.ai/v1/prices");
  key.searchParams.set("ids", [...new Set(ids)].sort().join(","));
  key.searchParams.set("vs", currency);
  return key.toString();
}

/**
 * THE DO call site — cache-wrapped read-through (SRVC-04).
 *
 * 1. canonical-key match BEFORE the DO → per-colo hit returns immediately,
 *    bypassing the DO entirely.
 * 2. miss → the 01-03 DO routing, verbatim.
 * 3. FRESH-ONLY admission: parse the body once; admit ONLY when
 *    response.ok AND body.stale === false. This excludes BOTH error
 *    responses (429/5xx/502 — Landmine 8, no cached-error poisoning) AND
 *    D-07 stale-serve 200s (stale: true, source "coingecko-cache"): a bare
 *    response.ok guard would admit a stale envelope at e.g. age 270s and
 *    re-serve it up to 45s later at a true age >300s — a D-12 breach.
 * 4. TTL: fresh-only admission + max-age=45 < FRESH_SEC=60 ⇒ every cache
 *    hit is fresh-band — the cache tier structurally can never serve
 *    stale, keeping D-06/D-06a's two-value source honest.
 *
 * Body discipline (Landmine 15): the client response and the cached response
 * are REBUILT independently from the parsed body — no consumed-body clone.
 * Never any cookie headers on cached responses (Landmine 5).
 */
async function fetchFromCoordinator(
  env: Env,
  ids: string[],
  currency: string,
  ctx: ExecutionContext,
): Promise<Response> {
  const cacheKey = canonicalCacheKey(ids, currency);

  // Cache API requires Request-keyed match; a string URL alone matched too
  // broadly in this workerd build (empirically verified: distinct query
  // strings collided on one entry when passing the URL string).
  const cacheRequest = new Request(cacheKey, { method: "GET" });
  const hit = await caches.default.match(cacheRequest);
  if (hit) return hit; // per-colo hit; DO untouched

  const stub = env.PRICE_COORDINATOR.get(
    env.PRICE_COORDINATOR.idFromName(currency.toUpperCase()),
  );
  const doUrl = new URL("https://do/prices");
  doUrl.searchParams.set("ids", ids.join(","));
  doUrl.searchParams.set("vs", currency);
  const doResponse = await stub.fetch(doUrl.toString());

  if (!doResponse.ok) return doResponse; // errors pass through uncached

  const body = (await doResponse.json()) as PriceEnvelope;
  const clientResponse = new Response(JSON.stringify(body), doResponse);

  if (body.stale === false) {
    // FRESH-only admission (see docblock). Two independently built Responses.
    clientResponse.headers.set("Cache-Control", "public, max-age=45");
    const cached = new Response(JSON.stringify(body), {
      headers: {
        "Content-Type": "application/json; charset=utf-8",
        "Cache-Control": "public, max-age=45",
      },
    });
    ctx.waitUntil(caches.default.put(cacheRequest, cached));
  }
  return clientResponse;
}

export default {
  async fetch(
    request: Request,
    env: Env,
    ctx: ExecutionContext,
  ): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname !== "/v1/prices") {
      return errorResponse(404, "not_found", `unknown path: ${url.pathname}`);
    }
    if (request.method !== "GET") {
      return errorResponse(405, "method_not_allowed", `method ${request.method} not allowed; use GET`);
    }

    // Validation BEFORE any DO/cache interaction (ASVS V5, T-01-03).
    const parsed = parsePricesRequest(url);
    if (!parsed.ok) {
      return errorResponse(parsed.status, parsed.code, parsed.message);
    }

    try {
      return await fetchFromCoordinator(env, parsed.ids, parsed.currency, ctx);
    } catch (e) {
      // Nothing escapes fetch() as an exception (D-08) — never a 500 crash.
      if (e instanceof UpstreamError) {
        return errorResponse(502, "upstream_error", e.message, e.upstreamStatus !== undefined ? { upstreamStatus: e.upstreamStatus } : undefined);
      }
      const message = e instanceof Error ? e.message : "internal error";
      return errorResponse(502, "upstream_error", message);
    }
  },
};
