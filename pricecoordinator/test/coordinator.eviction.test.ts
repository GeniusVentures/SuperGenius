// Plan 01-03 Task 2 — eviction persistence (SRVC-03, phase criterion 5, KF-7).
//
// DEViation from KF-7's primary API: evictDurableObject() hangs indefinitely
// on @cloudflare/vitest-plugin 1.2.4 (the downgraded pin — see 01-01
// deviations), verified empirically this session. abortAllDurableObjects()
// is the documented forcible counterpart: discards in-memory state while
// PRESERVING persisted storage — which is exactly the property criterion 5
// tests (SQL rows survive instance teardown). The same proof lands; only the
// teardown primitive differs. Verification requests use SUBSETS of the primed
// id-set (different canonical cache keys) so they stay cache-missing once
// 01-04's tier lands — they must traverse router → cold DO → SQL.
import { SELF, abortAllDurableObjects, reset } from "cloudflare:test";
import { http, HttpResponse } from "msw";
import { afterEach, describe, expect, it, vi } from "vitest";
import { network } from "./server";
import { FRESH_SEC } from "../src/envelope";

const BASE = "https://token.gnus.ai/v1/prices";
const UPSTREAM = "https://api.coingecko.com/api/v3/simple/price";

let upstreamCalls = 0;

// NOTE: abortAllDurableObjects is also used by afterEach — but here it is the
// assertion-bearing teardown (state torn down BETWEEN requests, mid-test).
const doTeardown = () => abortAllDurableObjects();

afterEach(async () => {
  vi.useRealTimers();
  await reset();
  await abortAllDurableObjects();
  network.resetHandlers();
});

function okHandler(prices: Record<string, Record<string, number>>) {
  return http.get(UPSTREAM, () => {
    upstreamCalls++;
    return HttpResponse.json(prices);
  });
}

describe("DO state survives eviction (SRVC-03, criterion 5)", () => {
  it("post-eviction subset request within freshness serves from SQL, no refetch", async () => {
    upstreamCalls = 0;
    network.use(okHandler({ bitcoin: { usd: 61234.12 }, ethereum: { usd: 3421.77 } }));

    // Prime {bitcoin, ethereum} — canonical key "bitcoin,ethereum".
    const r1 = await SELF.fetch(`${BASE}?ids=bitcoin,ethereum&vs=usd`);
    expect(r1.status).toBe(200);
    expect(upstreamCalls).toBe(1);

    // Forcible teardown under REAL timers: in-memory state discarded,
    // durable storage preserved (see file header for the 1.2.4 deviation).
    await doTeardown();

    // Within the freshness window, via the SUBSET bitcoin — different
    // canonical key from the priming request → cache-missing by construction
    // even after 01-04's tier: router → cold DO → SQL.
    vi.useFakeTimers({ toFake: ["Date"] });
    vi.setSystemTime(new Date(Date.now() + (FRESH_SEC - 10) * 1000));
    const r2 = await SELF.fetch(`${BASE}?ids=bitcoin&vs=usd`);
    expect(r2.status).toBe(200);
    const body = (await r2.json()) as {
      prices: Record<string, number>;
      source: string;
      stale: boolean;
    };
    expect(body.prices.bitcoin).toBe(61234.12); // survived — from SQL
    expect(upstreamCalls).toBe(1); // no refetch needed
    expect(body.source).toBe("coingecko");
    expect(body.stale).toBe(false);
  });

  it("post-eviction refetch path works from a cold instance (SQL reload, not memory)", async () => {
    upstreamCalls = 0;
    network.use(okHandler({ bitcoin: { usd: 100 }, ethereum: { usd: 200 } }));

    await SELF.fetch(`${BASE}?ids=bitcoin,ethereum&vs=usd`);
    expect(upstreamCalls).toBe(1);

    await doTeardown();

    // Past the freshness window (45s cache TTL long gone): cold DO refetches.
    vi.useFakeTimers({ toFake: ["Date"] });
    vi.setSystemTime(new Date(Date.now() + (FRESH_SEC + 5) * 1000));
    const r2 = await SELF.fetch(`${BASE}?ids=ethereum&vs=usd`);
    expect(r2.status).toBe(200);
    const body = (await r2.json()) as { prices: Record<string, number> };
    expect(body.prices.ethereum).toBe(200);
    expect(upstreamCalls).toBe(2); // refetched from the cold instance
  });
});
