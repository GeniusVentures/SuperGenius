// CoinGecko upstream client (plan 01-02, SRVC-06/D-04/D-05/KF-11).
// The API key is read ONLY here. Status is checked BEFORE JSON parsing —
// a 403-HTML body must never reach JSON.parse (T-01-06).
import { nowSec, UPSTREAM_TIMEOUT_MS, type PriceRow } from "./envelope";
import type { Env } from "./index";

const UPSTREAM_BASE = "https://api.coingecko.com/api/v3/simple/price";

/** Typed upstream failure carrying the truthful HTTP status (D-08). */
export class UpstreamError extends Error {
  readonly upstreamStatus?: number;
  constructor(message: string, upstreamStatus?: number) {
    super(message);
    this.name = "UpstreamError";
    this.upstreamStatus = upstreamStatus;
  }
}

/**
 * Fetch `/simple/price` for the given ids/currency. Ids are sorted for
 * URL cacheability. Returns rows ONLY for ids CoinGecko returned (D-09 —
 * unknown ids are simply absent). Throws UpstreamError on non-2xx or abort.
 */
export async function fetchUpstream(
  ids: string[],
  currency: string,
  env: Env,
): Promise<Map<string, PriceRow>> {
  const url = `${UPSTREAM_BASE}?ids=${[...ids].sort().join(",")}&vs_currencies=${currency}`;

  const headers: Record<string, string> = {};
  // D-04/D-05: key applied identically on every upstream call, iff present.
  if (env.COINGECKO_API_KEY) {
    headers["x-cg-demo-api-key"] = env.COINGECKO_API_KEY;
  }
  // No Accept-Encoding header — identity default (diagnosis forward-compat).

  // JS-level timeout so vitest fake timers can drive it (KF-11). Never
  // AbortSignal.timeout() — native, not fake-timer-controllable.
  const ctrl = new AbortController();
  const timer = setTimeout(
    () => ctrl.abort(new UpstreamError("upstream timeout")),
    UPSTREAM_TIMEOUT_MS,
  );
  let response: Response;
  try {
    response = await fetch(url, { headers, signal: ctrl.signal });
  } catch (e) {
    if (e instanceof UpstreamError) throw e;
    throw new UpstreamError(e instanceof Error ? e.message : "upstream fetch failed");
  } finally {
    clearTimeout(timer);
  }

  // Status BEFORE parse (T-01-06): non-2xx bodies are never parsed as JSON.
  if (!response.ok) {
    throw new UpstreamError(`coinGecko responded ${response.status}`, response.status);
  }

  const parsed = (await response.json()) as Record<string, Record<string, number>>;
  const rows = new Map<string, PriceRow>();
  const fetchedAt = nowSec();
  for (const id of Object.keys(parsed)) {
    const price = parsed[id]?.[currency];
    if (typeof price === "number" && Number.isFinite(price)) {
      rows.set(id, { id, price, fetchedAt });
    }
  }
  return rows;
}
