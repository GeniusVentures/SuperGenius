// Suite-wide test lifecycle (KF-2 + KF-8/Landmine 16). Every test file
// inherits this via vitest.config.ts setupFiles.
//
// afterEach order is LOAD-BEARING: real timers FIRST (reset/abort are
// clock-sensitive under fakes — Landmine 16), then reset() (wipes persisted
// binding data incl. DO SQL rows), then MSW handler cleanup.
//
// NOTE (empirical, plugin 1.2.4): calling abortAllDurableObjects() here, in
// EVERY afterEach, races miniflare's internal cache-entry Durable Objects
// (uncaught "Application called deleteAllDurableObjects()" noise that fails
// the run despite all assertions passing). Files that need DO memory-teardown
// (the coordinator.* and cache suites) keep their own afterEach with
// abortAllDurableObjects(); teardown of miniflare-internal DOs is inherently
// racy at suite granularity on this plugin version.
import { afterAll, afterEach, beforeAll, vi } from "vitest";
import { reset } from "cloudflare:test";
import { network } from "./server";

// NOTE (plugin 1.2.4 infra race): reset() natively calls
// deleteAllDurableObjects(); a deferred ctx.waitUntil(caches.default.put)
// from an earlier test can land on the aborted internal cache DO and surface
// as an uncaught "Application called deleteAllDurableObjects()" AFTER the
// green summary. Suppressed via dangerouslyIgnoreUnhandledErrors in
// vitest.config.ts — scoped infra-teardown noise, not test behavior. Real
// assertion failures still fail the run.

beforeAll(() => network.enable());

afterEach(async () => {
  vi.useRealTimers();
  await reset();
  network.resetHandlers();
});

afterAll(() => network.disable());
