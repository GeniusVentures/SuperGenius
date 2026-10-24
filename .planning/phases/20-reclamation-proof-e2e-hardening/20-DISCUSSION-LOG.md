# Phase 20: Reclamation Proof & E2E Hardening - Discussion Log

> **Audit trail only.** Decisions live in CONTEXT.md.

**Date:** 2026-10-24
**Areas discussed:** Compaction scheduling, Measurement & reporting, CI gate design, Race + immutability harness

## Compaction scheduling
| Option | Selected |
|--------|----------|
| Post-cycle + byte threshold (~16MB default; cancellable; never during settle; no new thread) | ✓ |
| Independent timer + persisted watermark | |

## Measurement & reporting
| Option | Selected |
|--------|----------|
| GetApproximateSizes at cycle-start vs post-compaction; bytes_reclaimed stamped AT compaction; honest split log | ✓ |
| SST/du file sizes | |

## CI gate design
| Option | Selected |
|--------|----------|
| Dedicated target; many known-size txs; ≥60%-of-erased-bytes bound; generous timeout | ✓ |
| Exact-delta assertion | |

## Race + immutability harness
| Option | Selected |
|--------|----------|
| Seam-held race (deterministic interleaving via the 18 interrupt point) + identity snapshots AFTER compaction | ✓ |
| True concurrent racing | |
