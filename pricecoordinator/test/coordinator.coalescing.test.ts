// Plan 01-03 Task 1 — single-flight coalescing proofs (SRVC-02).
//
// Determinism strategy (empirically established this session — see plan
// summary): vitest fake timers CANNOT fake setTimeout across the test→DO
// isolate boundary (faking timers breaks workerd RPC; the batch timer would
// never fire). Date-ONLY faking DOES propagate to the DO isolate's clock.
// Therefore: batch windows flush on REAL 15ms timers (await the fetches
// directly — the 15ms delay is real but bounded and not a sleep-based
// assertion), and time bands are driven by vi.setSystemTime (Date-only).
// The coalescing proof remains the MSW closure call count — not timing.
import { SELF, reset, abortAllDurableObjects } from "cloudflare:test";
import { http, HttpResponse } from "msw";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { network } from "./server";
import { FRESH_SEC } from "../src/envelope";

const BASE = "https://token.gnus.ai/v1/prices";
const UPSTREAM = "https://api.coingecko.com/api/v3/simple/price";

let upstreamCalls = 0;

function primeOkHandler() {
  network.use(
    http.get(UPSTREAM, ({ request }) => {
      upstreamCalls++;
      const url = new URL(request.url);
      const ids = (url.searchParams.get("ids") ?? "").split(",");
      const vs = url.searchParams.get("vs_currencies") ?? "usd";
      const body: Record<string, Record<string, number>> = {};
      for (const id of ids) body[id] = { [vs]: 100 + ids.indexOf(id) };
      return HttpResponse.json(body);
    }),
  );
}

beforeEach(() => {
  upstreamCalls = 0;
  // Date-only: moves the DO's clock for band tests without breaking the
  // real 15ms batch-flush timers (empirically verified this session).
  vi.useFakeTimers({ toFake: ["Date"] });
});

afterEach(async () => {
  // KF-8 / Landmine 16: real timers FIRST, then storage/DO teardown, then MSW.
  vi.useRealTimers();
  await reset();
  await abortAllDurableObjects();
  network.resetHandlers();
});

describe("single-flight coalescing (SRVC-02)", () => {
  it("two concurrent overlapping id-sets → exactly ONE upstream call, each caller its subset", async () => {
    primeOkHandler();
    const [r1, r2] = await Promise.all([
      SELF.fetch(`${BASE}?ids=bitcoin,ethereum&vs=usd`),
      SELF.fetch(`${BASE}?ids=bitcoin,solana&vs=usd`),
    ]);
    expect(upstreamCalls).toBe(1); // THE assertion
    const b1 = (await r1.json()) as { prices: Record<string, number> };
    const b2 = (await r2.json()) as { prices: Record<string, number> };
    expect(Object.keys(b1.prices).sort()).toEqual(["bitcoin", "ethereum"]);
    expect(Object.keys(b2.prices).sort()).toEqual(["bitcoin", "solana"]);
  });

  it("three-way overlap with a superset request → still one call per window", async () => {
    primeOkHandler();
    const [r1, r2, r3] = await Promise.all([
      SELF.fetch(`${BASE}?ids=bitcoin&vs=usd`),
      SELF.fetch(`${BASE}?ids=ethereum&vs=usd`),
      SELF.fetch(`${BASE}?ids=bitcoin,ethereum,solana&vs=usd`),
    ]);
    expect(upstreamCalls).toBe(1);
    const b1 = (await r1.json()) as { prices: Record<string, number> };
    const b2 = (await r2.json()) as { prices: Record<string, number> };
    const b3 = (await r3.json()) as { prices: Record<string, number> };
    expect(Object.keys(b1.prices)).toEqual(["bitcoin"]);
    expect(Object.keys(b2.prices)).toEqual(["ethereum"]);
    expect(Object.keys(b3.prices).sort()).toEqual(["bitcoin", "ethereum", "solana"]);
  });
});

describe("freshness gates (D-12, D-06a fresh-from-SQL)", () => {
  it("subset request inside the fresh window → served from SQL, ZERO new upstream calls", async () => {
    primeOkHandler();
    // Prime {bitcoin, ethereum} — canonical key "bitcoin,ethereum".
    const r1 = await SELF.fetch(`${BASE}?ids=bitcoin,ethereum&vs=usd`);
    expect(r1.status).toBe(200);
    expect(upstreamCalls).toBe(1);

    // Shift the clock WITHIN the fresh window (<60s). Request the SUBSET
    // ethereum — canonical key "ethereum" differs from "bitcoin,ethereum", so
    // this stays cache-missing even after 01-04's tier: it must traverse
    // router → DO → SQL.
    vi.setSystemTime(new Date(Date.now() + (FRESH_SEC - 10) * 1000));
    const r2 = await SELF.fetch(`${BASE}?ids=ethereum&vs=usd`);
    expect(upstreamCalls).toBe(1); // no refetch
    const b2 = (await r2.json()) as { source: string; stale: boolean; prices: Record<string, number> };
    expect(b2.source).toBe("coingecko"); // D-06a: fresh-from-SQL is "coingecko"
    expect(b2.stale).toBe(false);
    expect(Object.keys(b2.prices)).toEqual(["ethereum"]);
  });

  it("request after >60s → refetch happens (only expired ids travel upstream)", async () => {
    primeOkHandler();
    const r1 = await SELF.fetch(`${BASE}?ids=bitcoin&vs=usd`);
    expect(r1.status).toBe(200);
    expect(upstreamCalls).toBe(1);

    vi.setSystemTime(new Date(Date.now() + (FRESH_SEC + 5) * 1000));
    const r2 = await SELF.fetch(`${BASE}?ids=bitcoin&vs=usd`);
    expect(r2.status).toBe(200);
    expect(upstreamCalls).toBe(2); // refetched
  });
});

describe("partial-response non-clobbering (D-10)", () => {
  it("upstream returning only {a} leaves row b's price/fetchedAt untouched", async () => {
    // First call returns both.
    network.use(
      http.get(UPSTREAM, ({ request }) => {
        upstreamCalls++;
        const url = new URL(request.url);
        const ids = (url.searchParams.get("ids") ?? "").split(",");
        const vs = url.searchParams.get("vs_currencies") ?? "usd";
        const body: Record<string, Record<string, number>> = {};
        for (const id of ids) body[id] = { [vs]: ids.indexOf(id) === 0 ? 111 : 222 };
        return HttpResponse.json(body);
      }),
    );
    await SELF.fetch(`${BASE}?ids=a,b&vs=usd`);

    // Age both rows past fresh, then upstream returns ONLY a (new price).
    vi.setSystemTime(new Date(Date.now() + (FRESH_SEC + 5) * 1000));
    network.use(
      http.get(UPSTREAM, () => {
        upstreamCalls++;
        return HttpResponse.json({ a: { usd: 999 } });
      }),
    );
    await SELF.fetch(`${BASE}?ids=a,b&vs=usd`);

    // Read b via a cache-missing SUBSET key: b's row must survive with its
    // ORIGINAL price (222), never clobbered (D-10).
    const r3 = await SELF.fetch(`${BASE}?ids=b&vs=usd`);
    const b3 = (await r3.json()) as { prices: Record<string, number>; stale: boolean };
    expect(b3.prices.b).toBe(222); // original — never clobbered
    expect(b3.stale).toBe(true); // b is 60s–5min old on this success path
  });
});

describe("per-currency DO isolation (D-05 routing axis)", () => {
  it("usd and eur requests produce distinct vs_currencies upstream params", async () => {
    const seen: string[] = [];
    network.use(
      http.get(UPSTREAM, ({ request }) => {
        upstreamCalls++;
        const url = new URL(request.url);
        const vs = url.searchParams.get("vs_currencies") ?? "usd";
        seen.push(vs);
        const ids = (url.searchParams.get("ids") ?? "").split(",");
        const body: Record<string, Record<string, number>> = {};
        for (const id of ids) body[id] = { [vs]: 1 };
        return HttpResponse.json(body);
      }),
    );
    await Promise.all([
      SELF.fetch(`${BASE}?ids=bitcoin&vs=usd`),
      SELF.fetch(`${BASE}?ids=bitcoin&vs=eur`),
    ]);
    expect(upstreamCalls).toBe(2); // separate DO instances → separate batches
    expect(seen.sort()).toEqual(["eur", "usd"]);
  });
});

