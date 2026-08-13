/**
 * @file       TrustStateStore.hpp
 * @brief      Crash-safe local authority for confirmed trust state.
 */
#ifndef SUPERGENIUS_TRUST_STATE_STORE_HPP
#define SUPERGENIUS_TRUST_STATE_STORE_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/buffer.hpp"
#include "multisig/MultiSig.hpp"
#include "outcome/outcome.hpp"
#include "trustedpeer/GenesisManifest.hpp"
#include "trustedpeer/QuorumPolicy.hpp"

namespace sgns::storage
{
    class rocksdb;
}

namespace sgns::trustedpeer
{
    struct ConfirmedBurnState
    {
        static constexpr uint8_t ENCODING_VERSION = 1;

        uint8_t     encoding_version = ENCODING_VERSION;
        uint16_t    network_id       = 0;
        uint64_t    version          = 0;
        std::string expected_previous_hash;
        std::string authorizing_policy_hash;
        uint64_t    basis_points = 0;

        [[nodiscard]] std::optional<std::vector<uint8_t>>      CanonicalBytes() const;
        [[nodiscard]] std::optional<std::string>               Hash() const;
        [[nodiscard]] static std::optional<ConfirmedBurnState> DecodeCanonical( const std::vector<uint8_t> &bytes );
        bool                                                   operator==( const ConfirmedBurnState &other ) const;
    };

    enum class BurnAuthorizationKind : uint8_t
    {
        BootstrapOnly = 0,
        PeerQuorum,
    };

    struct ConfirmedTrustSnapshot
    {
        GenesisManifest               genesis;
        std::string                   genesis_fingerprint;
        std::vector<uint8_t>          bootstrap_signature;
        QuorumPolicyState             policy;
        multisig::CollectedSignatures policy_proof;
        ConfirmedBurnState            burn;
        multisig::CollectedSignatures burn_proof;
        BurnAuthorizationKind         burn_authorization = BurnAuthorizationKind::BootstrapOnly;

        bool operator==( const ConfirmedTrustSnapshot &other ) const;
    };

    /**
     * @brief Synchronous, network-scoped last-known-good trust store.
     *
     * A successful commit is the prerequisite for publishing the returned
     * snapshot to any cache or observer. Corrupt/partial local state, process
     * crashes, CRDT rollback/forks, and concurrent local candidates are
     * detected while this database high-water mark remains intact.
     *
     * A host administrator who restores this database and every local anchor
     * to an older, internally valid snapshot is outside the software-only
     * guarantee. Detecting that requires a TPM, OS-keystore monotonic counter,
     * or off-host checkpoint.
     */
    class TrustStateStore : public std::enable_shared_from_this<TrustStateStore>
    {
    public:
        enum class LoadStage : uint8_t
        {
            PolicyHistoryVerifiedBeforeBurnHead = 0,
        };

        enum class Error : uint8_t
        {
            NOT_FOUND = 0,
            ALREADY_INITIALIZED,
            NETWORK_MISMATCH,
            CORRUPT_GENESIS,
            CORRUPT_FINGERPRINT,
            INVALID_GENESIS_PROOF,
            MISSING_POLICY_RECORD,
            MISSING_BURN_RECORD,
            CORRUPT_POLICY_RECORD,
            CORRUPT_BURN_RECORD,
            INVALID_POLICY_PROOF,
            INVALID_BURN_PROOF,
            VERSION_DECREASE,
            VERSION_SKIP,
            WRONG_PREDECESSOR,
            WRONG_AUTHORIZER,
            INITIAL_BURN_NOT_CONFIRMED,
            STALE_HEAD,
            COMMIT_FAILED,
        };

        using Write          = std::pair<base::Buffer, base::Buffer>;
        using BatchCommitter = std::function<outcome::result<void>( storage::rocksdb &, const std::vector<Write> & )>;
        using LoadObserver   = std::function<void( LoadStage )>;

        static outcome::result<std::shared_ptr<TrustStateStore>> Open( const std::string &path,
                                                                       uint16_t           network_id,
                                                                       BatchCommitter     committer = {},
                                                                       LoadObserver       load_observer = {} );

        outcome::result<ConfirmedTrustSnapshot> LoadAndVerify() const;
        outcome::result<ConfirmedTrustSnapshot> CommitGenesis( const GenesisManifest      &manifest,
                                                               const std::vector<uint8_t> &bootstrap_signature,
                                                               const std::vector<uint8_t> &authorization_bytes = {} );
        outcome::result<ConfirmedTrustSnapshot> CommitPolicySuccessor(
            const QuorumPolicyState             &candidate,
            const multisig::CollectedSignatures &proof,
            const std::vector<uint8_t>          &authorization_bytes = {} );
        outcome::result<ConfirmedTrustSnapshot> CommitBurnSuccessor(
            const ConfirmedBurnState            &candidate,
            const multisig::CollectedSignatures &proof,
            const std::vector<uint8_t>          &authorization_bytes = {} );

    private:
        TrustStateStore( std::shared_ptr<storage::rocksdb> database,
                         uint16_t                          network_id,
                         BatchCommitter                    committer,
                         LoadObserver                      load_observer );

        outcome::result<ConfirmedTrustSnapshot> LoadAndVerifyUnlocked() const;
        outcome::result<void> CommitWrites( const std::vector<Write> &writes );

        std::shared_ptr<storage::rocksdb> database_;
        uint16_t                          network_id_ = 0;
        BatchCommitter                    committer_;
        LoadObserver                      load_observer_;
        mutable std::mutex                transition_mutex_;
    };

    /** Domain-separated predecessor used by the deterministic burn v1 candidate. */
    [[nodiscard]] std::string BurnGenesisAnchorHash( const std::string &genesis_fingerprint );
} // namespace sgns::trustedpeer

OUTCOME_HPP_DECLARE_ERROR_2( sgns::trustedpeer, TrustStateStore::Error );

#endif // SUPERGENIUS_TRUST_STATE_STORE_HPP
