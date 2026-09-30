// Plan 01-03 Task 1 — single-flight coalescing proofs (SRVC-02) via the
// deterministic call-count pattern (KF-4, Landmine 11): fire SELF.fetch
// un-awaited, advance fake time, assert the MSW closure counter.
// Tests go through SELF (full worker path). Router cutover is Task 2, so for
// Task 1 the tests exercise the DO directly when needed — see directStub().
import { SELF, env, reset, abortAllDurableObjects } from "cloudflare:test";
import { http, HttpResponse } from "msw";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { network } from "./server";
import { BATCH_WINDOW_MS, FRESH_SEC } from "../src/envelope";

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
  vi.useFakeTimers();
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
    const p1 = SELF.fetch(`${BASE}?ids=bitcoin,ethereum&vs=usd`);
    const p2 = SELF.fetch(`${BASE}?ids=bitcoin,solana&vs=usd`);
    await vi.advanceTimersByTimeAsync(BATCH_WINDOW_MS + 5);
    const [r1, r2] = await Promise.all([p1, p2]);
    expect(upstreamCalls).toBe(1); // THE assertion
    const b1 = (await r1.json()) as { prices: Record<string, number> };
    const b2 = (await r2.json()) as { prices: Record<string, number> };
    expect(Object.keys(b1.prices).sort()).toEqual(["bitcoin", "ethereum"]);
    expect(Object.keys(b2.prices).sort()).toEqual(["bitcoin", "solana"]);
  });

  it("three-way overlap with a superset request → still one call per window", async () => {
    primeOkHandler();
    const p1 = SELF.fetch(`${BASE}?ids=bitcoin&vs=usd`);
    const p2 = SELF.fetch(`${BASE}?ids=ethereum&vs=usd`);
    const p3 = SELF.fetch(`${BASE}?ids=bitcoin,ethereum,solana&vs=usd`);
    await vi.advanceTimersByTimeAsync(BATCH_WINDOW_MS + 5);
    const [r1, r2, r3] = await Promise.all([p1, p2, p3]);
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
    const p1 = SELF.fetch(`${BASE}?ids=bitcoin,ethereum&vs=usd`);
    await vi.advanceTimersByTimeAsync(BATCH_WINDOW_MS + 5);
    await p1;
    expect(upstreamCalls).toBe(1);

    // Advance WITHIN the fresh window (<60s). Request the SUBSET ethereum —
    // canonical key "ethereum" differs from "bitcoin,ethereum", so this stays
    // cache-missing even after 01-04's tier: it must traverse router → DO → SQL.
    await vi.advanceTimersByTimeAsync(FRESH_SEC - 10);
    const p2 = SELF.fetch(`${BASE}?ids=ethereum&vs=usd`);
    await vi.advanceTimersByTimeAsync(BATCH_WINDOW_MS + 5);
    const r2 = await p2;
    expect(upstreamCalls).toBe(1); // no refetch
    const b2 = (await r2.json()) as { source: string; stale: boolean; prices: Record<string, number> };
    expect(b2.source).toBe("coingecko"); // D-06a: fresh-from-SQL is "coingecko"
    expect(b2.stale).toBe(false);
    expect(Object.keys(b2.prices)).toEqual(["ethereum"]);
  });

  it("request after >60s → refetch happens (only expired ids travel upstream)", async () => {
    primeOkHandler();
    const p1 = SELF.fetch(`${BASE}?ids=bitcoin&vs=usd`);
    await vi.advanceTimersByTimeAsync(BATCH_WINDOW_MS + 5);
    await p1;
    expect(upstreamCalls).toBe(1);

    await vi.advanceTimersByTimeAsync(FRESH_SEC + 5);
    const p2 = SELF.fetch(`${BASE}?ids=bitcoin&vs=usd`);
    await vi.advanceTimersByTimeAsync(BATCH_WINDOW_MS + 5);
    await p2;
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
    const p1 = SELF.fetch(`${BASE}?ids=a,b&vs=usd`);
    await vi.advanceTimersByTimeAsync(BATCH_WINDOW_MS + 5);
    await p1;

    // Age both rows past fresh, then upstream returns ONLY a (new price).
    await vi.advanceTimersByTimeAsync(FRESH_SEC + 5);
    network.use(
      http.get(UPSTREAM, () => {
        upstreamCalls++;
        return HttpResponse.json({ a: { usd: 999 } });
      }),
    );
    const p2 = SELF.fetch(`${BASE}?ids=a,b&vs=usd`);
    await vi.advanceTimersByTimeAsync(BATCH_WINDOW_MS + 5);
    await p2;

    // Read b via a cache-missing SUBSET key inside the fresh window: b's row
    // must survive with its ORIGINAL price (222), not clobbered.
    await vi.advanceTimersByTimeAsync(5);
    const p3 = SELF.fetch(`${BASE}?ids=b&vs=usd`);
    await vi.advanceTimersByTimeAsync(BATCH_WINDOW_MS + 5);
    const r3 = await p3;
    const b3 = (await r3.json()) as { prices: Record<string, number>; stale: boolean };
    expect(b3.prices.b).toBe(222); // original — never clobbered (D-10)
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
    const p1 = SELF.fetch(`${BASE}?ids=bitcoin&vs=usd`);
    const p2 = SELF.fetch(`${BASE}?ids=bitcoin&vs=eur`);
    await vi.advanceTimersByTimeAsync(BATCH_WINDOW_MS + 5);
    await Promise.all([p1, p2]);
    expect(upstreamCalls).toBe(2); // separate DO instances → separate batches
    expect(seen.sort()).toEqual(["eur", "usd"]);
  });
});
