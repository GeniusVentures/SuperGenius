// Plan 01-02 Task 2 — router contract (SRVC-01, SRVC-08, D-08).
// Since 01-03 the worker routes through the PriceCoordinator DO: happy-path
// and upstream-failure cases use SELF (full worker path incl. the DO); the
// 4xx table stays unit-style (those paths short-circuit before the DO).
import { SELF, createExecutionContext, waitOnExecutionContext, reset } from "cloudflare:test";
import worker, { type Env } from "../src/index";
import { http, HttpResponse } from "msw";
import { afterEach, describe, expect, it } from "vitest";
import { network } from "./server";

const BASE = "https://token.gnus.ai";
const UPSTREAM = "https://api.coingecko.com/api/v3/simple/price";

afterEach(async () => {
  await reset(); // storage reset suffices — no DO-memory assertions here
  network.resetHandlers();
});

async function call(path: string, method = "GET") {
  const ctx = createExecutionContext();
  const res = await worker.fetch(
    new Request(`${BASE}${path}`, { method }),
    {} as unknown as Env, // 4xx paths never touch the DO binding
    ctx,
  );
  await waitOnExecutionContext(ctx);
  return res;
}

describe("GET /v1/prices happy path (SRVC-01)", () => {
  it("returns the six-key envelope with source 'coingecko', stale false", async () => {
    network.use(http.get(UPSTREAM, () => HttpResponse.json({ bitcoin: { usd: 61234.12 } })));
    const res = await SELF.fetch(`${BASE}/v1/prices?ids=bitcoin&vs=usd`);
    expect(res.status).toBe(200);
    const body = (await res.json()) as Record<string, unknown>;
    expect(Object.keys(body).sort()).toEqual(
      ["age", "currency", "fetchedAt", "prices", "source", "stale"].sort(),
    );
    expect(body.currency).toBe("usd");
    expect(body.prices).toEqual({ bitcoin: 61234.12 });
    expect(body.source).toBe("coingecko");
    expect(body.stale).toBe(false);
    expect(body.age).toBeLessThanOrEqual(60);
  });

  it("partial upstream coverage omits ids without error (D-09)", async () => {
    network.use(http.get(UPSTREAM, () => HttpResponse.json({ bitcoin: { usd: 1 } })));
    const res = await SELF.fetch(`${BASE}/v1/prices?ids=bitcoin,unknown-token&vs=usd`);
    expect(res.status).toBe(200);
    const body = (await res.json()) as Record<string, unknown> & { prices: Record<string, number> };
    expect(Object.keys(body.prices)).toEqual(["bitcoin"]);
  });
});

describe("4xx table (SRVC-08, D-08) — never 500", () => {
  const cases: Array<[string, number, string]> = [
    ["/v1/prices?vs=usd", 400, "invalid_request"], // missing ids
    ["/v1/prices?ids=&vs=usd", 400, "invalid_request"], // empty ids
    [`/v1/prices?ids=${Array.from({ length: 51 }, (_, i) => `t${i}`).join(",")}&vs=usd`, 400, "invalid_request"], // 51 ids
    ["/v1/prices?ids=bitcoin&vs=usd1", 400, "invalid_request"], // bad vs
    ["/nope", 404, "not_found"],
  ];
  for (const [path, status, code] of cases) {
    it(`${path.length > 60 ? path.slice(0, 57) + "..." : path} → ${status} ${code}`, async () => {
      const res = await call(path);
      expect(res.status).toBe(status);
      expect(res.status).not.toBe(500);
      const body = (await res.json()) as { error: { code: string; message: string } };
      expect(body.error.code).toBe(code);
      expect(typeof body.error.message).toBe("string");
    });
  }

  it("POST /v1/prices → 405 method_not_allowed", async () => {
    const res = await call("/v1/prices?ids=bitcoin&vs=usd", "POST");
    expect(res.status).toBe(405);
    const body = (await res.json()) as { error: { code: string } };
    expect(body.error.code).toBe("method_not_allowed");
  });
});

describe("allowlist enforcement (SRVC-08) — default closed at the router", () => {
  // `call()` passes {} as Env, so the DEFAULT closed allowlist is what runs:
  // unset ALLOWED_IDS/ALLOWED_VS can never mean unrestricted.
  it("GET /v1/prices?ids=bitcoin&vs=usd → 400 invalid_request policy message", async () => {
    const res = await call("/v1/prices?ids=bitcoin&vs=usd");
    expect(res.status).toBe(400);
    const body = (await res.json()) as { error: { code: string; message: string } };
    expect(body.error.code).toBe("invalid_request");
    expect(body.error.message).toMatch(/not allowed on this endpoint/);
  });

  it("GET /v1/prices?ids=genius-ai&vs=eur → 400 (vs outside default)", async () => {
    const res = await call("/v1/prices?ids=genius-ai&vs=eur");
    expect(res.status).toBe(400);
    const body = (await res.json()) as { error: { code: string; message: string } };
    expect(body.error.code).toBe("invalid_request");
    expect(body.error.message).toMatch(/not allowed on this endpoint/);
  });

  it("format validation runs BEFORE the membership gate — malformed id keeps today's message", async () => {
    const res = await call("/v1/prices?ids=Bit_Coin!&vs=usd");
    expect(res.status).toBe(400);
    const body = (await res.json()) as { error: { code: string; message: string } };
    expect(body.error.code).toBe("invalid_request");
    expect(body.error.message).toMatch(/\[a-z0-9-\]\+/);
    expect(body.error.message).not.toMatch(/not allowed on this endpoint/);
  });

  it("production-default id passes end-to-end: genius-ai/usd → 200 PriceEnvelope", async () => {
    network.use(http.get(UPSTREAM, () => HttpResponse.json({ "genius-ai": { usd: 0.42 } })));
    const res = await SELF.fetch(`${BASE}/v1/prices?ids=genius-ai&vs=usd`);
    expect(res.status).toBe(200);
    const body = (await res.json()) as Record<string, unknown> & {
      prices: Record<string, number>;
      stale: boolean;
    };
    expect(body.prices).toEqual({ "genius-ai": 0.42 });
    expect(body.stale).toBe(false);
  });
});

describe("upstream failure → structured 502 (D-08)", () => {
  it("403-HTML upstream → 502 upstream_error with upstreamStatus, JSON body", async () => {
    network.use(
      http.get(UPSTREAM, () =>
        new HttpResponse("<html>Forbidden</html>", {
          status: 403,
          headers: { "content-type": "text/html" },
        }),
      ),
    );
    const res = await SELF.fetch(`${BASE}/v1/prices?ids=bitcoin&vs=usd`);
    expect(res.status).toBe(502);
    const body = (await res.json()) as { error: { code: string; upstreamStatus?: number } };
    expect(body.error.code).toBe("upstream_error");
    expect(body.error.upstreamStatus).toBe(403);
  });
});
