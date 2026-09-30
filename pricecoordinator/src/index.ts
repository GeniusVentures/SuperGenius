// Worker entrypoint — real router (plan 01-02, SRVC-01/SRVC-08, D-08).
// Plan 01-03: the DO call site routes through the PriceCoordinator Durable
// Object — one instance per currency via idFromName (D-05) — which owns
// coalescing, SQL persistence, freshness gates, stale-serve, and hold-off.
export { PriceCoordinator } from "./coordinator";

import { parsePricesRequest } from "./validate";
import { UpstreamError } from "./upstream";

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
 * Routes to the PriceCoordinator DO instance for the request's currency
 * (idFromName — D-05). The DO request is a synthetic URL carrying the
 * validated ids/currency as query params (the DO re-validates cheaply).
 * The DO's Response (200 envelope, stale envelope, or D-08 error) is
 * relayed verbatim.
 */
async function fetchFromCoordinator(
  env: Env,
  ids: string[],
  currency: string,
): Promise<Response> {
  const stub = env.PRICE_COORDINATOR.get(
    env.PRICE_COORDINATOR.idFromName(currency.toUpperCase()),
  );
  const doUrl = new URL("https://do/prices");
  doUrl.searchParams.set("ids", ids.join(","));
  doUrl.searchParams.set("vs", currency);
  return stub.fetch(doUrl.toString());
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
