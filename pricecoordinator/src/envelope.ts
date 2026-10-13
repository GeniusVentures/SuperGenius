// Envelope model — the contract Phase 2/3 C++ code parses (plan 01-02, SRVC-01/FRESH-01).
// Semantics: D-06/D-06a (source values), D-09 (partial coverage), D-11 (max/age/stale),
// D-12 (freshness bands). Timestamps are epoch SECONDS everywhere (Landmine 10).

export interface PriceRow {
  id: string;
  price: number;
  fetchedAt: number; // epoch seconds
}

/** Exactly two values (D-06) — no third value for cache-tier-served responses. */
export type SourceKind = "coingecko" | "coingecko-cache";

export interface PriceEnvelope {
  currency: string;
  prices: Record<string, number>;
  fetchedAt: number; // epoch seconds — max(fetchedAt of returned ids)
  age: number; // seconds — now − fetchedAt
  source: SourceKind;
  stale: boolean;
}

// --- Band / tuning constants: single source of truth; tests import these ---
export const FRESH_SEC = 60; // ≤60s is fresh (D-12)
export const STALE_SEC = 300; // 60s–5min stale-but-usable; >5min unavailable
export const BATCH_WINDOW_MS = 15; // DO collecting window (plan 01-03)
export const UPSTREAM_TIMEOUT_MS = 8_000; // JS-level upstream timeout (KF-11)
export const MAX_IDS_PER_REQUEST = 50; // SRVC-08 cap

export type AgeBand = "fresh" | "stale" | "unavailable";

/** The ONLY place epoch milliseconds are converted to seconds (Landmine 10). */
export function nowSec(): number {
  return parseSec(Date.now());
}

/** Floor fractional seconds to whole epoch seconds. */
export function parseSec(ms: number): number {
  return Math.floor(ms / 1000);
}

export function classifyAge(ageSec: number): AgeBand {
  if (ageSec <= FRESH_SEC) return "fresh";
  if (ageSec <= STALE_SEC) return "stale";
  return "unavailable";
}

/**
 * Filter rows to the servable bands: fresh + stale. Rows >5min old are never
 * served (D-12). Partial input maps are preserved as Maps of survivors.
 */
export function usableRows(
  rows: Map<string, PriceRow>,
  now: number,
): Map<string, PriceRow> {
  const out = new Map<string, PriceRow>();
  for (const [id, r] of rows) {
    if (classifyAge(now - r.fetchedAt) !== "unavailable") out.set(id, r);
  }
  return out;
}

/**
 * Build the response envelope (D-11):
 *   fetchedAt = max(fetchedAt of returned ids)
 *   age       = now − that max
 *   stale     = ANY returned id > FRESH_SEC old
 *   source (D-06a, freshness-based):
 *     - "coingecko" when every returned id is ≤60s old — INCLUDING fresh data
 *       served from storage without a refetch — and on mixed-band SUCCESS
 *       responses (some id 61s–5min old but the request didn't come through
 *       the stale-serve-on-upstream-failure path);
 *     - "coingecko-cache" ONLY when the caller explicitly flags a stale-serve
 *       (upstream failed, usable rows served anyway — D-07).
 * `prices` contains only requested ids that have rows (D-09 — absent ids are
 * simply absent; no `missing` field).
 */
export function buildEnvelope(
  currency: string,
  rows: Map<string, PriceRow>,
  requestedIds: string[],
  now: number,
  staleServe: boolean,
): PriceEnvelope {
  const prices: Record<string, number> = {};
  let maxFetchedAt = 0;
  let stale = false;
  for (const id of requestedIds) {
    const r = rows.get(id);
    if (!r) continue; // D-09: partial coverage — id absent from prices
    prices[id] = r.price;
    if (r.fetchedAt > maxFetchedAt) maxFetchedAt = r.fetchedAt;
    if (classifyAge(now - r.fetchedAt) !== "fresh") stale = true;
  }
  return {
    currency,
    prices,
    fetchedAt: maxFetchedAt,
    age: now - maxFetchedAt,
    source: staleServe ? "coingecko-cache" : "coingecko",
    stale,
  };
}
