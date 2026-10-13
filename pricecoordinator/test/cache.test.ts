// Plan 01-04 — caches.default read-through (SRVC-04). SELF-style only (the
// Cache API is traversed via the full worker path — KF-12). Determinism per
// the 01-03 findings: Date-only fake timers for band seeding, real 15ms
// flushes, MSW call-counts for assertions. Test-unique id sets throughout.
// NO TTL-expiry tests via fake timers (KF-3: miniflare cache uses its own
// timers) — the header + hit/miss behavior is the proof.
import { SELF, reset } from "cloudflare:test";
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
  network.resetHandlers();
  // NOTE (plugin 1.2.4): abortAllDurableObjects() here races miniflare's
  // internal cache-entry DOs and emits uncaught-exception noise (exit != 0
  // despite green assertions) — REMOVED. Isolation is preserved without it:
  // reset() wipes DO storage per-test, and every test uses unique ids, so no
  // test can observe another's in-memory DO state (hold-off, batch window).
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
    // Ids are SINGLE-USE per test file run: the mock returns only requested
    // ids, so a fresh id guarantees a needed-row gap the DO must fill.
    network.use(
      http.get(UPSTREAM, ({ request }) => {
        upstreamCalls++;
        const ids = (new URL(request.url).searchParams.get("ids") ?? "").split(",");
        const body: Record<string, Record<string, number>> = {};
        for (const id of ids) body[id] = { usd: ids.indexOf(id) + 1 };
        return HttpResponse.json(body);
      }),
    );
    await SELF.fetch(`${BASE}?ids=dm2-a&vs=usd`);
    expect(upstreamCalls).toBe(1);

    // Different set → different canonical key → miss → DO: dm2-a fresh in
    // SQL, dm2-b new → one upstream call for dm2-b.
    const r2 = await SELF.fetch(`${BASE}?ids=dm2-a,dm2-b&vs=usd`);
    expect(upstreamCalls).toBe(2); // the miss reached the DO → upstream
    expect(r2.status).toBe(200);
    const b2 = (await r2.json()) as { prices: Record<string, number> };
    expect(Object.keys(b2.prices).sort()).toEqual(["dm2-a", "dm2-b"]);
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
    // COLD start under 429: nothing cached, nothing usable → 502.
    network.use(failHandler(429));
    const r1 = await SELF.fetch(`${BASE}?ids=ec-a&vs=usd`);
    expect(r1.status).toBe(502);

    // Advance past the cache TTL AND the fresh window so the retry is
    // cache-missing and needs a real refetch; hold-off must also have expired
    // (60s) for the upstream call to happen.
    vi.setSystemTime(new Date(Date.now() + (FRESH_SEC + 5) * 1000));
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
    // Unique-key request (st-b added): cache-miss → DO → 429 → stale-serve.
    const r1 = await SELF.fetch(`${BASE}?ids=st-a,st-b&vs=usd`);
    expect(r1.status).toBe(200); // stale-serve
    const b1 = (await r1.json()) as { stale: boolean; source: string; age: number };
    expect(b1.stale).toBe(true);
    expect(b1.source).toBe("coingecko-cache");

    // Second identical request: the stale 200 was NOT stored, so this request
    // re-reaches the DO (never a cached stale envelope). With hold-off active
    // the DO re-serves SQL rows — still the DO path, zero upstream calls.
    // THE PROOF of non-admission: if the stale 200 HAD been cached, this
    // response would carry identical fetchedAt AND `age` frozen at the cached
    // value while the clock keeps moving. Assert the envelope was rebuilt
    // from a live DO read: age reflects the CURRENT system time offset.
    vi.setSystemTime(new Date(Date.now() + 10 * 1000)); // +10s more
    const r2 = await SELF.fetch(`${BASE}?ids=st-a,st-b&vs=usd`);
    expect(r2.status).toBe(200);
    const b2 = (await r2.json()) as { stale: boolean; source: string; fetchedAt: number; age: number };
    expect(b2.stale).toBe(true);
    expect(b2.source).toBe("coingecko-cache");
    // Live DO read: age grew by (at least) the additional +10s — a cached
    // copy would replay b1's age exactly. (>= because the flush window adds
    // sub-second skew; identity is the falsifier, not the exact delta.)
    expect(b2.age).toBeGreaterThan(b1.age);
    // Seed(1) + r1's upstream attempt for the new st-b id under 429(1) = 2;
    // the hold-off then guarantees r2 makes no further upstream calls.
    expect(upstreamCalls).toBe(2);
  });
});
