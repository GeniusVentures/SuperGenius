// Plan 01-02 Task 2 — server-side-only key binding (SRVC-06, D-04/D-05, KF-9).
// Keyed path via unit-style env override; anonymous path is the default.
import { createExecutionContext, waitOnExecutionContext } from "cloudflare:test";
import worker, { type Env } from "../src/index";
import { http, HttpResponse } from "msw";
import { describe, expect, it, vi } from "vitest";
import { network } from "./server";
import { UPSTREAM_TIMEOUT_MS } from "../src/envelope";

const UPSTREAM = "https://api.coingecko.com/api/v3/simple/price";

async function callPrices(env: Env) {
  const ctx = createExecutionContext();
  const res = await worker.fetch(
    new Request("https://token.gnus.ai/v1/prices?ids=bitcoin&vs=usd"),
    env,
    ctx,
  );
  await waitOnExecutionContext(ctx);
  return res;
}

describe("x-cg-demo-api-key binding (D-04/D-05)", () => {
  it("sends the header when env.COINGECKO_API_KEY is set; never echoes it (SRVC-06)", async () => {
    let seen: string | null = "unset-sentinel";
    network.use(
      http.get(UPSTREAM, ({ request }) => {
        seen = request.headers.get("x-cg-demo-api-key");
        return HttpResponse.json({ bitcoin: { usd: 1 } });
      }),
    );
    const res = await callPrices({ COINGECKO_API_KEY: "test-key-123" } as unknown as Env);
    expect(res.status).toBe(200);
    expect(seen).toBe("test-key-123");
    expect(await res.text()).not.toContain("test-key-123");
  });

  it("anonymous when the key is unset — header null, still 200", async () => {
    let seen: string | null = "unset-sentinel";
    network.use(
      http.get(UPSTREAM, ({ request }) => {
        seen = request.headers.get("x-cg-demo-api-key");
        return HttpResponse.json({ bitcoin: { usd: 1 } });
      }),
    );
    const res = await callPrices({} as unknown as Env);
    expect(res.status).toBe(200);
    expect(seen).toBeNull();
    expect(await res.text()).not.toContain("test-key-123");
  });
});

describe("JS-level upstream timeout (KF-11)", () => {
  it("hang-gated upstream + fake timers past UPSTREAM_TIMEOUT_MS → abort, structured 502", async () => {
    vi.useFakeTimers();
    try {
      network.use(
        http.get(UPSTREAM, () => new Promise<Response>(() => {})), // never resolves
      );
      const pending = callPrices({} as unknown as Env);
      const res = await vi.advanceTimersByTimeAsync(UPSTREAM_TIMEOUT_MS + 100).then(() => pending);
      expect(res.status).toBe(502);
      const body = (await res.json()) as { error: { code: string } };
      expect(body.error.code).toBe("upstream_error");
      expect(await JSON.stringify(body)).not.toContain("test-key");
    } finally {
      vi.useRealTimers();
    }
  });
});

