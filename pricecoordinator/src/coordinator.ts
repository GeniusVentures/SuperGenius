// PriceCoordinator Durable Object (plan 01-03, SRVC-02/03/05) — the KF-4 shape:
// synchronous SQL prologue, joinable collecting batch window (setTimeout),
// per-id INSERT OR REPLACE persistence, stale-serve-on-failure, hold-off.
//
// Landmines honored: 1 (register before first await), 2 (reject waiters +
// clear state in finally), 3 (persist before resolve), 9 (60s hold-off after
// 429/403, never branch on rate-limit headers), 10 (epoch seconds),
// 11 (setTimeout, not alarms/interval).
import { DurableObject } from "cloudflare:workers";
import {
  buildEnvelope,
  classifyAge,
  FRESH_SEC,
  STALE_SEC,
  nowSec,
  usableRows,
  BATCH_WINDOW_MS,
  type PriceRow,
} from "./envelope";
import { fetchUpstream, UpstreamError } from "./upstream";
import type { Env } from "./index";

const MAX_IDS_PER_BATCH = 100; // > MAX_IDS_PER_REQUEST(50): one request always fits

interface Waiter {
  ids: string[];
  resolve: (rows: Map<string, PriceRow>) => void;
  reject: (e: unknown) => void;
}

const JSON_HEADERS = { "content-type": "application/json; charset=utf-8" };

export class PriceCoordinator extends DurableObject<Env> {
  private collecting: { ids: Set<string>; waiters: Waiter[] } | null = null;
  private inflight = false;
  private holdOffUntil = 0; // epoch ms — 429/403 back-pressure (Landmine 9)
  private flushTimer: ReturnType<typeof setTimeout> | undefined;
  private initialized = false;
  private pendingCurrency: string | null = null;

  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
  }

  private init(): void {
    if (this.initialized) return;
    // KF-5: schema is one row per id, epoch seconds (D-10/D-11).
    this.ctx.storage.sql.exec(
      "CREATE TABLE IF NOT EXISTS prices (" +
        "id TEXT PRIMARY KEY, " +
        "price REAL NOT NULL, " +
        "fetchedAt INTEGER NOT NULL)",
    );
    this.initialized = true;
  }

  async fetch(request: Request): Promise<Response> {
    // --- SYNCHRONOUS PROLOGUE: no await before waiter registration ---
    // (Landmine 1: DO input gates make this atomic.)
    this.init();

    const url = new URL(request.url);
    const currency = url.searchParams.get("vs") ?? "";
    const ids = (url.searchParams.get("ids") ?? "")
      .split(",")
      .filter((t) => t.length > 0);
    if (ids.length === 0 || currency.length === 0) {
      return doError(400, "invalid_request", "DO request missing ids/vs");
    }
    this.pendingCurrency = currency;

    const nowMs = Date.now();
    const now = Math.floor(nowMs / 1000);
    const rows = readRows(this.ctx.storage.sql, ids);
    const needed = ids.filter(
      (id) => !rows.has(id) || classifyAge(now - rows.get(id)!.fetchedAt) !== "fresh",
    );

    if (needed.length === 0) {
      // D-06a: fresh-from-SQL is source "coingecko" — no upstream, no join.
      const usable = usableRows(rows, now); // fresh rows always survive
      return envelopeResponse(currency, usable, ids, now, false);
    }

    let waiter: Promise<Map<string, PriceRow>> | null = null;
    if (nowMs >= this.holdOffUntil) {
      this.collecting ??= { ids: new Set(), waiters: [] };
      // Cap overflow: excess ids simply aren't refreshed this round (simplest
      // correct behavior; waiters never block).
      let budget = MAX_IDS_PER_BATCH - this.collecting.ids.size;
      const joining = budget <= 0 ? [] : needed.slice(0, budget);
      if (joining.length > 0) {
        for (const id of joining) this.collecting.ids.add(id);
        const joinedIds = [...joining];
        waiter = new Promise<Map<string, PriceRow>>((resolve, reject) => {
          this.collecting!.waiters.push({ ids: joinedIds, resolve, reject });
        });
        this.scheduleFlush();
      }
    }

    if (waiter === null) {
      // Landmine 9: hold-off (or a full batch budget) — do not fetch. This is
      // the stale-serve path (D-07): usable rows → stale envelope with
      // source "coingecko-cache"; nothing usable → structured 502.
      const usable = usableRows(rows, now);
      const anyUsable = ids.some((id) => usable.has(id));
      if (anyUsable) {
        return envelopeResponse(currency, usable, ids, now, true);
      }
      return doError(
        502,
        "upstream_error",
        "upstream unavailable (hold-off active after 429/403)",
      );
    }

    // --- END PROLOGUE (first await below) ---
    return awaitWaiterAndBuild(this, currency, ids, rows, waiter, now);
  }

  private scheduleFlush(): void {
    if (this.flushTimer !== undefined) return; // idempotent
    this.flushTimer = setTimeout(() => {
      this.flushTimer = undefined;
      void this.flush();
    }, BATCH_WINDOW_MS);
  }

  private async flush(): Promise<void> {
    const batch = this.collecting;
    const currency = this.pendingCurrency;
    this.collecting = null;
    if (!batch || currency === null) return;
    this.inflight = true;
    try {
      const fresh = await fetchUpstream([...batch.ids], currency, this.env);
      persistRows(this.ctx.storage.sql, fresh);
      // Prune >5min rows — unservable anyway (D-12); bounds storage (T-01-10).
      this.ctx.storage.sql.exec(
        "DELETE FROM prices WHERE fetchedAt < ?",
        nowSec() - STALE_SEC,
      );
      for (const w of batch.waiters) w.resolve(pick(fresh, w.ids));
    } catch (e) {
      if (e instanceof UpstreamError && (e.upstreamStatus === 429 || e.upstreamStatus === 403)) {
        this.holdOffUntil = Date.now() + FRESH_SEC * 1000; // Landmine 9
      }
      for (const w of batch.waiters) w.reject(e);
    } finally {
      this.inflight = false;
    }
  }
}

// --- module-level helpers (pure) ---

function readRows(sql: SqlStorage, ids: string[]): Map<string, PriceRow> {
  const rows = new Map<string, PriceRow>();
  if (ids.length === 0) return rows;
  const placeholders = ids.map(() => "?").join(",");
  const cursor = sql.exec(
    `SELECT id, price, fetchedAt FROM prices WHERE id IN (${placeholders})`,
    ...ids,
  );
  for (const row of cursor) {
    const r = row as { id: string; price: number; fetchedAt: number };
    rows.set(r.id, { id: r.id, price: r.price, fetchedAt: r.fetchedAt });
  }
  return rows;
}

function persistRows(sql: SqlStorage, rows: Map<string, PriceRow>): void {
  for (const r of rows.values()) {
    // INSERT OR REPLACE per returned id — a partial response never clobbers
    // rows for ids not returned (D-10).
    sql.exec(
      "INSERT OR REPLACE INTO prices (id, price, fetchedAt) VALUES (?, ?, ?)",
      r.id,
      r.price,
      r.fetchedAt,
    );
  }
}

function pick(fresh: Map<string, PriceRow>, ids: string[]): Map<string, PriceRow> {
  const out = new Map<string, PriceRow>();
  for (const id of ids) {
    const r = fresh.get(id);
    if (r) out.set(id, r);
  }
  return out;
}

function envelopeResponse(
  currency: string,
  rows: Map<string, PriceRow>,
  ids: string[],
  now: number,
  staleServe: boolean,
): Response {
  const envelope = buildEnvelope(currency, rows, ids, now, staleServe);
  return new Response(JSON.stringify(envelope), {
    status: 200,
    headers: JSON_HEADERS,
  });
}

function doError(
  status: number,
  code: string,
  message: string,
  upstreamStatus?: number,
): Response {
  return new Response(
    JSON.stringify({
      error: { code, message, ...(upstreamStatus !== undefined ? { upstreamStatus } : {}) },
    }),
    { status, headers: JSON_HEADERS },
  );
}

/**
 * Shared post-prologue path: await the batch waiter, merge SQL + fresh rows,
 * build the envelope. On waiter rejection (upstream failure) serve stale if
 * usable rows exist (D-07), else the structured 502 (D-08). NEVER throws.
 */
async function awaitWaiterAndBuild(
  _doRef: PriceCoordinator,
  currency: string,
  ids: string[],
  sqlRows: Map<string, PriceRow>,
  waiter: Promise<Map<string, PriceRow>>,
  now: number,
): Promise<Response> {
  try {
    const fresh = await waiter;
    const merged = new Map([...sqlRows, ...fresh]);
    const usable = usableRows(merged, now);
    return envelopeResponse(currency, usable, ids, now, false);
  } catch (e) {
    // Upstream failed. Stale-serve if usable rows exist (D-07).
    const usable = usableRows(sqlRows, now);
    const anyUsable = ids.some((id) => usable.has(id));
    if (anyUsable) {
      return envelopeResponse(currency, usable, ids, now, true); // source: coingecko-cache
    }
    if (e instanceof UpstreamError) {
      return doError(502, "upstream_error", e.message, e.upstreamStatus);
    }
    return doError(502, "upstream_error", e instanceof Error ? e.message : "upstream failure");
  }
}

