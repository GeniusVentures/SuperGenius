/**
 * @file       BurnConfig.hpp
 * @brief      Genesis-seeded, quorum-signed, cache-refresh-via-quorum-
 *             re-derivation `BURN_BASIS_POINTS` value, built entirely on top
 *             of SecureCrdt/SecureCrdtRegistry/TrustedPeerRegistry (BURN-01,
 *             BURN-02, BURN-03). Direct structural template of
 *             TrustedPeerRegistry (Phase 10), replacing
 *             TransactionManager's hardcoded `BURN_BASIS_POINTS` constant.
 * @date       2026-07-24
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_ACCOUNT_BURNCONFIG_HPP
#define SGNS_ACCOUNT_BURNCONFIG_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "base/logger.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/hierarchical_key.hpp"
#include "outcome/outcome.hpp"
#include "securecrdt/ISignedCRDTData.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/SecureCrdtRegistry.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

namespace sgns
{
    class GeniusAccount;
} // namespace sgns

namespace sgns::account
{
    class ConfirmedBurnValueProvider
    {
    public:
        [[nodiscard]] bool IsReady() const;
        [[nodiscard]] uint64_t GetBasisPoints() const;

    private:
        friend class BurnConfig;
        std::atomic<bool> ready_{ false };
        std::atomic<uint64_t> basis_points_{ 0 };
    };

    /**
     * @brief ISignedCRDTData payload type carrying the burn-basis-points
     *        value. Serialization/verification mirrors
     *        `TrustedPeerListPayload`'s structural-only-verify convention
     *        (never diffs against cached state).
     */
    class BurnConfigPayload : public sgns::securecrdt::ISignedCRDTData
    {
    public:
        /// @brief Total basis points representing 100% -- any decoded value
        ///        above this is semantically invalid.
        static constexpr uint64_t BASIS_POINTS_TOTAL = 10000;

        BurnConfigPayload() = default;

        /**
         * @brief Constructs a payload directly from a basis-points value
         *        (used by callers that need to serialize a proposed value).
         * @param[in] basis_points Burn basis-points value.
         */
        explicit BurnConfigPayload( uint64_t basis_points );

        std::vector<uint8_t> SerializeToBytes() const override;
        bool                 DeserializeFromBytes( const std::vector<uint8_t> &bytes ) override;
        bool                 Verify( const std::vector<uint8_t> &payload ) const override;
        void                 Apply() override;

        /**
         * @brief Returns the decoded/constructed basis-points value.
         * @return Basis-points value.
         */
        uint64_t GetBasisPoints() const
        {
            return basis_points_;
        }

    private:
        uint64_t basis_points_ = 0;
        bool     applied_      = false;
    };

    /**
     * @brief Genesis-seeded, in-memory-cached, quorum-updatable
     *        `BURN_BASIS_POINTS` value. Delegates ALL signature/quorum logic
     *        to SecureCrdt/SecureCrdtRegistry -- no bespoke signature/quorum
     *        logic exists here (mirrors TPR-03 precedent).
     */
    class BurnConfig : public std::enable_shared_from_this<BurnConfig>
    {
    public:
        /// @brief Known genesis default burn-basis-points value (BURN-03 pre-quorum fallback).
        static constexpr uint64_t GENESIS_DEFAULT_BASIS_POINTS = 100;

        using RefreshCallback = std::function<void( uint64_t )>;
        using SignCallback = std::function<std::vector<uint8_t>( const std::vector<uint8_t> & )>;

        BurnConfig( std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt,
                    std::shared_ptr<sgns::crdt::GlobalDB>                   db,
                    std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> trusted_peer_registry,
                    uint64_t                                                quorum_threshold,
                    std::shared_ptr<sgns::GeniusAccount>                    account,
                    sgns::crdt::HierarchicalKey                             base_key );

        ~BurnConfig();

        /**
         * @brief Constructs a BurnConfig, registers its signer-set-source and
         *        CRDT change-callback, and auto-seeds the genesis default
         *        exactly once if eligible.
         * @param[in] secure_crdt SecureCrdt wrapper to delegate propose/sign/
         *            quorum operations to.
         * @param[in] db GlobalDB instance (needed directly for
         *            RegisterNewElementCallback).
         * @param[in] trusted_peer_registry Signer-set source (D-05).
         * @param[in] quorum_threshold Quorum threshold to enforce.
         * @param[in] account This node's account (genesis auto-seed signing).
         * @param[in] base_key Registered CRDT key this instance owns.
         * @return outcome::success(instance) on success, or
         *         outcome::failure(SecureCrdt::Error::QUORUM_THRESHOLD_BELOW_FLOOR)
         *         if `quorum_threshold` is below the majority-safety floor
         *         for the current trusted-peer set size (D-07) -- construction
         *         FAILS, no instance is produced.
         */
        static outcome::result<std::shared_ptr<BurnConfig>> New(
            std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt,
            std::shared_ptr<sgns::crdt::GlobalDB>                   db,
            std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> trusted_peer_registry,
            uint64_t                                                quorum_threshold,
            std::shared_ptr<sgns::GeniusAccount>                    account,
            sgns::crdt::HierarchicalKey base_key = sgns::crdt::HierarchicalKey( "burn-config" ) );

        static outcome::result<std::shared_ptr<BurnConfig>> NewProduction(
            std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt,
            std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> trusted_peer_registry,
            std::shared_ptr<sgns::trustedpeer::TrustStateStore>     trust_store,
            std::string                                             local_signer_address,
            SignCallback                                            sign_callback,
            std::string                                             candidate_domain = "burn-config" );

        outcome::result<sgns::securecrdt::CandidateId> OnTrustedPeerGenesisConfirmed();
        outcome::result<std::vector<sgns::securecrdt::CandidateId>> ListPendingBurnCandidates() const;
        outcome::result<sgns::securecrdt::CandidateId> ProposeBurnCandidate( uint64_t basis_points );
        outcome::result<sgns::securecrdt::CandidateId> ApproveBurnCandidate(
            const sgns::securecrdt::CandidateId &candidate_id );
        outcome::result<bool> TryActivateBurnCandidate( const sgns::securecrdt::CandidateId &candidate_id );
        [[nodiscard]] bool IsEconomicallyReady() const;
        [[nodiscard]] std::shared_ptr<const ConfirmedBurnValueProvider> GetConfirmedValueProvider() const;
        [[nodiscard]] static std::optional<sgns::securecrdt::CandidateCore> BurnCandidateCore(
            const sgns::trustedpeer::ConfirmedBurnState &candidate,
            const std::string &domain = "burn-config" );

        /**
         * @brief Returns the currently-cached, quorum-confirmed basis-points
         *        value (relaxed atomic load) -- no live CRDT read.
         * @return Cached basis-points value.
         */
        uint64_t GetCachedBasisPoints() const;

        /**
         * @brief Registers a callback invoked whenever the cached
         *        basis-points value changes as a result of a fresh
         *        quorum-re-derivation.
         * @param[in] cb Callback receiving the new basis-points value.
         */
        void RegisterRefreshCallback( RefreshCallback cb );

        /**
         * @brief Unregisters this instance's signer-set-source from
         *        SecureCrdtRegistry (test-fixture teardown helper).
         */
        void Unregister();

    private:
        /**
         * @brief Registers this instance's signer-set-source with
         *        SecureCrdtRegistry under base_key_.
         */
        bool RegisterSignerSetSource();

        /**
         * @brief Registers the GlobalDB new-element callback that re-derives
         *        quorum on every base_key OR sig/<addr> child element.
         */
        void RegisterCrdtChangeCallback();

        /**
         * @brief Re-runs SecureCrdt::ReadIfQuorum(base_key_) fresh -- NEVER
         *        trusts the callback's positionally-supplied new_data
         *        (Pitfall 2) -- and updates the cache + invokes registered
         *        refresh callbacks if the freshly-confirmed value differs
         *        from the current cache. Never signs anything.
         */
        void OnCrdtElementChanged();

        /**
         * @brief Proposes+signs the KNOWN GENESIS DEFAULT ONLY, exactly once,
         *        iff no value is yet confirmed at base_key_ AND this node's
         *        account address is among the current trusted peers (D-01,
         *        D-02, D-03). Never runs for any other proposed value.
         */
        void TrySeedGenesisIfEligible();

        bool RegisterProductionDomain();
        outcome::result<sgns::securecrdt::CandidateAuthorizationSnapshot> ResolveBurnAuthorization() const;
        outcome::result<sgns::securecrdt::CandidateId> SubmitLocalApproval(
            const sgns::securecrdt::CandidateCore &core );
        void PublishConfirmedBurn( const sgns::trustedpeer::ConfirmedTrustSnapshot &snapshot );

        std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt_;
        std::shared_ptr<sgns::crdt::GlobalDB>                   db_;
        std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> trusted_peer_registry_;
        uint64_t                                                quorum_threshold_;
        std::shared_ptr<sgns::GeniusAccount>                    account_;
        sgns::crdt::HierarchicalKey                             base_key_;

        std::atomic<uint64_t> cached_basis_points_{ GENESIS_DEFAULT_BASIS_POINTS };

        std::mutex                   refresh_callbacks_mutex_;
        std::vector<RefreshCallback> refresh_callbacks_;

        int registry_token_ = 0;

        bool production_mode_ = false;
        std::shared_ptr<sgns::trustedpeer::TrustStateStore> trust_store_;
        std::string local_signer_address_;
        SignCallback sign_callback_;
        std::string candidate_domain_ = "burn-config";
        std::optional<sgns::securecrdt::CandidateId> automatic_genesis_candidate_;
        std::shared_ptr<ConfirmedBurnValueProvider> confirmed_value_provider_ =
            std::make_shared<ConfirmedBurnValueProvider>();

        sgns::base::Logger logger_ = sgns::base::createLogger( "BurnConfig" );
    };
} // namespace sgns::account

#endif // SGNS_ACCOUNT_BURNCONFIG_HPP
