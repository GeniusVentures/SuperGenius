/**
 * @file       TrustedPeerRegistry.hpp
 * @brief      Genesis-seeded, quorum-updatable trusted-peer set built entirely
 *             on top of Phase 9's SecureCrdt/SecureCrdtRegistry/ISignedCRDTData
 *             machinery. This is the first real (non-test) consumer of the
 *             SecureCRDT layer, and the signer-set-source dependency Phase 11
 *             (BURN_BASIS_POINTS) will build on (TPR-01, TPR-02, TPR-03).
 * @date       2026-07-24
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_TRUSTEDPEER_TRUSTEDPEERREGISTRY_HPP
#define SGNS_TRUSTEDPEER_TRUSTEDPEERREGISTRY_HPP

#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "base/logger.hpp"
#include "crdt/hierarchical_key.hpp"
#include "outcome/outcome.hpp"
#include "securecrdt/ISignedCRDTData.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/SecureCrdtRegistry.hpp"

namespace sgns::trustedpeer
{
    /**
     * @brief ISignedCRDTData payload type carrying the trusted-peer list.
     *        Serialization is a newline-joined encoding of the peer address
     *        list (addresses are 128-char hex strings and never contain '\n').
     *        Verify() performs structural validation ONLY (non-empty, no
     *        duplicates, each entry exactly 128 lowercase-hex characters) --
     *        it never diffs against any cached/mutable state (Pitfall 4).
     */
    class TrustedPeerListPayload : public sgns::securecrdt::ISignedCRDTData
    {
    public:
        TrustedPeerListPayload() = default;

        /**
         * @brief Constructs a payload directly from a peer list (used by
         *        callers that need to serialize a proposed value).
         * @param[in] peers Peer address list.
         */
        explicit TrustedPeerListPayload( std::vector<std::string> peers );

        std::vector<uint8_t> SerializeToBytes() const override;
        bool                 DeserializeFromBytes( const std::vector<uint8_t> &bytes ) override;
        bool                 Verify( const std::vector<uint8_t> &payload ) const override;
        void                 Apply() override;

        /**
         * @brief Returns the decoded/constructed peer list.
         * @return Peer address list.
         */
        const std::vector<std::string> &GetPeers() const
        {
            return peers_;
        }

    private:
        std::vector<std::string> peers_;
    };

    /**
     * @brief Genesis-seeded, in-memory-cached, quorum-updatable trusted-peer
     *        set. Delegates ALL signature/quorum logic to SecureCrdt /
     *        SecureCrdtRegistry -- no bespoke signature/quorum logic exists
     *        here (TPR-03).
     */
    class TrustedPeerRegistry : public std::enable_shared_from_this<TrustedPeerRegistry>
    {
    public:
        /**
         * @brief Constructs a TrustedPeerRegistry. The genesis peer list is
         *        cached immediately (D-05) -- GetCurrentPeers() reflects it
         *        even before SeedGenesis/TryConfirm are called.
         * @param[in] secure_crdt SecureCrdt wrapper to delegate propose/sign/
         *            quorum operations to.
         * @param[in] genesis_peers Genesis trusted-peer address list (non-empty).
         * @param[in] bootstrapper_address Sole ephemeral genesis signer address.
         * @param[in] quorum_threshold Quorum threshold applied once genesis is
         *            confirmed (i.e. for membership-change proposals).
         * @param[in] base_key Registered CRDT key this instance owns.
         */
        TrustedPeerRegistry( std::shared_ptr<sgns::securecrdt::SecureCrdt> secure_crdt,
                             std::vector<std::string>                      genesis_peers,
                             std::string                                   bootstrapper_address,
                             uint64_t                                      quorum_threshold,
                             sgns::crdt::HierarchicalKey                   base_key );

        ~TrustedPeerRegistry();

        /**
         * @brief Constructs a TrustedPeerRegistry and registers its
         *        signer-set-source with SecureCrdtRegistry.
         * @param[in] secure_crdt SecureCrdt wrapper to delegate propose/sign/
         *            quorum operations to.
         * @param[in] genesis_peers Genesis trusted-peer address list (non-empty).
         * @param[in] bootstrapper_address Sole ephemeral genesis signer address.
         * @param[in] quorum_threshold Quorum threshold applied once genesis is
         *            confirmed.
         * @param[in] base_key Registered CRDT key this instance owns.
         * @return outcome::success(instance) with the newly-constructed,
         *         registered TrustedPeerRegistry instance, or
         *         outcome::failure(SecureCrdt::Error::QUORUM_THRESHOLD_BELOW_FLOOR)
         *         if `quorum_threshold` is below the majority-safety floor for
         *         `genesis_peers.size()` (D-07) -- construction fails, no
         *         instance is produced.
         */
        static outcome::result<std::shared_ptr<TrustedPeerRegistry>> New(
            std::shared_ptr<sgns::securecrdt::SecureCrdt> secure_crdt,
            std::vector<std::string>                      genesis_peers,
            std::string                                    bootstrapper_address,
            uint64_t                                        quorum_threshold,
            sgns::crdt::HierarchicalKey                     base_key =
                sgns::crdt::HierarchicalKey( "trusted-peer-registry" ) );

        /**
         * @brief Seeds the genesis trusted-peer list: proposes the genesis
         *        payload then adds exactly one ephemeral-bootstrapper
         *        signature. The signature is PRECOMPUTED -- this method never
         *        signs live (D-02).
         * @param[in] genesis_peers Genesis trusted-peer address list.
         * @param[in] ephemeral_signature Precomputed bootstrapper signature
         *            over the genesis payload.
         * @return outcome::success on success, or the first failing
         *         SecureCrdt call's error.
         */
        outcome::result<void> SeedGenesis( const std::vector<std::string> &genesis_peers,
                                            const std::vector<uint8_t>     &ephemeral_signature );

        /**
         * @brief Proposes a membership-change value (new full peer list).
         * @param[in] new_peers Proposed new full trusted-peer address list.
         * @return outcome::success on success, or the failing SecureCrdt call's error.
         */
        outcome::result<void> ProposeMembershipChange( const std::vector<std::string> &new_peers );

        /**
         * @brief Adds a signature over the currently-proposed value.
         * @param[in] signer_address Address claimed to have produced `signature`.
         * @param[in] signature Raw signature bytes.
         * @return outcome::success on success, or the failing SecureCrdt call's error.
         */
        outcome::result<void> SignMembershipChange( const std::string          &signer_address,
                                                     const std::vector<uint8_t> &signature );

        /**
         * @brief Attempts to confirm the currently-proposed value against
         *        quorum. On confirmation, decodes/verifies/applies the
         *        payload and overwrites the cached peer set -- never
         *        speculatively before quorum is independently confirmed.
         * @return outcome::success(true) if this call newly confirmed a
         *         value, outcome::success(false) if quorum is not yet met,
         *         or a failure if the confirmed payload is malformed/invalid.
         */
        outcome::result<bool> TryConfirm();

        /**
         * @brief Returns a copy of the current cached trusted-peer set.
         * @return Current trusted-peer address list.
         */
        std::vector<std::string> GetCurrentPeers() const;

        /**
         * @brief Reports whether genesis has been confirmed.
         * @return true once TryConfirm has confirmed the genesis value.
         */
        bool IsGenesisConfirmed() const;

        /**
         * @brief Unregisters this instance's signer-set-source from
         *        SecureCrdtRegistry (test-fixture teardown helper).
         */
        void Unregister();

    private:
        /**
         * @brief Registers this instance's signer-set-source with
         *        SecureCrdtRegistry under "trusted-peer-registry".
         */
        void RegisterSignerSetSource();

        /**
         * @brief Resolves the current authorized signer set: the sole
         *        bootstrapper (threshold 1) before genesis confirmation, or
         *        the current cached peer set (at quorum_threshold_)
         *        afterwards. Reads ONLY cached_peers_/genesis_confirmed_ --
         *        NEVER re-enters the SecureCrdt quorum-read path (Pitfall 2).
         * @return Signer set snapshot for the current state.
         */
        outcome::result<sgns::securecrdt::SignerSetSnapshot> ResolveSignerSet() const;

        std::shared_ptr<sgns::securecrdt::SecureCrdt> secure_crdt_;
        sgns::crdt::HierarchicalKey                   base_key_;
        std::string                                   bootstrapper_address_;
        uint64_t                                       quorum_threshold_;

        mutable std::shared_mutex cache_mutex_;
        std::vector<std::string> cached_peers_;
        bool                      genesis_confirmed_ = false;
        int                       registry_token_    = 0;

        sgns::base::Logger logger_ = sgns::base::createLogger( "TrustedPeerRegistry" );
    };
} // namespace sgns::trustedpeer

#endif // SGNS_TRUSTEDPEER_TRUSTEDPEERREGISTRY_HPP
