# Deploying token.gnus.ai — price-coordinator Cloudflare Worker

An operator runbook: from a fresh clone of this repository to a live
`https://token.gnus.ai/v1/prices` endpoint serving the C++ client's tier-2
fallback price feed. **No worker code changes are required** — the worker is
complete and fully tested; this document only deploys it.

---

## 1. Overview — what you are deploying

The **price-coordinator** is a Cloudflare Worker that acts as the tier-2
fallback price feed for the SuperGenius C++ node. It exposes a single endpoint:

```
GET /v1/prices?ids=<comma-separated-coin-ids>&vs=<fiat-currency>
```

and returns a JSON **PriceEnvelope** (prices, freshness flags, source, age).

Request flow inside the worker:

1. **Edge cache tier** — a `caches.default` read-through cache with canonical
   keys (sorted + deduped ids) and FRESH-ONLY admission sits *ahead* of the
   Durable Object. Fresh responses carry `Cache-Control: public, max-age=45`;
   stale and error responses are never admitted.
2. **PriceCoordinator Durable Object** — one instance per currency via
   `idFromName(currency)`, owning request coalescing, SQLite persistence,
   freshness gates, stale-serve, and upstream hold-off.

The canonical production origin is **`https://token.gnus.ai`**. The C++
client's default fallback URL and the worker's canonical cache keys both
hardcode this hostname (see §10).

## 2. Prerequisites

| Requirement | Notes |
|---|---|
| Cloudflare account | **The Free plan works.** The PriceCoordinator Durable Object uses the SQLite-backed storage class (migration `v1` → `new_sqlite_classes` in `wrangler.jsonc`), which is available on the Free plan and is required by the worker's `ctx.storage.sql` usage. |
| gnus.ai DNS zone on that account | Required for the custom-domain step (§7). The zone must live on the *same* Cloudflare account you deploy the worker to. |
| Node ≥ 20 | `node --version` to check. |
| wrangler | Already vendored — `wrangler ^4.136.3` is in `node_modules` (devDependency). **Never install wrangler globally**; always invoke it as `npx wrangler`. No `npm install` is needed in this directory. |

Commands below are copy-pasteable with your cwd at the **repository root**
(the directory containing `SuperGenius/`). Steps that need a different working
directory say so explicitly — wrangler resolves `wrangler.jsonc` from the
*current working directory*, so deploy/secret/tail commands must run from
`SuperGenius/pricecoordinator/`.

## 3. Pre-deploy gates (both must be green)

Run from the repository root:

```powershell
npm --prefix SuperGenius/pricecoordinator run test
npm --prefix SuperGenius/pricecoordinator run typecheck
```

- `test` runs the vitest suite (worker + DO + validation + regression tests).
- `typecheck` runs `tsc --noEmit` over both the worker and test tsconfigs.

**Do not deploy unless both exit 0.**

## 4. Authentication

Interactive (recommended for a first deploy):

```powershell
cd SuperGenius/pricecoordinator
npx wrangler login
```

This opens a browser flow and stores an OAuth token locally.

CI / non-interactive alternative — set the `CLOUDFLARE_API_TOKEN` environment
variable instead:

```powershell
$env:CLOUDFLARE_API_TOKEN = "<your-api-token>"
```

Create the token in the Cloudflare dashboard under **My Profile → API
Tokens**, using the **"Edit Cloudflare Workers"** template, scoped to your
account. When `CLOUDFLARE_API_TOKEN` is set, wrangler skips the browser flow
entirely.

## 5. Deploy

```powershell
cd SuperGenius/pricecoordinator
npx wrangler deploy
```

This single command:

- uploads the worker script (`src/index.ts` entrypoint) to your account, and
- applies the Durable Object migration (`v1` → `new_sqlite_classes`:
  `PriceCoordinator`), creating the DO class on first deploy.

On success wrangler prints the preview route, which goes live immediately:

```
https://price-coordinator.<your-subdomain>.workers.dev
```

> The workers.dev route is fine for smoke-testing, but it is a **second live
> origin** — §7 makes `token.gnus.ai` the primary route and shows how to
> disable the workers.dev one.

## 6. Optional: CoinGecko API key

The worker runs **keyless** against CoinGecko's public API at lower rate
limits. To raise the limits, set the secret:

```powershell
cd SuperGenius/pricecoordinator
npx wrangler secret put COINGECKO_API_KEY
```

wrangler prompts for the value — paste your key (`<your-api-key>`); it is
stored encrypted in Cloudflare and never echoed.

> **D-04 (hard rule):** secrets live ONLY in `wrangler secret` (production) or
> a local `.dev.vars` file (development). They are **never** written into
> `wrangler.jsonc`, never committed to git, and never appear in shell history
> or logs. `.dev.vars` is local-only and must stay untracked (it is already in
> this directory's `.gitignore`).

## 7. Custom domain `token.gnus.ai`

Today's `wrangler.jsonc` has **no `routes` stanza** — the deployed worker is
reachable only on its `*.workers.dev` preview route until you attach the
custom domain. Choose **one** of the two options below.

**Prerequisite:** the `gnus.ai` DNS zone must be on the same Cloudflare
account as the worker (§2).

### Option (a) — dashboard (no file edit)

1. Cloudflare dashboard → **Workers & Pages** → `price-coordinator`
   → **Settings** → **Domains & Routes** → **Add** → **Custom Domain**.
2. Enter `token.gnus.ai` and confirm.
3. Cloudflare auto-provisions the DNS record and the TLS certificate; wait for
   the cert to become active (usually under a minute).

### Option (b) — wrangler.jsonc edit

Add a `routes` stanza with `custom_domain: true`, then re-deploy:

```jsonc
  "routes": [
    { "pattern": "token.gnus.ai", "custom_domain": true }
  ],
```

```powershell
cd SuperGenius/pricecoordinator
npx wrangler deploy
```

> This edit is an **operator action at deploy time**. It is intentionally not
> pre-applied to the committed `wrangler.jsonc`.

### Make token.gnus.ai the primary route

The canonical cache keys inside the worker and the C++ client default both
hardcode `token.gnus.ai`, so it should be the *only* advertised origin:

- Disable the workers.dev route: dashboard → `price-coordinator` →
  **Settings** → **Domains & Routes** → workers.dev route → toggle **Disable**,
  or equivalently add `"workers_dev": false` to `wrangler.jsonc`.

## 8. Production cautions

- **`BATCH_WINDOW_MS_OVERRIDE` must NOT be set in production.** It is a test
  seam (see the `Env` docblock in `src/index.ts`) that widens the DO's
  collecting window so CI runners can observe coalescing. Production always
  uses the compiled-in `BATCH_WINDOW_MS`.
- **Never add message-queue or key-value (KV) bindings** to this worker
  (hard rule SRVC-07). Its state model is exactly: edge cache + the SQLite
  Durable Object.
- **Never change the DO storage class away from SQLite.** The Free plan
  compatibility and the worker's `ctx.storage.sql` queries both depend on
  `new_sqlite_classes`. Re-creating the class as `new_classes` would break
  both.
- **Never put the CoinGecko key (or any secret) into `wrangler.jsonc`** — see
  the D-04 rule in §6.

## 9. Post-deploy verification

Run from anywhere (all checks hit the live origin):

```powershell
# Happy path: 200, PriceEnvelope with "stale": false on fresh admission,
# and Cache-Control: public, max-age=45
# NOTE: the CoinGecko asset id for GNUS is "genius-ai" (the C++ client's
# IPriceSource docs use the same id). Plain "gnus" is an unknown id and
# returns 200 with "prices": {} (D-09 silently drops unknown ids).
curl.exe -i "https://token.gnus.ai/v1/prices?ids=genius-ai&vs=usd"

# Negative: unknown path -> 404 {"error":{"code":"not_found",...}}
curl.exe -i "https://token.gnus.ai/v1/prices/extra"

# Negative: non-GET method -> 405 method_not_allowed
curl.exe -i -X POST "https://token.gnus.ai/v1/prices?ids=genius-ai&vs=usd"

# Cache canonicalization: duplicate ids address the same canonical cache key
# (sorted + deduped) — compare the two envelopes; the second is a cache hit.
curl.exe -s "https://token.gnus.ai/v1/prices?ids=genius-ai,genius-ai&vs=usd"
```

Expected on the happy path: HTTP 200, JSON body with `"stale": false`, header
`Cache-Control: public, max-age=45`. (A `"stale": true` envelope with source
`"coingecko-cache"` is *valid* stale-serve behavior after upstream failures —
it simply is not admitted to the edge cache, and its max-age header differs.)

Durable Object health:

- Dashboard → **Storage & Databases** → **Durable Objects** → namespace
  **PriceCoordinator** → inspect per-currency instances and their SQLite
  storage.
- Live logs:

```powershell
cd SuperGenius/pricecoordinator
npx wrangler tail
```

Leave `tail` running while you re-issue the curl checks to watch requests flow
through the router, cache tier, and DO.

## 10. C++ client resolution — zero code changes

The SuperGenius C++ client already points at this worker:

- `SuperGenius/src/coinprices/PriceEndpoints.hpp` defines
  `kFallbackBaseUrlDefault = "https://token.gnus.ai"`, and **the client
  appends `/v1/prices`** to that base.
- Tier-2 (fallback) base URL override: `SGNS_PRICE_FALLBACK_URL` env var
  (when set and non-empty it replaces `token.gnus.ai`).
- Tier-1 (primary, CoinGecko direct) base URL override: `SGNS_COINGECKO_URL`
  env var. Tier-2 is exercised when tier-1 is unset **or** fails.

To reproduce exactly what the C++ fallback request hits, with no C++ side
effects:

```powershell
curl.exe -i "https://token.gnus.ai/v1/prices?ids=genius-ai&vs=usd"
```

If that returns 200 with a fresh envelope, every node running default
configuration (no `SGNS_PRICE_FALLBACK_URL` / `SGNS_COINGECKO_URL` overrides,
tier-1 failing or unset) will consume this endpoint unchanged.

## 11. Ops — rollback and versions

Incident recovery (run from `SuperGenius/pricecoordinator`):

```powershell
cd SuperGenius/pricecoordinator
npx wrangler versions list
npx wrangler rollback
```

- `versions list` shows deployed versions; `rollback` re-points live traffic
  at the previous version. Durable Object data (SQLite rows) is retained.
- `compatibility_date` (currently `2026-09-01` in `wrangler.jsonc`) changes
  alter runtime behavior gates — treat any change as a **deliberate
  follow-up** with a re-run of the §3 test suite, never as part of a routine
  deploy.

---

*Placeholders such as `<your-api-token>` and `<your-api-key>` mark every
location where a real secret would appear — this runbook intentionally
contains none (D-04).*
