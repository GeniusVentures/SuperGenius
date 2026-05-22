# Checkpoint - Bridge Consensus Adapter Ready

Date: 2026-05-16

## Scope

This checkpoint is for handing the parent `SuperGenius` workspace to a new chat instance. It records the current consensus opaque-payload state, the completed bridge consensus adapter work, and the next bridge finalization integration step.

## Parent SuperGenius Status

Branch:

- `consensus_proto_refactor`

Recent committed consensus work:

- `fa80bcfd Add bridge consensus adapter`
- `083778e6 Add consensus subject type hash`
- `a32ca4cc Add generic consensus subjects`
- `b041fbe1 Expose string consensus handler registration`
- `ae670b36 Remove consensus subject enum identity`
- `96a8c4aa Use opaque consensus subject payloads`
- `4582b74a Test malformed consensus payloads`
- `cae17dc3 Name subject type hash size`

Current consensus subject shape:

- `account_id`
- `subject_type_hash`
- `payload`
- `payload_hash`

Removed from `ConsensusSubject`:

- `subject_id`
- `subject_type`
- built-in subject enum identity
- protobuf `oneof payload`
- `GenericSubject`

Built-in consensus payload messages still exist:

- `NonceSubject`
- `TaskResultSubject`
- `RegistryBatchSubject`

Those built-ins are now serialized into opaque `payload` bytes. Their payload bytes are prefixed with the 32-byte subject type hash using `base::Hash256::size()` through the named constant in `Consensus.cpp`, so protobuf unknown-field cross-parse cannot make one built-in payload decode as another built-in payload type.

Dispatch is by `subject_type_hash`.

Canonical built-in subject type strings:

- `sgns.nonce.v1`
- `sgns.task_result.v1`
- `sgns.registry_batch.v1`

Bridge subject type string:

- `gnus.bridge_event.v1`

Relevant consensus APIs / helpers:

- `ConsensusManager::ComputeSubjectTypeHash(std::string)`
- `ConsensusManager::CreateGenericSubject(account_id, subject_type, payload)`
- `ConsensusManager::DecodeNonceSubject(...)`
- `ConsensusManager::DecodeTaskResultSubject(...)`
- `ConsensusManager::DecodeRegistryBatchSubject(...)`
- `ConsensusManager::SubjectTypeMatches(...)`

Bridge consensus adapter status:

- Bridge-owned helpers live in `src/account/BridgeConsensusAdapter.hpp/.cpp`.
- `sgns::kBridgeEventSubjectType` owns `gnus.bridge_event.v1`.
- `sgns::CreateBridgeEventConsensusSubject(account_id, claim)` builds opaque bridge consensus subjects with:

```cpp
ConsensusManager::CreateGenericSubject(
    account_id,
    "gnus.bridge_event.v1",
    eth::bridge_event_claim_payload(claim));
```

- `sgns::DecodeBridgeEventConsensusSubject(subject)` checks the bridge subject type hash, checks the opaque payload hash, and decodes `eth::BridgeEventClaim`.
- `sgns::MakeBridgeEventConsensusHandler(...)` wraps bridge claim handlers and rejects malformed bridge payloads before dispatch.
- `sgns::RegisterBridgeEventConsensusHandler(...)` registers by the canonical subject type string through `Blockchain::RegisterSubjectHandler(...)`.
- Bridge-specific payload parsing remains outside core consensus.

## Verification Already Run

Parent SuperGenius:

- `build/OSX/Debug/test_bin/bridge_consensus_adapter_test`
  - 5 tests passed, covering bridge subject creation/decode, malformed bridge payload rejection, subject type hash mismatch, payload hash mismatch, and successful handler dispatch.
- `build/OSX/Debug/test_bin/consensus_subject_test`
  - 10 tests passed, including malformed built-in payload and hash/payload mismatch coverage.
- `ninja -C build/OSX/Debug`
  - Passed after the bridge adapter work.

evmrelay:

- `evmrelay/build/OSX/Debug/test_bin/bridge_observation_test`
  - 12 tests passed, including bridge event claim payload round-trip and malformed payload rejection.

## Current Parent Working Tree Notes

Observed parent status at checkpoint time:

- `ProofSystem` is an untracked or changed nested path.
- `SGProcessingManager` is a modified nested path.
- `evmrelay` is modified from the parent perspective because its submodule HEAD moved and it has local untracked artifacts.
- `AgentDocs/CHECKPOINT.md` is this checkpoint file.
- Other unrelated untracked local files/directories are present:
  - `new-issues.sh`
  - `src/crdt.zip`
  - `src/structure.txt`
  - `src/watcher.zip`
  - `test/src/proof/circuit.crct0`
  - `test/src/proof/sgnus_proof.bin`
  - `test/src/proof/table.tbl0`
  - `zkPOC/`

Do not revert or remove these unless explicitly requested.

## evmrelay Submodule Status

Submodule branch:

- `develop`

Current evmrelay HEAD:

- `1578296 Decode bridge event claim payloads`

Remote state:

- `develop` is ahead of `origin/develop` by one local commit unless pushed after this checkpoint.

Relevant evmrelay commits:

- `1578296 Decode bridge event claim payloads`
- `3a61a55 Fix zero offset eth message routing`
- `a1d2811 Expose bridge event claim payload`
- `92c33b3 Add bridge observation signing`
- earlier RPC receipt / codec / finality work

Current local untracked evmrelay artifacts:

- `AgentDocs/Refactor_chat.txt`
- `CRDT.Datastore.TEST.unit_2/`
- `CRDT.Datastore.TEST/`
- `examples/all.json`
- `examples/logs/`
- `examples/test_discovery.sh`
- `go-ethereum/`
- `rlp_enodes/`

The evmrelay checkpoint file has also been updated with bridge consensus handoff notes.

## Next Primary Work

The consensus refactor and bridge consensus adapter are ready. The next work should connect finalized bridge consensus results to transaction effects, not redesign consensus identity.

1. Find the existing bridge mint / claim completion path in SuperGenius, likely around `MintTransactionV2`, `TransactionManager::MintFunds(...)`, and bridge/public-chain validation.
2. Decide the exact certificate handler ownership for finalized bridge events. The current adapter registers subject handlers; the finalization path likely also needs a bridge-owned certificate handler registered by `gnus.bridge_event.v1`.
3. On finalized bridge certificates, decode the bridge claim with `DecodeBridgeEventConsensusSubject(...)`.
4. Convert the decoded `eth::BridgeEventClaim` into the existing mint / claim completion input without adding bridge-specific parsing to core consensus.
5. Add focused tests for:
   - proposal/certificate handling path for `gnus.bridge_event.v1`
   - finalized bridge claim routes into mint / claim completion
   - malformed finalized bridge payload is rejected or stalls according to the chosen handler contract
6. Push `evmrelay` commit `1578296` and parent commit `fa80bcfd` when ready.

## Secondary Remaining evmrelay Work

The older `eth_watch` cleanup remains secondary unless the user explicitly asks to resume it.

- `examples/eth_watch/eth_watch.cpp` may still have redundant local chain structure:
  - `ChainEntry::canonical_name`
  - local `kChains`
- Intended direction:
  - canonical chain names should come from bootstrap JSON / bootstrap peer helpers
  - no duplicate alias names in `eth_watch`
  - no duplicated bootnode arrays in `eth_watch`
  - fork hash should come from cached `chain_enodes.json.gz`
  - `network_id` and `genesis_hash` remain unless bootstrap metadata is extended
- Runtime handshake has not been re-proven end-to-end after those structural edits.

## Handoff Prompt

```text
Continue from AgentDocs/CHECKPOINT.md in the SuperGenius repo.

Current parent branch is consensus_proto_refactor.

The consensus refactor and bridge adapter are committed through:
- fa80bcfd Add bridge consensus adapter
- cae17dc3 Name subject type hash size
- 4582b74a Test malformed consensus payloads
- 96a8c4aa Use opaque consensus subject payloads
- ae670b36 Remove consensus subject enum identity
- b041fbe1 Expose string consensus handler registration
- a32ca4cc Add generic consensus subjects
- 083778e6 Add consensus subject type hash

ConsensusSubject now has only:
- account_id
- subject_type_hash
- payload
- payload_hash

It no longer has subject_id, subject_type, built-in enums, protobuf oneof payload, or GenericSubject.

Built-in subjects are still protobuf messages serialized into opaque payload bytes and prefixed with the 32-byte subject type hash. Dispatch is by subject_type_hash.

Verified:
- build/OSX/Debug/test_bin/bridge_consensus_adapter_test passed with 5 tests
- build/OSX/Debug/test_bin/consensus_subject_test passed with 10 tests
- ninja -C build/OSX/Debug passed
- evmrelay/build/OSX/Debug/test_bin/bridge_observation_test passed with 12 tests

evmrelay is on develop at local HEAD 1578296, ahead of origin/develop unless pushed. Relevant bridge payload helpers are:
- eth::bridge_event_claim_payload(const BridgeEventClaim&)
- eth::decode_bridge_event_claim_payload(const codec::ByteBuffer&)

Primary next step:
Continue bridge integration by routing finalized `gnus.bridge_event.v1` certificates into the existing bridge mint / claim completion path.

Use the bridge-owned adapter in `src/account/BridgeConsensusAdapter.hpp/.cpp`:
- CreateBridgeEventConsensusSubject(account_id, claim)
- DecodeBridgeEventConsensusSubject(subject)
- MakeBridgeEventConsensusHandler(handler)
- RegisterBridgeEventConsensusHandler(blockchain, handler)

Keep bridge-specific payload parsing and handling outside core consensus. Add the bridge-owned certificate/finalization integration and tests for proposal/certificate handling and successful mint/claim completion routing.

Do not remove unrelated local untracked files or artifact directories unless explicitly asked.
```
