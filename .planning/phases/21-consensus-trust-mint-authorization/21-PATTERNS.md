# Phase 21: Consensus Trust & Mint Authorization - Pattern Map

**Mapped:** 2026-10-19
**Files analyzed:** 14 (8 modified source areas, 1 new helper, 4 test areas + build wiring, 1 doc deliverable)
**Analogs found:** 13 / 14 (the audit doc is a document, not code — no analog needed)

> All line numbers below were re-verified against current `develop` this session. The research file (21-RESEARCH.md) line citations were confirmed accurate.
> **Correction to CONTEXT.md/CONCERNS.md:** `src/account/BridgeConsensusAdapter.cpp` **no longer exists** (removed in commit `76ea1b754`). The live constructor-injection exemplars are `TransactionConsensusHandler` (separate class holding an `owner_` reference, TransactionConsensusHandler.hpp:57-61) and `TransactionManager::New`'s injected dependency list (TransactionManager.cpp:124-134). Planner should cite those, not the deleted adapter.

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/blockchain/ValidatorRegistry.{hpp,cpp}` (verified loader + cap/equivocation wiring) | service (domain owner: registry authorization) | request-response (CID load + chain-walk verify) | same file: `VerifyUpdateClassified` (:1551-1610) + `FilterRegistryUpdate` (:1454-1487) | exact |
| `src/blockchain/Consensus.{hpp,cpp}` (grandfather probe, verified-only cache) | service (certificate validation) | request-response (durable probe + verdict) | same file: `SubmitCertificate` probe (:2735-2753), verdict memo (:1362-1393) | exact |
| NEW bounded-defer helper (`{hpp,cpp}`, placement = planner; first consumer is PCIV or Consensus) | utility (deadline + FIFO + oldest-first eviction) | event-driven (defer/retry) | `PublicChainInputValidator.cpp:264-299` FIFO + `crdt::CRDTWorkJournal` (`crdt_work_journal.hpp:34-115`) | role-match (composite of two) |
| `src/transaction/TransactionConsensusHandler.cpp` (v1-mint consensus-ingress gate) | controller (consensus ingress) | request-response (validate → verdict) | same file: `ValidateTransactionForConsensus` (:515-554) | exact |
| `src/transaction/TransactionManager.{hpp,cpp}` (sync filter / preflight gates) | middleware (CRDT admission filter) | event-driven (delta admission) | same file: `FilterTransaction` (:3772-3832), preflight (:2019-2055) | exact |
| `src/account/PublicChainInputValidator.{hpp,cpp}` (fail-closed branches, receipt decode + bind, defer-window plumbing) | service (evidence validator) | request-response (RPC evidence) | same file: `GatherVerificationEvidence` (:307-439) | exact |
| `src/account/MintTransactionV2.{hpp,cpp}` (fact accessors if needed — identity stays in `GetSlotID`) | model (canonical burn-fact identity) | transform | same file: `GetSlotID` (:212-234) | exact |
| `src/account/GeniusNode.{hpp,cpp}` (D-03/D-04 config keys; folded-todo pinned fingerprint) | config (boot wiring) | file-I/O (JSON config read) | same file: `LoadNetworkConfig` (:1621-1694) | exact |
| `src/account/BurnConfig.cpp`, `src/securecrdt/QuorumThresholdValidation.hpp`, `src/trustedpeer/*` (folded-todo touchpoints) | config / domain | file-I/O | `GeniusNode.cpp:490-534` trust-field parse + TrustStartupController wiring (:927-959) | role-match |
| `test/src/blockchain/` new TRUST-01/02 + MINT-03 targets, extended `validator_registry_batch_slot_test` / `multi_node_finality_fault_test` | test | integration (multi-node / registry unit) | `multi_node_finality_fault_test.cpp:1-70` + `:1878`; `validator_registry_promotion_test` static-helper pattern | exact |
| `test/src/blockchain/CMakeLists.txt` (addtest wiring; maybe re-enable `consensus_certificate_test`) | config (build) | n/a | existing `addtest(...)` + `target_link_libraries` + `set_tests_properties` entries | exact |
| `test/src/account/public_chain_input_validator_slot_test.cpp` (MINT-02/04 unit pairs) | test | request-response (mock RPC) | `bridge_e2e_chainlist_test.cpp:295-343` (`SetTransportFactory` mock + v1 mint builder) | exact |
| `test/src/bridge_e2e/` (MINT-04 adversarial pair) | test | request-response (anvil/mock) | same suite: `anvil_fixture.hpp`, chainlist mocks | exact |
| TRUST-02 audit document (suggested `AgentDocs/` or `.planning/notes/`) | doc | n/a | — | none needed |

## Pattern Assignments

### `src/blockchain/ValidatorRegistry.{hpp,cpp}` (service, request-response)

**Analog:** the same file's filter wiring and verification machinery — the strict loader *joins* these, it does not invent a new verifier.

**Three-verdict enum to reuse** — `ValidatorRegistry.hpp:311-328`:
```cpp
enum class UpdateVerification : uint8_t
{
    kValid,           ///< Update fully verified.
    kMissingDependency, ///< Referenced data (base registry snapshot, member certificate) not synced locally yet — retryable.
    kInvalid,         ///< Update failed verification permanently.
};
UpdateVerification VerifyUpdateClassified( const RegistryUpdate &update, bool enforce_time_window ) const;
```

**Fail-closed filter wiring the strict loader mirrors** — `ValidatorRegistry.cpp:1467-1486`:
```cpp
switch ( VerifyUpdateClassified( update, false ) )
{
case UpdateVerification::kValid:            return crdt::CRDTDataFilter::ElementFilterResult::Accept();
case UpdateVerification::kMissingDependency:
    // ... Stall the whole delta job instead of permanently dropping the element ...
    return crdt::CRDTDataFilter::ElementFilterResult::Stall();
case UpdateVerification::kInvalid: default:
    return crdt::CRDTDataFilter::ElementFilterResult::Reject();
}
```

**Raw loader body that becomes the private recursion-safe core** — `ValidatorRegistry.cpp:785-820` (today's `LoadRegistryByCid`; reads `db_->GetLocalDeltaKeyValues(cid)` directly, bypassing the filter — the TRUST-01 bypass):
```cpp
outcome::result<ValidatorRegistry::Registry> ValidatorRegistry::LoadRegistryByCid( const std::string &cid ) const
{
    BOOST_OUTCOME_TRY( auto delta_key_values, db_->GetLocalDeltaKeyValues( cid ) );
    crdt::HierarchicalKey registry_key{ registry_key_ };
    for ( const auto &[key, registry_update_buffer] : delta_key_values )
    {
        if ( key != registry_key.GetKey() ) { continue; }
        std::vector<uint8_t> serialized_update( registry_update_buffer.begin(), registry_update_buffer.end() );
        auto decoded = DeserializeRegistryUpdate( serialized_update );
        if ( decoded.has_error() ) { continue; }
        return decoded.value().registry();
    }
    return outcome::failure( std::errc::no_such_file_or_directory );
}
```

**The recursion trap the raw core protects against** — `ValidatorRegistry.cpp:1568-1589` (`VerifyUpdateClassified`'s own base fetch goes through `LoadRegistryByCid`; if that same function verifies, it recurses). Genesis-root termination case at :1591-1610 (empty `prev_registry_cid` + `genesis_authority_` signature):
```cpp
auto base_registry_result = LoadRegistryByCid( prev_registry_cid );   // :1581 — must stay RAW
if ( base_registry_result.has_error() )
{
    return UpdateVerification::kMissingDependency;                    // retryable, not invalid
}
```

**Cap precedent for D-04** — `ValidatorRegistry.hpp:66-68` (`static constexpr size_t DefaultMaxNewValidatorsPerUpdate = 10;`) and enforcement in `InsertNewValidators`, `ValidatorRegistry.cpp:2131-2138`:
```cpp
for ( const auto &validator_id : new_ids )
{
    if ( added >= max_new_validators_per_update_ )
    {
        logger_->debug( "{}: new validator cap reached {}", __func__, max_new_validators_per_update_ );
        break;
    }
```
Insertion shape (REGULAR/ACTIVE) at :2143-2147: `entry->set_role( Role::REGULAR ); entry->set_status( Status::ACTIVE );`

---

### `src/blockchain/Consensus.{hpp,cpp}` (service, request-response)

**Analog:** the same file's durable-record probe, verdict memo, and cached loader.

**Grandfather probe precedent (D-01)** — `Consensus.cpp:2735-2753` (`SubmitCertificate` already reads the canonical slot record before deciding; the new probe copies exactly this read with `GetExpectedCertificateSlotKey`, :3571-3578):
```cpp
auto existing = db_->Get( { key } );
if ( existing.has_value() )
{
    Certificate existing_certificate;
    if ( !existing_certificate.ParseFromArray( existing.value().data(), existing.value().size() ) ||
         !ValidateCertificateKey( existing_certificate, key ) ||
         ValidateCertificate( existing_certificate ) != Check::Approve )
    {
        return outcome::failure( std::errc::invalid_argument );
    }
    if ( SerializedCertificateHash( existing.value().toString() ) < SerializedCertificateHash( serialized ) )
    {
        return outcome::success();     // durable record already final
    }
}
```
Finality contract wording to copy into the new probe's Doxygen — `Consensus.hpp:650`: "A successful durable readback proves finality". Working proof at `Consensus.cpp:4683-4695`.

**The Check verdict enum (do not create a new one)** — `Consensus.hpp:116-122`:
```cpp
enum class Check
{
    Approve, ///< Object is approved
    Reject,  ///< Object is rejected
    Pending, ///< Object evaluation is pending
    Stalled  ///< Object evaluation is stalled
};
```

**Verified-only cache to convert** — `Consensus.cpp:2589-2614` (`LoadRegistryByCidCached`; today calls raw `registry_->LoadRegistryByCid`; note clear-all-at-cap eviction the research flags for resizing) and its contract at `Consensus.hpp:1243-1253`:
```cpp
auto loaded = registry_->LoadRegistryByCid( cid );   // ← becomes the verified loader
if ( loaded.has_error() ) { return loaded; }
{
    std::lock_guard lock( registry_cache_mutex_ );
    if ( registry_cache_.size() >= kRegistryCacheMaxEntries ) { registry_cache_.clear(); }
    registry_cache_.emplace( cid, loaded.value() );
}
```

**Verdict memo discipline (Pitfall 1 — never memoize retryable)** — `Consensus.cpp:1362-1393`:
```cpp
const auto validation = ValidateCertificate( certificate );
if ( validation == Check::Pending || validation == Check::Stalled )
{
    return outcome::failure( std::errc::resource_unavailable_try_again );   // NOT cached
}
const bool accepted = validation == Check::Approve;
{
    std::lock_guard cache_lock( certificate_verdict_cache_mutex_ );
    certificate_verdict_cache_[slot_key] = CertificateVerdict{ content_digest, accepted };
}
```

**Registry-dependent Stalled precedent in ValidateCertificate** — `Consensus.cpp:3513-3521` (raw load site the strict path replaces) and Stall-then-retry filter semantics at `Consensus.cpp:3359-3373`. Equivocation log-only branch for the TRUST-02 audit: `Consensus.cpp:3406-3416`.

**Chain-id classification to extend, not fork (MINT-02)** — `Consensus.cpp:57-70`:
```cpp
bool IsPublicChainMintChainId( std::string_view chain_id )
{
    if ( chain_id.empty() ) { return false; }
    if ( chain_id == "public" ) { return true; }
    return std::all_of( chain_id.begin(), chain_id.end(),
                        []( unsigned char c ) { return c >= '0' && c <= '9'; } );
}
```

---

### NEW bounded-defer helper (utility, event-driven)

**Analogs:** bounded FIFO with oldest-first eviction (`PublicChainInputValidator.cpp`) + durable retry (`CRDTWorkJournal`). The helper is "evidence-FIFO shape + deadline"; the journal stays the durable backstop.

**FIFO + eviction core to copy** — `PublicChainInputValidator.cpp:264-299`:
```cpp
void PublicChainInputValidator::StoreEvidence( const std::string       &claim_key,
                                               RpcVerificationEvidence  evidence ) const
{
    std::lock_guard lock( evidence_mutex_ );
    if ( evidence_by_claim_.emplace( claim_key, std::move( evidence ) ).second )
    {
        evidence_order_.push_back( claim_key );
    }
    else
    {
        evidence_by_claim_[claim_key] = std::move( evidence );  // replace stale in place
    }

    while ( evidence_order_.size() > kMaxCachedEvidence )       // kMaxCachedEvidence = 256
    {
        evidence_by_claim_.erase( evidence_order_.front() );
        evidence_order_.pop_front();                            // oldest-first under pressure
    }
}
```
(`TakeEvidence` at :285-299 shows the paired erase-from-both-containers removal.)

**Durable retry semantics to delegate to (do not duplicate)** — `crdt_work_journal.hpp:34-115` (`MarkSeen` / `MarkProcessing` / `MarkStalled(key, lease)` / `MarkDone` / `ListUnfinished(pattern)` / `RecoverStaleProcessing(pattern, stale)`) with the usage loop at `Consensus.cpp:4564-4627` (compiled-once `std::regex PATTERN`, `RecoverStaleProcessing(PATTERN, std::chrono::seconds(15))`, `Stalled → MarkStalled(key, 0ms)` keep-alive, exponential backoff clamp at :4707-4717).

**Deadline/TTL precedent for the map fields** — `Consensus.hpp:1237-1242` (pending-vote queue bound + TTL constants):
```cpp
static constexpr size_t                kMaxTrackedPendingVoteQueues{ 10000 };
static constexpr std::chrono::minutes  kPendingVoteQueueTTL{ 30 };
```

---

### `src/transaction/TransactionConsensusHandler.cpp` (controller, request-response)

**Analog:** the same file's verdict chain — add the v1-mint gate as one more `Check` before `Reject`, exactly like the existing rules.

**Verdict chain to extend** — `TransactionConsensusHandler.cpp:515-554`:
```cpp
//TODO - Deal with checking the Mint                                    // :545 — this phase's insertion point
if ( !CheckTransactionTypeRules( tx ) )
{
    logger_->error( "{}: Type rules failed tx={}", __func__, tx.GetHash() );
    return ConsensusManager::ValidationResult::Reject();
}
```
Each check is: boolean predicate → `logger_->error` naming the rule → `ValidationResult::Reject()`. The new gate follows this exact shape (type == "mint" → Reject, with the Pitfall-5 migration decision applied).

**`CheckTransactionTypeRules` today** — `TransactionConsensusHandler.cpp:801-819` (returns `true` for any tx without UTXO parameters — the MINT-03 gap):
```cpp
bool TransactionConsensusHandler::CheckTransactionTypeRules( const GeniusTransaction &tx ) const
{
    if ( tx.HasUTXOParameters() )
    {
        auto params_opt = tx.GetUTXOParametersOpt();
        ...
        const auto &[_, validator] = owner_.SelectInputValidator( tx );
        return validator.ValidateUTXOParameters( ... );
    }
    return true;    // ← self-signed v1 mint slips through here
}
```

**Constructor-injection shape (the live BridgeConsensusAdapter successor)** — `TransactionConsensusHandler.hpp:57-61`:
```cpp
TransactionConsensusHandler( TransactionManager &owner, base::Logger logger ) :
    owner_( owner ), logger_( std::move( logger ) )
```
If the v1-mint rule needs state (defer deadlines), prefer a small injected class over growing `TransactionManager` (CONCERNS.md god-file guidance; `TransactionManager::New`'s DI list at `TransactionManager.cpp:124-134` is the wiring analog).

---

### `src/transaction/TransactionManager.{hpp,cpp}` (middleware, event-driven)

**Analog:** the same file's ingress surfaces.

**Sync filter (`FilterTransaction`)** — `TransactionManager.cpp:3772-3832`. Signature-valid tx passes (`CheckTransactionAuthorization` at :3788); failure → tombstone proof-key cleanup. A certificate-conditional v1 gate here must return the filter's Stall-equivalent or keep the do/while + `should_delete` structure:
```cpp
do
{
    auto maybe_new_tx = DeSerializeTransaction( element.value() );
    if ( maybe_new_tx.has_error() ) { ...; break; }
    new_tx = maybe_new_tx.value();
    if ( !CheckTransactionAuthorization( *new_tx ) ) { ...; break; }
    if ( KeyExistsInDB( GetTransactionPath( *new_tx ) ) ) { ...; break; }
    should_delete = false;
} while ( 0 );
```

**Submission preflight loop (RPC/local ingress)** — `TransactionManager.cpp:2019-2055`: per-transaction checks before any CRDT Put (size limit → `std::errc::message_size`; missing commitment → `std::errc::invalid_argument`; `SelectInputValidator` + witness). The v1-mint preflight rejection slots in as the first per-tx check with a named `std::errc`.

**Type dispatch map (do not fork)** — `TransactionManager.cpp:107-122` (`transaction_parsers`: `"mint"`, `"mint-v2"`, `"migration"` all share `ParseMintTransaction` — admission gating must NOT touch this map; deserialization stays open per D-02).

---

### `src/account/PublicChainInputValidator.{hpp,cpp}` (service, request-response)

**Analog:** the same file — every edit is surgical inside `GatherVerificationEvidence` (:307-490) and the evidence struct.

**Fail-open branches to close (MINT-02)** — `PublicChainInputValidator.cpp:319-334`:
```cpp
if ( source_reference.empty() )
{
    // Nothing to verify against a public chain: the claim is accepted but
    // no endpoint confirmed anything, so every slot stays empty.
    logger->debug( "GatherVerificationEvidence skipped because source reference is empty" );
    evidence.valid = true;                       // ← becomes per-type reject
    return evidence;
}
const auto chain_id = tx.GetChainId();
if ( chain_id.empty() || chain_id == "supergenius" )
{
    logger->debug( "GatherVerificationEvidence bypassed for local chain_id={}", chain_id );
    evidence.valid = true;                       // ← becomes type-authorized path
    return evidence;
}
```

**Receipt check site MINT-04 extends (status + contract + topic0 only today)** — `PublicChainInputValidator.cpp:400-433`: reverted-receipt discards evidence (`return RpcVerificationEvidence{}` at :407), then the loop matching `ep.bridge_contract_address` + `ep.accepted_topic0_hashes` (:412-433). The decode+bind lands inside this loop's matched-log branch. Include pattern: PCIV already includes `<eth/json_rpc.hpp>` (:17); adding `<eth/abi_decoder.hpp>` is the same linkage.

**Evidence struct to extend (one fact, one field — Phase 24 consumes the same decode)** — `PublicChainInputValidator.hpp:65-92` (`RpcVerificationEvidence` with `SlotHash(size_t)`), endpoint config `WeightedRpcEndpoint` at :40-49 (`accepted_topic0_hashes` carries BOTH v1+v2 topic0).

**DI seam for adversarial tests** — `SetTransportFactory`, `PublicChainInputValidator.hpp:196-207`, resolved in `GatherVerificationEvidence` at `PublicChainInputValidator.cpp:351-357`.

**Validator verdict contract (MINT-02 must map to it, not bypass it)** — `InputValidators.hpp:54-60` (`WitnessVerdict`: `kValid` / `kNotSynced` (retryable) / `kInvalid`).

---

### `src/account/MintTransactionV2.{hpp,cpp}` (model, transform)

**Analog:** its own slot identity — the one home for the burn-fact tuple. The MINT-04 comparison reads these facts; it never recomputes them.

**Canonical identity** — `MintTransactionV2.cpp:212-234`:
```cpp
std::string MintTransactionV2::GetSlotID() const
{
    static constexpr std::string_view kPrefix    = "mint-v2:";
    static constexpr std::string_view kSeparator = ":";
    std::string key( kPrefix );
    key += chain_id_;
    key += kSeparator;
    key += token_id_.ToHex();
    key += kSeparator;
    key += std::to_string( GetAmount() );
    if ( !utxo_params_.second.empty() ) { key += kSeparator; key += utxo_params_.second.front().dest_address; }
    if ( !utxo_params_.first.empty() )  { key += kSeparator; key += utxo_params_.first.front().txid_hash_.toReadableString(); }
    return key;
}
```
If the comparison needs the fields separately, add named accessors here (one home) rather than re-parsing the string at the comparison site.

---

### `src/account/GeniusNode.{hpp,cpp}` (config, file-I/O)

**Analog:** the same file's config loaders — two distinct patterns for two different files.

**network_config.json (D-03 window, D-04 cap)** — `GeniusNode.cpp:1621-1686`: missing file = documented public defaults; present-but-corrupt = fatal (`settings.valid = false`, :1646-1654); the `read`/`read_seconds` optional-key lambdas (:1656-1686) with FindMember one-lookup and ill-typed-keeps-default semantics:
```cpp
auto read = [&]( const char *key, auto &out )
{
    using T           = std::decay_t<decltype( out )>;
    const auto member = config_json.FindMember( key );
    if ( member == config_json.MemberEnd() ) { return; }
    ...
};
auto read_seconds = [&]( const char *key, std::chrono::seconds &out )
{
    auto seconds = static_cast<int>( out.count() );
    read( key, seconds );
    out = std::chrono::seconds( seconds );
};
read( "high_water", settings.high_water );   // call-site shape
```
Fail-closed escalation precedent for the new keys: `GeniusNode.cpp:1716-1753` (malformed identity → `settings.valid = false`, only key names in errors).

**sgns_config.json trust fields (folded todo)** — `GeniusNode.cpp:490-534` (HasMember+IsX parse of `trusted_peers` / `bootstrapper_node` / quorum thresholds, quorum floors from `securecrdt`), TrustStartupController wiring + `FATAL_TRUST_MISMATCH` at :927-959. Pinned-fingerprint source string already exists unread: `example/node_test/sgns_config.json:4` (`_trust_configuration_notice` carries ceremony fingerprint `4f6840f5...`). GenesisManifest owns canonicalization (`GenesisManifest::Fingerprint()`/`DecodeAndVerify` — do not duplicate hashing).

---

### Test files (test)

**Multi-node integration apparatus (TRUST-01 compat / MINT-03 compat)** — `test/src/blockchain/multi_node_finality_fault_test.cpp`: includes block :1-46; `...TestAccess` friend-class pattern exposing private members :50-69; already exercises `LoadRegistryByCid` at :1878 (`network.first.blockchain->GetValidatorRegistry()->LoadRegistryByCid( proposal.value().registry_cid() )`). CMake gating pattern (APPLE + CMake ≥3.29 + `set_tests_properties(... TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK phase12_real_socket_ports)`) at `test/src/blockchain/CMakeLists.txt:112-122`.

**Registry unit tests (TRUST-01/02, cap enforcement)** — `addtest` + `target_link_libraries` entries `test/src/blockchain/CMakeLists.txt:35-59` (`validator_registry_slot_quorum_test`, `validator_registry_promotion_test` — pure static helpers, no GlobalDB wiring; comment at :51-53 explains the pattern). `consensus_certificate_test` is commented out at :14-21 — planner decides re-enable vs multi-node apparatus (Pitfall 9).

**Mock-RPC unit tests (MINT-02/04 adversarial)** — `test/src/bridge_e2e/bridge_e2e_chainlist_test.cpp:295-343`: `validator.SetTransportFactory([&](url, timeout){ return std::make_unique<FixedReceiptTransport>(BuildValidReceiptJson(source_ref, contract, topic0)); })` + v1 mint construction `MintTransaction::New( 1, "11155111", TokenID::FromBytes({0x00}), dag )` + negative-control discipline (bogus topic0 "the fix must not weaken receipt-log validation"). Mismatched-facts adversarial receipts extend `BuildValidReceiptJson` the same way.

**Wait discipline** — `test/testutil/wait_condition.hpp:101-146`: `ASSERT_WAIT_FOR_CONDITION(condition, timeout, description, actual_duration)` / `EXPECT_WAIT_FOR_CONDITION(...)`; never `sleep_for`.

**Spendability compat proof (MINT-03)** — cite `GeniusInputValidator.cpp:496-512`: producer certificate missing → `kNotSynced` (unspendable-but-retryable), the existing mechanism that keeps uncertified stored v1 inert.

**Migration trap fixture (Pitfall 5)** — `Migration3_4_0To3_5_0.cpp:368-377`: `MintTransaction::New( 0, std::to_string(network_id), token_id, std::move(dag) )` — zero-value v1 mints as nonce fillers; the MINT-03 exemption decision must have a compat test proving this path survives.

## Shared Patterns

### Verdict model: per-module enums + Stall, never a god-enum (D-03)
**Sources:** `Consensus.hpp:116-122` (`Check`), `ValidatorRegistry.hpp:311-316` (`UpdateVerification`), `InputValidators.hpp:54-60` (`WitnessVerdict`), `ValidatorRegistry.cpp:1467-1486` (kMissingDependency→`ElementFilterResult::Stall()`), `Consensus.cpp:3359-3373` (Check::Stalled→Stall with the why-comment).
**Apply to:** every new authorization boundary this phase. Mapping rule: missing dependency → retryable verdict (Stalled/kNotSynced/kMissingDependency), never memoized (`Consensus.cpp:1383-1387`); only deterministic unauthorized → Reject.

### Error handling: `outcome::result<T>` with named failures
**Sources:** `ValidatorRegistry.cpp:785-820` (`outcome::failure(std::errc::no_such_file_or_directory)`), `TransactionManager.cpp:2030/2041` (named `std::errc` per rule), `Consensus.cpp:1386` (`resource_unavailable_try_again` = retryable convention).
**Apply to:** all new fallible paths; log names the failing rule (`logger_->error("{}: ... failed ...", __func__, ...)` house style).

### Config: config-wins-with-safe-defaults, fail-closed on corrupt
**Source:** `GeniusNode.cpp:1621-1694` (`read`/`read_seconds` lambdas, defaults in `NetworkSettings`, corrupt-file fatal), Doxygen on port-resolution precedent.
**Apply to:** D-03 deferral window (~30 min default + test override) and D-04 admission cap as new `read`/`read_seconds` call-sites beside the watermark/reconnect knobs.

### Logging
**Source:** per-file static logger factories (`PublicChainInputValidator.cpp:35-39` `base::createLogger("InputValidator")`; `ValidatorRegistryLogger()` / `ConsensusManagerLogger()` same shape).
**Apply to:** any new helper file gets the same anonymous-namespace logger factory; trace on entry (`"{}: entry ..."`), error names the rule, never log key values (D-03 privacy precedent, `GeniusNode.cpp:1714-1715` comment).

### Retry backstop: CRDTWorkJournal, not new queues
**Source:** `crdt_work_journal.hpp:34-115` + usage `Consensus.cpp:4564-4627` (compiled-once regex, 15s recovery, MarkStalled(0ms) keep-alive, exponential backoff with clamp :4707-4717).
**Apply to:** any delta-stall this phase introduces; the new bounded-defer helper is the *in-memory* bound (deadline + FIFO eviction), the journal is the durable mechanism — do not rebuild either.

### Test discipline: adversarial + compat pairs, wait macros, DI mocks
**Sources:** `bridge_e2e_chainlist_test.cpp:295-343` (negative control + mock transport), `multi_node_finality_fault_test.cpp:50-69` (TestAccess friend pattern), `wait_condition.hpp:101-146`, CMake `addtest` at `test/src/blockchain/CMakeLists.txt:2-21/97-122`.
**Apply to:** every fix — named attacker authority → expected rejection, plus the pre-upgrade legitimate twin (restart/sync/migration survival).

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| TRUST-02 audit document (`AgentDocs/` or `.planning/notes/`) | doc | n/a | Documentation deliverable; existing notes under `.planning/notes/` are the format precedent (e.g. `security-assessment-2026-10-response.md`), no code analog applicable |

Everything else has an in-tree analog — this phase is routing/gating/binding, not new machinery (per RESEARCH.md "Don't Hand-Roll" table).

## Metadata

**Analog search scope:** `src/blockchain`, `src/transaction`, `src/account`, `src/crdt`, `src/migration`, `src/trustedpeer`, `evmrelay/include/eth`, `test/src/{blockchain,account,bridge_e2e}`, `test/testutil`, `example/node_test`
**Files scanned:** ~25 read in targeted ranges (Consensus.cpp 4773 ln, ValidatorRegistry.cpp 2610 ln, TransactionManager.cpp 5037 ln, GeniusNode.cpp 5305 ln read via targeted offsets only)
**Pattern extraction date:** 2026-10-19
**Drift watch:** Consensus.cpp and ValidatorRegistry.cpp are the two highest-drift files (RESEARCH.md warning) — planner should re-grep anchor symbols (`LoadRegistryByCid`, `VerifyUpdateClassified`, `ValidateCertificate`) if develop moves before implementation.
