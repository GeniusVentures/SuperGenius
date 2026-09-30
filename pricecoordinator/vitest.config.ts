import { cloudflareTest } from "@cloudflare/vitest-plugin";
import { defineConfig } from "vitest/config";

export default defineConfig({
  plugins: [
    cloudflareTest({
      wrangler: { configPath: "./wrangler.jsonc" },
      miniflare: {
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
  test: { setupFiles: ["./test/setup.ts"] },
});
