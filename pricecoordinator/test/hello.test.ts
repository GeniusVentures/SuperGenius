// Plan 01-01 harness proof — the plugin-wiring tests (both invocation
// styles). The MSW-layering and egress-canary tests moved to their dedicated
// home in hermeticity.test.ts (plan 01-05: one canary, one home).
// Since plan 01-02 the router serves only GET /v1/prices; GET / is 404 JSON.
import { SELF, createExecutionContext, waitOnExecutionContext } from "cloudflare:test";
import worker from "../src/index";
import { expect, it } from "vitest";

it("integration style: SELF.fetch reaches the worker (GET / → 404 JSON not_found)", async () => {
  const res = await SELF.fetch("https://token.gnus.ai/");
  expect(res.status).toBe(404);
  const body = (await res.json()) as { error: { code: string } };
  expect(body.error.code).toBe("not_found");
});

it("unit style: worker.fetch reaches the worker (GET / → 404 JSON not_found)", async () => {
  const ctx = createExecutionContext();
  const res = await worker.fetch(
    new Request("https://token.gnus.ai/"),
    {} as unknown as Parameters<typeof worker.fetch>[1],
    ctx,
  );
  await waitOnExecutionContext(ctx);
  expect(res.status).toBe(404);
  const body = (await res.json()) as { error: { code: string } };
  expect(body.error.code).toBe("not_found");
});
