// Plan 01-05 Task 2 — the suite's dedicated zero-egress canary (KF-2).
// One canary, one home: the 01-01 canary moved here from hello.test.ts.
// The outboundService guard in vitest.config.ts is structural; these tests
// make the property regression-tested, not conventional.
import { http, HttpResponse } from "msw";
import { describe, expect, it } from "vitest";
import { network } from "./server";

const UPSTREAM = "https://api.coingecko.com/api/v3";

describe("hermeticity (TEST-01 zero-egress)", () => {
  it("an unmocked upstream fetch is blocked — 599 egress_blocked, never a live response", async () => {
    // No handler for /ping — must fall through to the fail-closed guard.
    const res = await fetch(`${UPSTREAM}/ping`);
    expect(res.status).toBe(599);
    const body = (await res.json()) as { error: { code: string } };
    expect(body.error.code).toBe("egress_blocked");
  });

  it("mocked and unmocked paths discriminate: the mock answers, the sibling is blocked", async () => {
    network.use(
      http.get(`${UPSTREAM}/simple/price`, () =>
        HttpResponse.json({ bitcoin: { usd: 1 } }),
      ),
    );
    const mocked = await fetch(`${UPSTREAM}/simple/price?ids=bitcoin&vs_currencies=usd`);
    expect(mocked.status).toBe(200);
    expect(((await mocked.json()) as { bitcoin: unknown }).bitcoin).toBeDefined();

    const sibling = await fetch(`${UPSTREAM}/coins/list`);
    expect(sibling.status).toBe(599);
    const body = (await sibling.json()) as { error: { code: string } };
    expect(body.error.code).toBe("egress_blocked");
  });
});
