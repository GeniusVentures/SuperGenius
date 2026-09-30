// Plan 01-02 Task 2 — router contract (SRVC-01, SRVC-08, D-08) via unit-style
// worker.fetch with MSW-mocked upstream (interim direct-upstream wiring).
import { createExecutionContext, waitOnExecutionContext } from "cloudflare:test";
import worker, { type Env } from "../src/index";
import { http, HttpResponse } from "msw";
import { describe, expect, it } from "vitest";
import { network } from "./server";

const BASE = "https://token.gnus.ai";
const UPSTREAM = "https://api.coingecko.com/api/v3/simple/price";

async function call(path: string, envOverride?: Partial<Env>, method = "GET") {
  const ctx = createExecutionContext();
  const res = await worker.fetch(
    new Request(`${BASE}${path}`, { method }),
    { ...(envOverride ?? {}) } as unknown as Env,
    ctx,
  );
  await waitOnExecutionContext(ctx);
  return res;
}

function mockUpstream(prices: Record<string, Record<string, number>>) {
  return HttpResponse.json(prices);
}

describe("GET /v1/prices happy path (SRVC-01)", () => {
  it("returns the six-key envelope with source 'coingecko', stale false", async () => {
    network.use(http.get(UPSTREAM, () => mockUpstream({ bitcoin: { usd: 61234.12 } })));
    const res = await call("/v1/prices?ids=bitcoin&vs=usd");
    expect(res.status).toBe(200);
    const body = await res.json();
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
    network.use(http.get(UPSTREAM, () => mockUpstream({ bitcoin: { usd: 1 } })));
    const res = await call("/v1/prices?ids=bitcoin,unknown-token&vs=usd");
    expect(res.status).toBe(200);
    const body = await res.json();
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
    const res = await call("/v1/prices?ids=bitcoin&vs=usd", undefined, "POST");
    expect(res.status).toBe(405);
    const body = (await res.json()) as { error: { code: string } };
    expect(body.error.code).toBe("method_not_allowed");
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
    const res = await call("/v1/prices?ids=bitcoin&vs=usd");
    expect(res.status).toBe(502);
    const body = (await res.json()) as { error: { code: string; upstreamStatus?: number } };
    expect(body.error.code).toBe("upstream_error");
    expect(body.error.upstreamStatus).toBe(403);
  });
});
