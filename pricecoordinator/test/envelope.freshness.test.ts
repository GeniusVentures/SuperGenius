// Plan 01-02 Task 1 — envelope freshness semantics (D-06/D-06a/D-09/D-11/D-12, FRESH-01).
// Pure-module tests: fake system time + seeded rows; constants imported from the
// module under test (no magic numbers). Boundaries asserted at exactly
// 59/60/61s and 299/300/301s.
import { describe, expect, it, vi, afterEach } from "vitest";
import {
  buildEnvelope,
  classifyAge,
  nowSec,
  parseSec,
  usableRows,
  FRESH_SEC,
  STALE_SEC,
  type PriceRow,
} from "../src/envelope";
import { parsePricesRequest } from "../src/validate";

afterEach(() => vi.useRealTimers());

function row(id: string, price: number, fetchedAt: number): PriceRow {
  return { id, price, fetchedAt };
}

const NOW = 1_790_719_234; // design-reference epoch seconds

describe("buildEnvelope band semantics", () => {
  it("all ids ≤60s → source 'coingecko', stale false (fresh-from-storage included, D-06a)", () => {
    const rows = new Map([
      ["bitcoin", row("bitcoin", 61234.12, NOW - 17)],
      ["ethereum", row("ethereum", 3421.77, NOW - 59)],
    ]);
    const env = buildEnvelope("usd", rows, ["bitcoin", "ethereum"], NOW, false);
    expect(env.source).toBe("coingecko");
    expect(env.stale).toBe(false);
    expect(env.fetchedAt).toBe(NOW - 17); // max(fetchedAt) — D-11
    expect(env.age).toBe(17); // now − max — D-11
  });

  it("boundary 59/60/61: stale flips exactly past FRESH_SEC", () => {
    for (const [age, stale] of [
      [59, false],
      [60, false], // ≤60s is fresh (D-12: "every requested id is ≤60s old")
      [61, true],
    ] as const) {
      const rows = new Map([["bitcoin", row("bitcoin", 1, NOW - age)]]);
      const env = buildEnvelope("usd", rows, ["bitcoin"], NOW, false);
      expect(env.stale).toBe(stale);
      expect(env.source).toBe(age <= FRESH_SEC ? "coingecko" : "coingecko");
      if (age > FRESH_SEC) expect(env.stale).toBe(true);
    }
  });

  it("ANY id >60s (others fresh) → stale true, source 'coingecko' on success path (D-06a 'only' clause)", () => {
    // Partial-refresh shape: requested {a,b}; a refetched fresh; b served from
    // storage at ~120s because CoinGecko omitted it (D-09). NOT a stale-serve.
    const rows = new Map([
      ["a", row("a", 1, NOW - 5)],
      ["b", row("b", 2, NOW - 120)],
    ]);
    const env = buildEnvelope("usd", rows, ["a", "b"], NOW, false);
    expect(env.stale).toBe(true);
    expect(env.source).toBe("coingecko"); // never "coingecko-cache" merely for band membership
  });

  it("stale-serve flag → source 'coingecko-cache' (D-06/D-07)", () => {
    const rows = new Map([["bitcoin", row("bitcoin", 1, NOW - 94)]]);
    const env = buildEnvelope("usd", rows, ["bitcoin"], NOW, true);
    expect(env.source).toBe("coingecko-cache");
    expect(env.stale).toBe(true);
  });

  it("fetchedAt = max(fetchedAt of returned ids); age = now − max (D-11)", () => {
    const rows = new Map([
      ["a", row("a", 1, NOW - 300)],
      ["b", row("b", 2, NOW - 10)],
      ["c", row("c", 3, NOW - 100)],
    ]);
    const env = buildEnvelope("usd", rows, ["a", "b", "c"], NOW, false);
    expect(env.fetchedAt).toBe(NOW - 10);
    expect(env.age).toBe(10);
  });

  it("partial rows (requested {a,b}, rows {a}) → prices has only a; no missing key (D-09)", () => {
    const rows = new Map([["a", row("a", 1, NOW - 5)]]);
    const env = buildEnvelope("usd", rows, ["a", "b"], NOW, false);
    expect(Object.keys(env.prices)).toEqual(["a"]);
    expect(env).not.toHaveProperty("missing");
    expect(JSON.stringify(env)).not.toContain("missing");
  });

  it("prices preserve the design-reference numeric shape exactly", () => {
    const rows = new Map([["bitcoin", row("bitcoin", 61234.12, NOW - 17)]]);
    const env = buildEnvelope("usd", rows, ["bitcoin"], NOW, false);
    expect(env).toEqual({
      currency: "usd",
      prices: { bitcoin: 61234.12 },
      fetchedAt: NOW - 17,
      age: 17,
      source: "coingecko",
      stale: false,
    });
  });
});

describe("classifyAge / usableRows boundaries (D-12)", () => {
  it("classifyAge at 59/60/61 and 299/300/301", () => {
    expect(classifyAge(59)).toBe("fresh");
    expect(classifyAge(60)).toBe("fresh");
    expect(classifyAge(61)).toBe("stale");
    expect(classifyAge(299)).toBe("stale");
    expect(classifyAge(300)).toBe("stale");
    expect(classifyAge(301)).toBe("unavailable");
  });

  it("usableRows drops >5min rows, keeps 60s–5min and fresh rows", () => {
    const rows = new Map([
      ["fresh", row("fresh", 1, NOW - 30)],
      ["stale", row("stale", 2, NOW - 120)],
      ["gone", row("gone", 3, NOW - STALE_SEC - 1)],
    ]);
    const usable = usableRows(rows, NOW);
    expect(usable.has("fresh")).toBe(true);
    expect(usable.has("stale")).toBe(true);
    expect(usable.has("gone")).toBe(false);
  });
});

describe("nowSec / parseSec (Landmine 10 — seconds, never ms)", () => {
  it("nowSec divides Date.now() by 1000", () => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date("2026-09-30T00:00:00Z"));
    expect(nowSec()).toBe(Math.floor(Date.parse("2026-09-30T00:00:00Z") / 1000));
  });

  it("parseSec floors fractional seconds", () => {
    expect(parseSec(1790719234.9)).toBe(1790719234);
  });
});

describe("parsePricesRequest (SRVC-08 allowlist)", () => {
  const url = (qs: string) => new URL(`https://token.gnus.ai/v1/prices${qs}`);

  it("accepts ids=bitcoin,ethereum + vs=usd", () => {
    const r = parsePricesRequest(url("?ids=bitcoin,ethereum&vs=usd"));
    expect(r.ok).toBe(true);
    if (r.ok) {
      expect(r.ids).toEqual(["bitcoin", "ethereum"]);
      expect(r.currency).toBe("usd");
    }
  });

  it("dedupes ids=bitcoin,bitcoin", () => {
    const r = parsePricesRequest(url("?ids=bitcoin,bitcoin&vs=usd"));
    expect(r.ok).toBe(true);
    if (r.ok) expect(r.ids).toEqual(["bitcoin"]);
  });

  const bad: Array<[string, string]> = [
    ["missing ids", ""],
    ["empty ids", "?ids=&vs=usd"],
    ["invalid id chars", "?ids=Bit_Coin!&vs=usd"],
    ["51 ids", `?ids=${Array.from({ length: 51 }, (_, i) => `tok${i}`).join(",")}&vs=usd`],
    ["invalid vs", "?ids=bitcoin&vs=usd1"],
    ["missing vs", "?ids=bitcoin"],
  ];
  for (const [name, qs] of bad) {
    it(`rejects ${name} with 400 invalid_request`, () => {
      const r = parsePricesRequest(url(qs));
      expect(r.ok).toBe(false);
      if (!r.ok) {
        expect(r.status).toBe(400);
        expect(r.code).toBe("invalid_request");
        expect(typeof r.message).toBe("string");
      }
    });
  }
});
