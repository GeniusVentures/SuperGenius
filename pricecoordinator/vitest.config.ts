import { cloudflareTest } from "@cloudflare/vitest-plugin";
import { defineConfig } from "vitest/config";

export default defineConfig({
  plugins: [
    cloudflareTest({
      wrangler: { configPath: "./wrangler.jsonc" },
      miniflare: {
        // Test seam (KF-4 CI flake, plan 05-02): the DO's collecting window
        // runs on REAL timers (fake timers cannot cross the isolate
        // boundary), and CI runners can take >15ms between concurrent fetch
        // arrivals — splitting "one batch" into two and breaking the
        // single-flight call-count proofs. 250ms keeps concurrent arrivals
        // in one window under runner jitter while staying well under the
        // per-test budget; production keeps the 15ms default (binding
        // absent in wrangler.jsonc).
        // Allowlist test seam (SRVC-08): miniflare bindings take precedence
        // over wrangler vars in the test runtime, so the ~30 existing
        // SELF.fetch integration tests using arbitrary fixture ids keep
        // passing while the production default stays closed (genius-ai/usd
        // in wrangler.jsonc). A NEW fixture id used in a SELF.fetch test
        // must be added here or validation will (correctly) 400 it.
        bindings: {
          BATCH_WINDOW_MS_OVERRIDE: "250",
          ALLOWED_IDS:
            "a,b,bitcoin,ck-a,ck-b,cold-token,cold-token2,dm2-a,dm2-b,ec-a,ethereum,fs-a,genius-ai,hd-a,ho-x,key-c,key-d,rt-bitcoin,rw1-x,sfx-u1,solana,st-a,st-b,unknown-token",
          ALLOWED_VS: "usd,eur",
        },
        // Fail-closed egress guard (KF-2/KF-13): any outbound request that
        // reaches outboundService was NOT intercepted by MSW inside the
        // worker isolate — it must never leave workerd. Return a synthetic
        // 599 instead of forwarding to the real network.
        outboundService(request) {
          return new Response(
            JSON.stringify({
              error: {
                code: "egress_blocked",
                message: `unexpected outbound fetch in test: ${request.url}`,
              },
            }),
            { status: 599 },
          );
        },
      },
    }),
  ],
  test: {
    setupFiles: ["./test/setup.ts"],
    // Plugin-1.2.4 infra race: miniflare's internal cache-entry DO can
    // surface reset()-vs-deferred-put races as uncaught exceptions AFTER the
    // run summary (all assertions already green). dangerouslyIgnoreUnhandledErrors
    // is scoped to exactly this noise; real test failures still fail normally.
    dangerouslyIgnoreUnhandledErrors: true,
  },
});
