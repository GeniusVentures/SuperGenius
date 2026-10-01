---
phase: quick-260827-hbf
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - src/account/GeniusNode.hpp
autonomous: true
requirements:
  - QUICK-260827-01

must_haves:
  truths:
    - "SDK-side consumers can obtain the node's GraphSync network via GeniusNode's public GetGraphsyncNetwork() (GeniusSDK calls GeniusSDKGetNode()->GetGraphsyncNetwork(); no SDK change required)"
    - "The returned shared_ptr aliases the same graphsync::Network instance handed to the node's GlobalDBs"
  artifacts:
    - path: "src/account/GeniusNode.hpp"
      provides: "Public GetGraphsyncNetwork() accessor"
      contains: "GetGraphsyncNetwork"
  key_links:
    - from: "GeniusNode::GetGraphsyncNetwork()"
      to: "GeniusNode::graphsyncnetwork_ (private, declared line 820)"
      via: "inline return of the member"
      pattern: "return graphsyncnetwork_;"
---

<objective>
Add a single public accessor `GetGraphsyncNetwork()` to `GeniusNode` exposing the private `graphsyncnetwork_` member.

Purpose: Investigation confirmed the GeniusSDK cannot reach `graphsyncnetwork_` any other way — `GlobalDB` receives the network via constructor but exposes no getter, and `GeniusNode` exposes no `GetGlobalDB()`/`GetTxGlobalDB()` either. The SDK holds a `std::shared_ptr<GeniusNode>` via `GeniusSDKGetNode()` and includes `account/GeniusNode.hpp`, so a public getter on GeniusNode is the entire scope.

Output: One insertion in `src/account/GeniusNode.hpp` — nothing else changes.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
Investigation findings (locked — verified against source on 2026-08-27):

- `graphsyncnetwork_` is a private member of `GeniusNode`, declared at `src/account/GeniusNode.hpp:820`:
  `std::shared_ptr<ipfs_lite::ipfs::graphsync::Network> graphsyncnetwork_;`
- Created at `src/account/GeniusNode.cpp:1431` and passed into GlobalDB construction at `GeniusNode.cpp:1556` and `:1595`.
- No chained public path exists (no GlobalDB getter, no graphsync getter) — hence this accessor.
- GeniusSDK needs NO changes; it will call `GeniusSDKGetNode()->GetGraphsyncNetwork()`.

Insertion site (read these line ranges before editing):

- `src/account/GeniusNode.hpp:608-620` — anchor block:

```cpp
        /**
         * @brief Returns the underlying PubSub service.
         * @return Shared PubSub instance used by the node.
         */
        std::shared_ptr<ipfs_pubsub::GossipPubSub> GetPubSub()
        {
            return pubsub_;
        }

        /**
         * @brief Releases processing service, core, queue, and result-storage references.
         */
        void ResetProcessingMembers();
```

`GetPubSub()`'s closing brace is line 615; line 616 is blank; the `ResetProcessingMembers()` Doxygen block starts at line 617. The public section runs to line 789 (`    private:`) — the insertion point is public.

Style contract (match exactly — the snippet already does):
- 4-space class-member indent / 8-space continuation, Allman braces on their own line, inline body, `@brief`/`@return` Doxygen block.
</context>

<tasks>

<task type="auto">
  <name>Task 1: Insert GetGraphsyncNetwork() public accessor into GeniusNode.hpp</name>
  <files>src/account/GeniusNode.hpp</files>
  <action>
Single surgical insertion into the public getter section of `src/account/GeniusNode.hpp`. Insert the following snippet VERBATIM (user-supplied, locked — do not reword the Doxygen, rename, or restyle) between the blank line after `GetPubSub()`'s closing brace (line 616) and the `ResetProcessingMembers()` Doxygen block (line 617), so the accessor sits alongside the other runtime-service getter:

```cpp
        /**
         * @brief Returns the shared GraphSync network used by the node's GlobalDBs.
         * @return Shared graphsync Network instance; inbound graphsync for this
         *         host is dispatched through its registered protocol handler.
         */
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network> GetGraphsyncNetwork()
        {
            return graphsyncnetwork_;
        }
```

Keep one blank line before and after the snippet so the file's blank-line separation between members is preserved. Nothing else changes: no GeniusSDK edits, no GeniusNode.cpp edits, no include changes (the return type is already spelled identically at line 820 of this same header, so all needed types are in scope). Do not touch any other file.
  </action>
  <verify>
    <automated>
LINE=$(grep -n "GetGraphsyncNetwork" src/account/GeniusNode.hpp | head -1 | cut -d: -f1) && PRIV=$(grep -n "^    private:" src/account/GeniusNode.hpp | head -1 | cut -d: -f1) && [ -n "$LINE" ] && [ "$LINE" -lt "$PRIV" ] && [ "$(grep -c "GetGraphsyncNetwork" src/account/GeniusNode.hpp)" -eq 1 ] && git diff --stat src/account/GeniusNode.hpp && git diff src/account/GeniusNode.hpp | grep -c "^+" | grep -v '^0$' && echo "PASS: accessor present once, in public section"
    </automated>
  </verify>
  <done>
`git diff src/account/GeniusNode.hpp` shows exactly one hunk: the 8-line snippet inserted after `GetPubSub()` and before `ResetProcessingMembers()`, verbatim per the locked snippet, with blank-line separation. The accessor appears exactly once in the file, at a line number lower than the first `private:` label (789), proving it is public. No other file is modified.
  </done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| SDK consumer → GeniusNode internals | GeniusSDK (in-process, sibling repo) gains read access to the node's graphsync network via the new public accessor |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-Q-01 | Information Disclosure | GeniusNode::GetGraphsyncNetwork() | accept | Accessor is the explicit, user-locked intent; the SDK is a trusted in-process consumer holding the node shared_ptr already. No new external boundary crossed. |
| T-Q-02 | Tampering | shared_ptr aliasing of graphsyncnetwork_ | accept | Same exposure pattern as the existing GetPubSub() getter one line above; lifetime is owned by GeniusNode's declaration order (documented at lines 807-820). |
</threat_model>

<verification>
1. Grep position check (automated, in task verify): accessor exists exactly once, line number < 789 (public section).
2. Diff review: single hunk, verbatim snippet, correct anchor placement (after GetPubSub, before ResetProcessingMembers).
3. Compile risk is nil by construction — the return type is used verbatim in this same header at line 820; full submodule build intentionally NOT run (heavy submodule deps, per task constraints).
</verification>

<success_criteria>
- `GetGraphsyncNetwork()` is a public inline member of GeniusNode returning `std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>` by value, aliasing `graphsyncnetwork_`.
- Snippet is byte-identical to the locked user snippet; surrounding style (indent, Allman braces, Doxygen, blank lines) matches the file.
- Zero changes outside `src/account/GeniusNode.hpp`.
</success_criteria>

<output>
Create `.planning/quick/260827-hbf-check-if-graphsyncnetwork-can-be-obtaine/260827-hbf-SUMMARY.md` when done
</output>
