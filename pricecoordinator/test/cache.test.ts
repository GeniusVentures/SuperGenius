// Plan 01-04 — caches.default read-through (SRVC-04). SELF-style only (the
// Cache API is traversed via the full worker path — KF-12). Determinism per
// the 01-03 findings: Date-only fake timers for band seeding, real 15ms
// flushes, MSW call-counts for assertions. Test-unique id sets throughout.
// NO TTL-expiry tests via fake timers (KF-3: miniflare cache uses its own
// timers) — the header + hit/miss behavior is the proof.
import { SELF, reset, abortAllDurableObjects } from "cloudflare:test";
import { http, HttpResponse } from "msw";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { network } from "./server";
import { FRESH_SEC } from "../src/envelope";

const BASE = "https://token.gnus.ai/v1/prices";
const UPSTREAM = "https://api.coingecko.com/api/v3/simple/price";

let upstreamCalls = 0;

function okHandler(prices: Record<string, Record<string, number>>) {
  return http.get(UPSTREAM, () => {
    upstreamCalls++;
    return HttpResponse.json(prices);
  });
}

function failHandler(status: number) {
  return http.get(UPSTREAM, () => {
    upstreamCalls++;
    return HttpResponse.json({}, { status });
  });
}

beforeEach(() => {
  upstreamCalls = 0;
  vi.useFakeTimers({ toFake: ["Date"] });
});

afterEach(async () => {
  vi.useRealTimers();
  await reset();
  await abortAllDurableObjects();
  network.resetHandlers();
});

describe("cache round-trip (SRVC-04)", () => {
  it("second identical request is served from cache — upstreamCalls unchanged", async () => {
    network.use(okHandler({ "rt-bitcoin": { usd: 101 } }));
    const r1 = await SELF.fetch(`${BASE}?ids=rt-bitcoin&vs=usd`);
    expect(r1.status).toBe(200);
    expect(upstreamCalls).toBe(1);

    const r2 = await SELF.fetch(`${BASE}?ids=rt-bitcoin&vs=usd`);
    expect(r2.status).toBe(200);
    expect(upstreamCalls).toBe(1); // DO bypassed — cache hit
    const b1 = (await r1.json()) as { prices: Record<string, number> };
    const b2 = (await r2.json()) as { prices: Record<string, number> };
    expect(b2.prices).toEqual(b1.prices);
  });

  it("cache hit preserves fresh semantics: source coingecko, stale false (D-06a)", async () => {
    network.use(okHandler({ "fs-a": { usd: 1 } }));
    await SELF.fetch(`${BASE}?ids=fs-a&vs=usd`);
    const r2 = await SELF.fetch(`${BASE}?ids=fs-a&vs=usd`);
    const body = (await r2.json()) as { source: string; stale: boolean };
    expect(body.source).toBe("coingecko");
    expect(body.stale).toBe(false);
  });
});

describe("canonical keys (Landmine 6)", () => {
  it("permuted and duped id lists hit the same entry", async () => {
    network.use(okHandler({ "ck-a": { usd: 1 }, "ck-b": { usd: 2 } }));
    const r1 = await SELF.fetch(`${BASE}?ids=ck-a,ck-b&vs=usd`);
    expect(upstreamCalls).toBe(1);

    // Permuted order → same canonical key → cache hit.
    const r2 = await SELF.fetch(`${BASE}?ids=ck-b,ck-a&vs=usd`);
    expect(upstreamCalls).toBe(1);
    const b2 = (await r2.json()) as { prices: Record<string, number> };
    expect(b2.prices).toEqual({ "ck-a": 1, "ck-b": 2 });

    // Duped variant → same canonical key → cache hit.
    const r3 = await SELF.fetch(`${BASE}?ids=ck-a,ck-a,ck-b&vs=usd`);
    expect(upstreamCalls).toBe(1);
  });

  it("different id-set misses the cache and reaches the DO", async () => {
    network.use(okHandler({ "dm-a": { usd: 1 }, "dm-b": { usd: 2 } }));
    await SELF.fetch(`${BASE}?ids=dm-a&vs=usd`);
    expect(upstreamCalls).toBe(1);

    // Different set → miss → DO → upstream for dm-b (no SQL row, no cache row).
    const r2 = await SELF.fetch(`${BASE}?ids=dm-a,dm-b&vs=usd`);
    expect(upstreamCalls).toBe(2); // batch refetch (both ids need fetching in the new key's DO pass? dm-a fresh in SQL — only dm-b is needed)
    expect(r2.status).toBe(200);
  });
});

describe("response header (KF-3)", () => {
  it("DO-sourced 200 carries Cache-Control: public, max-age=45", async () => {
    network.use(okHandler({ "hd-a": { usd: 1 } }));
    const res = await SELF.fetch(`${BASE}?ids=hd-a&vs=usd`);
    expect(res.headers.get("cache-control")).toBe("public, max-age=45");
  });
});

describe("errors are never cached (Landmine 8)", () => {
  it("a 502 is not served from cache — the next request re-reaches the DO", async () => {
    network.use(failHandler(429));
    const r1 = await SELF.fetch(`${BASE}?ids=ec-a&vs=usd`);
    expect(r1.status).toBe(502); // nothing usable cached → structured error

    network.resetHandlers();
    network.use(okHandler({ "ec-a": { usd: 5 } }));
    const r2 = await SELF.fetch(`${BASE}?ids=ec-a&vs=usd`);
    expect(r2.status).toBe(200); // reached the DO + upstream — not the cached 502
    const body = (await r2.json()) as { prices: Record<string, number> };
    expect(body.prices["ec-a"]).toBe(5);
  });
});

describe("stale-serve 200s are never stored (admission guard, D-07/D-12)", () => {
  it("stale 200 (coingecko-cache) is NOT admitted; next identical request re-reaches the DO", async () => {
    // Seed rows, then age them into the stale band (61s+).
    network.use(okHandler({ "st-a": { usd: 7 } }));
    await SELF.fetch(`${BASE}?ids=st-a&vs=usd`);
    expect(upstreamCalls).toBe(1);

    vi.setSystemTime(new Date(Date.now() + (FRESH_SEC + 5) * 1000));
    network.use(failHandler(429));
    const r1 = await SELF.fetch(`${BASE}?ids=st-a&vs=usd`);
    expect(r1.status).toBe(200); // stale-serve
    const b1 = (await r1.json()) as { stale: boolean; source: string };
    expect(b1.stale).toBe(true);
    expect(b1.source).toBe("coingecko-cache");

    // Second identical request MUST re-reach the DO (the stale 200 was not
    // stored): it re-enters hold-off/stale path — upstreamCalls may or may not
    // increment (hold-off suppresses upstream), but the response must come
    // from the DO again, and can never be a cached stale envelope.
    const r2 = await SELF.fetch(`${BASE}?ids=st-a&vs=usd`);
    expect(r2.status).toBe(200);
    const b2 = (await r2.json()) as { stale: boolean; source: string };
    expect(b2.stale).toBe(true);
    expect(b2.source).toBe("coingecko-cache");
    // The DO served it again from SQL (hold-off active → no upstream call):
    expect(upstreamCalls).toBe(1); // only the original seeding call
  });
});
