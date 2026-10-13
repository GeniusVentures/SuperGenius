---
phase: 15-private-networks-consume-privatenetworkid-identity-and-bind
plan: "10"
subsystem: auth
tags: [config, privatenetwork, geniusnode, failclosed, cplusplus, cmake]

# Dependency graph
requires:
  - 15-01 config identity surface (private_network_id/network_key parsing, NetworkSettings.valid fail-closed channel, test helpers)
provides:
  - WriteNetworkConfig always emits parseable JSON for every accepted PSK encoding (full string-unsafe escaping incl. \n \r \t \b \f and <0x20 as \u00XX)
  - LoadNetworkConfig fails closed on an existing-but-unparseable network_config.json (settings.valid=false -> InitNetwork aborts) — closes the silent public-boot chain (VERIFICATION gap 6, CR-01+WR-01)
affects: [geniusnode-config, public-node provisioning behavior (unchanged, pinned by test)]

# Tech tracking
tech-stack:
  added: [] # no new third-party dependencies
  patterns:
    - "Input-class-complete fail-closed posture: identity validation is meaningless until parse succeeds, so the parse gate itself is fatal for EXISTING files while a MISSING file stays the documented public default (D-01)"

key-files:
  created: []
  modified:
    - src/account/GeniusNode.cpp
    - test/src/account/network_config_private_network_test.cpp

key-decisions:
  - "Missing-file branch kept public-default (not made fatal): a missing network_config.json is the documented public-node provisioning state per D-01; the exploit chain is closed by writer escaping + fatal parse errors on existing files together"
  - "Parse-error log message names only the file and condition (GeniusNodeLogger()->error), never key values — D-03 posture maintained"

requirements-completed: [D-01, D-02, PNET-CFG]

# Metrics
duration: 6min
completed: 2026-09-03
---

# Phase 15 Plan 10: Fail-Closed Provisioning Chain (CR-01/WR-01) Summary

**Full JSON string escaping in WriteNetworkConfig plus a fatal parse-error branch in LoadNetworkConfig close the write→reload silent-public-boot chain, pinned by 2 new regression scenes (suite 7/7)**

## Performance

- **Duration:** ~6 min (started 2026-09-03T11:40:10Z, completed 2026-09-03T11:46:19Z)
- **Tasks:** 2/2
- **Files modified:** 2

## Accomplishments

- **Task 1 (615375ee):** `WriteNetworkConfig`'s escaping switch extended to full JSON string
  safety (`\\`, `"`, `\n`, `\r`, `\t`, `\b`, `\f`; any other `<0x20` char as
  `fmt::format("\\u{:04x}", ...)`), so the canonical go-ipfs swarm-key text (embedded literal
  newlines) round-trips as valid JSON. `LoadNetworkConfig`'s parse-error branch
  (`HasParseError() || !IsObject()`) now sets `settings.valid = false` and logs
  "network_config.json is unreadable or invalid JSON - refusing to start" — InitNetwork's
  existing `!settings.valid` abort (~:1938 post-edit) fails the load. The missing-file branch
  is unchanged public default, now carrying a comment documenting the deliberate
  missing-vs-corrupt distinction (D-01).
- **Task 2 (a1119160):** Two regression scenes in
  `network_config_private_network_test.cpp`:
  `SwarmKeyTextWithNewlinesRoundTrips` (CR-01 — asserts the file contains the escaped
  two-char `\n` sequence with zero raw newline bytes, and that `GeniusNode::New` retains
  `VALID_PRIVATE_NETWORK_ID` after reload; pre-fix the node booted public with an empty id)
  and `CorruptConfigFailsNodeStart` (WR-01 — an existing file with `", \"broken\": [unclosed"`
  yields `GeniusNode::New == nullptr`; pre-fix the node started public).

## Task Commits

1. **Task 1: Fail-closed provisioning chain (writer escaping + loader parse-error failure)** - `615375ee` (fix)
2. **Task 2: Round-trip and fail-closed regression scenes** - `a1119160` (test)

**Plan metadata:** (this commit — docs: complete plan)

## Files Created/Modified

- `src/account/GeniusNode.cpp` - WriteNetworkConfig escaping switch (:226-276) full control-char coverage; LoadNetworkConfig missing-file comment (:1617-1621) + parse-error fail-closed branch (:1627-1636) with exact review-sketch error message
- `test/src/account/network_config_private_network_test.cpp` - 2 new scenes + `#include <algorithm>` (std::count for the raw-newline assertion)

## Verification Results

- `ninja -C build/OSX/Release genius_node` - builds clean (only pre-existing warning at the untouched NodeState enum switch, :4742)
- `ninja -C build/OSX/Release network_config_private_network_test` - builds clean
- `ctest -R network_config_private_network` - **7/7 scenes PASS** (~13s): AbsentKeysKeepPublicNodeBehavior, ValidIdentityPairIsRetainedAndDistinctFromKey, UppercaseHexBodyIdentityAccepted, MalformedIdentityFailsNodeStart, HalfProvisionedIdentityPairFailsNodeStart + the 2 new scenes
- `ctest -R "private_network_registry_binding|network_registry_test"` - **2/2 PASS** (regression guards; same file, adjacent regions untouched)
- Source assertions: five escape cases at :244/:247/:250/:253/:256 + `u{:04x}` fallback at :262; `valid = false` in the HasParseError block (:1634); zero `valid = false` in the missing-file branch

## Deviations from Plan

### Plan-Snippet Adaptations

**1. Stale sed line anchor in Task 1's verify command**
- **Found during:** Task 1 verification
- **Issue:** The plan's `sed -n '1599,1610p' ... | grep -c "valid = false"` anchor assumed the loader at pre-edit positions, but the writer change (+30 lines) shifts the parse-error branch to :1627-1636.
- **Fix:** Ran the identical assertions at the actual positions (output above) — `valid = false` confirmed inside the `HasParseError() || !IsObject()` block and absent from the missing-file branch. Same adaptation class 15-01 already documented for this file's drift-prone anchors.
- **Files modified:** none (verification-only adaptation)

---

**Total deviations:** 1 verification-only adaptation
**Impact on plan:** None — both tasks implemented exactly per the binding CR-01/WR-01 review sketches.

## Issues Encountered

None.

## Known Stubs

None. Both new scenes fully assert real behavior (escaped file text + retained identity; nullptr on corrupt file). No placeholder values introduced.

## Threat Flags

None. Threat register mitigations landed as planned: T-15-10-01 (WR-01 fatal parse errors — implemented and tested), T-15-10-02 (CR-01 full escaping — implemented and tested), T-15-10-03 (error logs name only the file and condition — verified message text contains no key material), T-15-10-04 (missing-file public default — pinned green by AbsentKeysKeepPublicNodeBehavior). No new security-relevant surface beyond the plan's threat model.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- VERIFICATION gap 6 closed: the fail-closed identity posture now holds for unparseable configs and newline-bearing swarm keys
- Written configs are always valid JSON; any corruption of an existing file is fatal — later plans can assume an existing network_config.json is either parseable or the node refuses to start

## Self-Check: PASSED

- Both modified files exist on disk; commits 615375ee and a1119160 verified in git log
- 7/7 suite scenes + both regression-guard suites green
---
*Phase: 15-private-networks-consume-privatenetworkid-identity-and-bind*
*Completed: 2026-09-03*
