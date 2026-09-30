// PriceCoordinator Durable Object — minimal stub (plan 01-01).
// The real single-flight coalescing + SQLite-backed implementation lands in
// plan 01-03; this stub only proves the wrangler DO wiring end-to-end.
import { DurableObject } from "cloudflare:workers";

export class PriceCoordinator extends DurableObject {}
