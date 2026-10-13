// Single MSW network instance shared by every test file (KF-2).
// The lifecycle (enable/resetHandlers/disable) is wired in test/setup.ts,
// which vitest.config.ts registers as a global setupFile.
import { setupNetwork } from "@msw/cloudflare";

export const network = setupNetwork();
