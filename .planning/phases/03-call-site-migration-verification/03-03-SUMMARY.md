---
plan: 03
phase: 3-call-site-migration-verification
status: code-complete-build-pending
requirements: [INTF-04, MIG-03, MIG-04]
---

# Plan 03 Summary — Delete Old Factories + Verification

## What was built
Deleted the 3 old `GeniusNode` factories + the old private ctor (INTF-04) as the final step, after Plan 02 left zero callers:
- Header: old `New(autodht, port_seed, is_full_node)` overload, `NewFromPrivateKey`, `NewFromMnemonic`, and the old private ctor `(dev_config, account, autodht, port_seed, is_full_node)` declarations removed (with their Doxygen).
- Implementation: the matching 4 definitions removed.
- **Retained:** canonical `New(dev_config, AccountSource)`, the reordered `(dev_config, AccountSource)` ctor (`std::visit`), `WriteNetworkConfig`/`WriteSgnsConfig`, `NodeType` enum, all Phase 1/2 work. `GeniusAccount::NewFromPrivateKey` (different class) untouched.

Per CONTEXT D-03: migrate-then-delete (build green throughout — deletion was the final commit, after all callers migrated).

## Commit
`refactor(03-03): delete old factories + old private ctor (INTF-04) — canonical New(dev_config,AccountSource) is sole entry point`

## Self-Check (static, PASS)
- ✓ MIG-04 grep: `GeniusNode::NewFromPrivateKey`/`NewFromMnemonic` in `src/`+`example/`+`test/` = **0**; every `GeniusNode::New(` call is the canonical 2-arg factory.
- ✓ Retention: canonical `New` (1 decl + 1 def), new `(dev_config, AccountSource)` ctor (1 decl + 1 def, full `std::visit` body intact), helpers + `INVALID_NODE_TYPE` present.
- ✓ No duplicate/mangled ctor (verified the real ctor's init-list + derivation + 4 visitor branches survived).
- ○ MIG-03 (full build + CTest green) pending user environment.
