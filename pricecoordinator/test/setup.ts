// MSW lifecycle around the whole run (KF-2). Plan 01-05 will extend the
// afterEach with `vi.useRealTimers()` → `reset()` → `abortAllDurableObjects()`;
// deliberately not added yet — no DO state exists to reset in 01-01.
import { afterAll, afterEach, beforeAll } from "vitest";
import { network } from "./server";

beforeAll(() => network.enable());
afterEach(() => network.resetHandlers());
afterAll(() => network.disable());
