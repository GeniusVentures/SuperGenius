// Plan 01-05 Task 1 — config-inspection test (KF-10): the permanent guard
// making SRVC-07 (no Queues/KV), SRVC-03's config half (SQLite-only DO
// migration — Free plan), and D-04 (no inline secrets) CI-enforced invariants
// that survive future edits. Migrations are append-only once applied, so this
// test is the only forward defense (KF-1).
import { describe, expect, it } from "vitest";
import wranglerRaw from "../wrangler.jsonc?raw";
import { parseJsonc } from "./helpers";

const cfg = parseJsonc(wranglerRaw) as Record<string, unknown>;

describe("wrangler.jsonc permanent config guard", () => {
  it("uses a SQLite-backed DO migration including PriceCoordinator (Free plan, SRVC-03)", () => {
    const migrations = cfg.migrations as Array<Record<string, unknown>>;
    expect(
      Array.isArray(migrations) &&
        migrations.some((m) =>
          (m.new_sqlite_classes as string[] | undefined)?.includes("PriceCoordinator"),
        ),
    ).toBe(true);
  });

  it("contains NO bare new_classes array (KV-backed DOs are paid-only)", () => {
    expect(JSON.stringify(cfg.migrations)).not.toContain('new_classes":[');
  });

  it("has no Queues and no KV bindings (SRVC-07)", () => {
    expect(cfg.queues).toBeUndefined();
    expect(cfg.kv_namespaces).toBeUndefined();
    expect(JSON.stringify(cfg.durable_objects ?? {})).not.toMatch(/queue|kv/i);
  });

  it("never inlines credentials — no key name, no required-credential config (D-04)", () => {
    expect(wranglerRaw).not.toContain("COINGECKO_API_KEY");
    // `wrangler secret` is the deployment mechanism and appears in a comment
    // ("optional `wrangler secret` / .dev.vars entry") — assert no secrets
    // CONFIG BLOCK instead of banning the word entirely.
    expect(cfg.secrets).toBeUndefined();
  });
});

describe("parseJsonc (guard the guard)", () => {
  it("strips line and block comments, then parses", () => {
    const jsonc = [
      "{",
      "  // line comment",
      '  "a": 1, /* block',
      "  comment */",
      '  "b": "text"',
      "}",
    ].join("\n");
    expect(parseJsonc(jsonc)).toEqual({ a: 1, b: "text" });
  });
});
