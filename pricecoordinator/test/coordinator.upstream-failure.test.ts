// Plan 01-03 Task 2 — upstream-failure matrix (SRVC-05, D-07, D-08, D-12) and
// hold-off (Landmine 9). Seeding: a successful mocked fetch populates the DO;
// handlers are then swapped via network.use (afterEach resets). Time bands are
// driven by Date-only fake timers (empirical finding — see plan summary); the
// forced-failure requests traverse real 15ms batch flushes.
import { SELF, reset, abortAllDurableObjects } from "cloudflare:test";
import { http, HttpResponse } from "msw";
import { afterEach, describe, expect, it, vi } from "vitest";
import { network } from "./server";
import { FRESH_SEC } from "../src/envelope";

const BASE = "https://token.gnus.ai/v1/prices";
const UPSTREAM = "https://api.coingecko.com/api/v3/simple/price";

let upstreamCalls = 0;

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

function failHandler(status: number, body?: string) {
  return http.get(UPSTREAM, () => {
    upstreamCalls++;
    if (body !== undefined) {
      return new HttpResponse(body, {
        status,
        headers: { "content-type": "text/html" },
      });
    }
    return HttpResponse.json({}, { status });
  });
}

/** Seed fresh rows via a successful fetch, then age them past the fresh band. */
async function seedAndAge(ids: string, ageSec: number) {
  upstreamCalls = 0;
  network.use(okHandler({ bitcoin: { usd: 50000 }, ethereum: { usd: 3000 } }));
  const r = await SELF.fetch(`${BASE}?ids=${ids}&vs=usd`);
  expect(r.status).toBe(200);
  vi.setSystemTime(new Date(Date.now() + ageSec * 1000));
}

describe("upstream failure with usable rows → stale-serve (D-07)", () => {
  it("429 with 61s-old rows → 200, stale true, source coingecko-cache", async () => {
    vi.useFakeTimers({ toFake: ["Date"] });
    await seedAndAge("bitcoin", FRESH_SEC + 1);
    network.use(failHandler(429));
    const res = await SELF.fetch(`${BASE}?ids=bitcoin&vs=usd`);
    expect(res.status).toBe(200);
    const body = (await res.json()) as { stale: boolean; source: string; prices: Record<string, number> };
    expect(body.stale).toBe(true);
    expect(body.source).toBe("coingecko-cache");
    expect(body.prices.bitcoin).toBe(50000);
  });

  it("500 with usable rows → identical stale-serve outcome", async () => {
    vi.useFakeTimers({ toFake: ["Date"] });
    await seedAndAge("bitcoin", FRESH_SEC + 1);
    network.use(failHandler(500));
    const res = await SELF.fetch(`${BASE}?ids=bitcoin&vs=usd`);
    expect(res.status).toBe(200);
    const body = (await res.json()) as { stale: boolean; source: string };
    expect(body.stale).toBe(true);
    expect(body.source).toBe("coingecko-cache");
  });

  it("403-HTML with usable rows → stale-serve; body never parsed, no 500", async () => {
    vi.useFakeTimers({ toFake: ["Date"] });
    await seedAndAge("bitcoin", FRESH_SEC + 1);
    network.use(failHandler(403, "<html>Forbidden</html>"));
    const res = await SELF.fetch(`${BASE}?ids=bitcoin&vs=usd`);
    expect(res.status).toBe(200);
    const body = (await res.json()) as { source: string };
    expect(body.source).toBe("coingecko-cache");
  });
});

describe("upstream failure with nothing usable → structured 502 (D-08)", () => {
  it("429 with EMPTY DO storage → 502 upstream_error with upstreamStatus 429", async () => {
    vi.useFakeTimers({ toFake: ["Date"] });
    upstreamCalls = 0;
    network.use(failHandler(429));
    const res = await SELF.fetch(`${BASE}?ids=cold-token&vs=usd`);
    expect(res.status).toBe(502);
    const body = (await res.json()) as { error: { code: string; upstreamStatus?: number } };
    expect(body.error.code).toBe("upstream_error");
    expect(body.error.upstreamStatus).toBe(429);
  });

  it("403-HTML with empty storage → 502 with upstreamStatus 403, JSON body", async () => {
    vi.useFakeTimers({ toFake: ["Date"] });
    upstreamCalls = 0;
    network.use(failHandler(403, "<html>WAF block</html>"));
    const res = await SELF.fetch(`${BASE}?ids=cold-token2&vs=usd`);
    expect(res.status).toBe(502);
    const body = (await res.json()) as { error: { code: string; upstreamStatus?: number } };
    expect(body.error.code).toBe("upstream_error");
    expect(body.error.upstreamStatus).toBe(403);
  });
});

describe(">5min rows are never served (D-12 unavailable band)", () => {
  it("301s+ old rows + upstream failure → 502, not stale-serve", async () => {
    vi.useFakeTimers({ toFake: ["Date"] });
    await seedAndAge("bitcoin", 301);
    network.use(failHandler(429));
    const res = await SELF.fetch(`${BASE}?ids=bitcoin&vs=usd`);
    expect(res.status).toBe(502);
    const body = (await res.json()) as { error: { code: string } };
    expect(body.error.code).toBe("upstream_error");
  });
});

describe("hold-off after 429 (Landmine 9)", () => {
  it("second request during the 60s hold-off makes ZERO upstream calls and serves stale", async () => {
    vi.useFakeTimers({ toFake: ["Date"] });
    await seedAndAge("bitcoin", FRESH_SEC + 1);
    // First failing request: ages rows into stale band, triggers 429 + hold-off.
    network.use(failHandler(429));
    const r1 = await SELF.fetch(`${BASE}?ids=bitcoin&vs=usd`);
    expect(r1.status).toBe(200); // stale-serve
    const callsAfterFirst = upstreamCalls;

    // Second request DURING hold-off (only 1s later): must not call upstream.
    vi.setSystemTime(new Date(Date.now() + 1000));
    const r2 = await SELF.fetch(`${BASE}?ids=bitcoin&vs=usd`);
    expect(upstreamCalls).toBe(callsAfterFirst); // ZERO new upstream calls
    expect(r2.status).toBe(200);
    const body = (await r2.json()) as { stale: boolean; source: string };
    expect(body.stale).toBe(true);
    expect(body.source).toBe("coingecko-cache");
  });
});
