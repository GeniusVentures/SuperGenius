// Plan 01-02 Task 2 — server-side-only key binding (SRVC-06, D-04/D-05).
//
// Keyed-path proof lives at the fetchUpstream seam — the ONLY place the key
// is read (D-04). Since the 01-03 DO cutover, a unit-style env override on
// worker.fetch does NOT propagate into the Durable Object (the DO's env is
// platform-owned from wrangler bindings), so KF-9's env-spread pattern is
// applied directly to fetchUpstream instead. An end-to-end SELF test keeps
// the no-leak assertion on real response bodies.
// Timeout is likewise a direct fetchUpstream unit test: fake timers don't
// cross the DO RPC boundary (established empirically in plan 01-03).
import { SELF, env, reset } from "cloudflare:test";
import { http, HttpResponse } from "msw";
import { afterEach, describe, expect, it, vi } from "vitest";
import { network } from "./server";
import { UPSTREAM_TIMEOUT_MS } from "../src/envelope";
import { fetchUpstream } from "../src/upstream";
import type { Env } from "../src/index";

const UPSTREAM = "https://api.coingecko.com/api/v3/simple/price";

afterEach(async () => {
  vi.useRealTimers();
  await reset(); // storage reset suffices — no DO-memory assertions here
  network.resetHandlers();
});

describe("x-cg-demo-api-key binding (D-04/D-05) — fetchUpstream seam", () => {
  it("sends the header when env.COINGECKO_API_KEY is set (SRVC-06)", async () => {
    let seen: string | null = "unset-sentinel";
    network.use(
      http.get(UPSTREAM, ({ request }) => {
        seen = request.headers.get("x-cg-demo-api-key");
        return HttpResponse.json({ "key-a": { usd: 1 } });
      }),
    );
    const keyed = { ...env, COINGECKO_API_KEY: "test-key-123" } as Env;
    const rows = await fetchUpstream(["key-a"], "usd", keyed);
    expect(rows.get("key-a")?.price).toBe(1);
    expect(seen).toBe("test-key-123");
  });

  it("anonymous when the key is unset — header null, still succeeds", async () => {
    let seen: string | null = "unset-sentinel";
    network.use(
      http.get(UPSTREAM, ({ request }) => {
        seen = request.headers.get("x-cg-demo-api-key");
        return HttpResponse.json({ "key-b": { usd: 2 } });
      }),
    );
    const { COINGECKO_API_KEY: _omit, ...anon } = { ...env } as Env;
    const rows = await fetchUpstream(["key-b"], "usd", anon as Env);
    expect(rows.get("key-b")?.price).toBe(2);
    expect(seen).toBeNull();
  });

  it("end-to-end: response bodies through the full worker never contain the key", async () => {
    network.use(
      http.get(UPSTREAM, () => HttpResponse.json({ "key-c": { usd: 3 } })),
    );
    const res = await SELF.fetch("https://token.gnus.ai/v1/prices?ids=key-c&vs=usd");
    expect(res.status).toBe(200);
    expect(await res.text()).not.toContain("test-key-123");
    // 502-shaped bodies must not leak either
    network.resetHandlers();
    network.use(
      http.get(UPSTREAM, () =>
        new HttpResponse("<html>x</html>", { status: 403, headers: { "content-type": "text/html" } }),
      ),
    );
    const res2 = await SELF.fetch("https://token.gnus.ai/v1/prices?ids=key-d&vs=usd");
    expect(await res2.text()).not.toContain("test-key-123");
  });
});

describe("JS-level upstream timeout (KF-11) — direct fetchUpstream unit", () => {
  it("hang-gated upstream + fake timers past UPSTREAM_TIMEOUT_MS → abort", async () => {
    vi.useFakeTimers();
    network.use(
      http.get(UPSTREAM, () => new Promise<Response>(() => {})), // never resolves
    );
    const pending = fetchUpstream(["bitcoin"], "usd", {} as Env);
    // Attach the rejection handler IMMEDIATELY (no .then deferral) so the
    // abort-time rejection is always handled; settle on a flag.
    let rejected = false;
    const guarded = pending.catch(() => {
      rejected = true;
    });
    await vi.advanceTimersByTimeAsync(UPSTREAM_TIMEOUT_MS + 100);
    await guarded;
    expect(rejected).toBe(true);
  });
});
