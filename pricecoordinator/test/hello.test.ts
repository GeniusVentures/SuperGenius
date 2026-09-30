// Plan 01-01 harness proof — four tests that validate the entire toolchain
// before any domain code exists (TEST-01 bootstrap):
//   1. integration style (SELF.fetch)      → wrangler config + plugin wiring
//   2. unit style (worker.fetch + ctx)     → the path used later for keyed-env tests
//   3. MSW-works + layering proof          → mocked CoinGecko fetch answered BEFORE
//                                             the outboundService egress guard
//   4. hermeticity canary                  → unmocked upstream fetch is blocked
//                                             (599 egress_blocked) — zero egress
import { SELF, createExecutionContext, waitOnExecutionContext } from "cloudflare:test";
import worker from "../src/index";
import { http, HttpResponse } from "msw";
import { expect, it } from "vitest";
import { network } from "./server";

it("integration style: SELF.fetch returns 200 'ok'", async () => {
  const res = await SELF.fetch("https://token.gnus.ai/");
  expect(res.status).toBe(200);
  expect(await res.text()).toBe("ok");
});

it("unit style: worker.fetch returns 200 'ok'", async () => {
  const ctx = createExecutionContext();
  const res = await worker.fetch(
    new Request("https://token.gnus.ai/"),
    {} as unknown as Parameters<typeof worker.fetch>[1],
    ctx,
  );
  await waitOnExecutionContext(ctx);
  expect(res.status).toBe(200);
  expect(await res.text()).toBe("ok");
});

it("MSW-mocked CoinGecko fetch succeeds ahead of the egress guard", async () => {
  network.use(
    http.get("https://api.coingecko.com/api/v3/simple/price", () =>
      HttpResponse.json({ bitcoin: { usd: 1 } }),
    ),
  );
  const res = await fetch(
    "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd",
  );
  expect(res.status).toBe(200);
  const body = (await res.json()) as { bitcoin: { usd: number } };
  expect(body.bitcoin).toBeDefined();
  expect(body.bitcoin.usd).toBe(1);
});

it("hermeticity canary: unmocked upstream fetch is blocked (599 egress_blocked)", async () => {
  // No MSW handler registered for /ping — the request must fall through to the
  // fail-closed outboundService guard in vitest.config.ts, never the real network.
  const res = await fetch("https://api.coingecko.com/api/v3/ping");
  expect(res.status).toBe(599);
  const body = (await res.json()) as { error: { code: string } };
  expect(body.error.code).toBe("egress_blocked");
});
