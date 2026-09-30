// Worker entrypoint — real router (plan 01-02, SRVC-01/SRVC-08, D-08).
// The PriceCoordinator DO class MUST be re-exported: wrangler's `main` must
// export every Durable Object class named in wrangler.jsonc.
export { PriceCoordinator } from "./coordinator";

import { buildEnvelope, nowSec } from "./envelope";
import { parsePricesRequest } from "./validate";
import { fetchUpstream, UpstreamError } from "./upstream";

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
 * THE DO call site — one seam function so plan 01-04 can wrap it with the
 * caches.default read-through without touching the router.
 *
 * interim: direct upstream; replaced by PriceCoordinator DO in plan 01-03
 * (DO fetch + stale-serve + hold-off). Until then this keeps the router/
 * envelope layer fully testable end-to-end.
 */
async function fetchFromCoordinator(
  env: Env,
  ids: string[],
  currency: string,
): Promise<Response> {
  const rows = await fetchUpstream(ids, currency, env);
  const envelope = buildEnvelope(currency, rows, ids, nowSec(), false);
  return new Response(JSON.stringify(envelope), {
    status: 200,
    headers: JSON_HEADERS,
  });
}

export default {
  async fetch(
    request: Request,
    env: Env,
    _ctx: ExecutionContext,
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
      return await fetchFromCoordinator(env, parsed.ids, parsed.currency);
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
