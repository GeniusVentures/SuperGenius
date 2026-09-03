/**
 * @file       NetworkRegistry.hpp
 * @brief      SecureCRDT-backed per-privateNetworkId membership registry (D-06).
 *             A child trust domain whose bootstrap record is signed by a
 *             majority of the GLOBAL TrustedPeerRegistry and which, once
 *             confirmed, governs itself from its own cached member set and
 *             quorum -- a single member can never admit itself unilaterally.
 *             Built entirely on the existing SecureCrdt/SecureCrdtRegistry/
 *             ISignedCRDTData machinery (no bespoke signature protocol),
 *             mirroring the TrustedPeerRegistry lifecycle (Pattern 3).
 *             Serialized records carry ONLY non-secret metadata (D-03): the
 *             libp2p PeerId allow-list (consumed by the 15-05 connection
 *             gater), the member signing addresses, and key version/
 *             fingerprint -- never raw private-network credential material.
 * @date       2026-09-01
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_NETWORKREGISTRY_NETWORKREGISTRY_HPP
#define SGNS_NETWORKREGISTRY_NETWORKREGISTRY_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "base/logger.hpp"
#include "crdt/hierarchical_key.hpp"
#include "outcome/outcome.hpp"
#include "peerregistry/PeerRegistry.hpp"
#include "securecrdt/ISignedCRDTData.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/SecureCrdtRegistry.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

namespace sgns::crdt
{
    class GlobalDB; // change-callback registration only (BurnConfig pattern)
} // namespace sgns::crdt

namespace sgns::networkregistry
{
    /**
     * @brief ISignedCRDTData payload type carrying a private network's
     *        membership record. Serialization is an explicit line-based field
     *        layout (see NetworkRegistry.cpp); every field is non-secret
     *        metadata (D-03) -- the raw pnet credential is NEVER stored.
     *
     *        Two peer-id views are carried because the two consumers need
     *        disjoint identity spaces: `network_peers` entries are libp2p
     *        PeerId base58 strings (so the 15-05 connection gater can compare
     *        remote_peer.toBase58() directly), while `network_signers`
     *        entries are 128-hex member account addresses (the only identity
     *        space SecureCrdt/multisig can verify signatures against --
     *        ResolveLegacySignerSnapshot rejects non-hex signer sets).
     *
     *        Verify() performs structural validation ONLY (non-empty unique
     *        PeerId-prefixed peer list, unique well-formed signer addresses,
     *        version >= 1) -- it never diffs against any cached/mutable
     *        state (TrustedPeerListPayload convention).
     */
    class NetworkMembershipPayload : public sgns::securecrdt::ISignedCRDTData
    {
    public:
        /// @brief Wire-layout magic/version line every serialized record starts with.
        static constexpr std::string_view MAGIC = "SGNS-NETREG-1";

        NetworkMembershipPayload() = default;

        /**
         * @brief Constructs a payload directly from its fields (used by
         *        callers that need to serialize a proposed value).
         * @param[in] peers libp2p PeerId base58 membership list.
         * @param[in] signers Member account addresses (128-hex).
         * @param[in] pnet_key_version Offline credential version (informational).
         * @param[in] pnet_key_fingerprint Short hex fingerprint of the
         *            credential (optional, never the credential itself).
         */
        NetworkMembershipPayload( std::vector<std::string> peers,
                                  std::vector<std::string> signers,
                                  uint32_t                pnet_key_version = 1,
                                  std::string             pnet_key_fingerprint = {} );

        /**
         * @brief Decodes a membership record into a fully-constructed payload.
         * @param[in] bytes Raw record bytes.
         * @return The decoded payload, or std::nullopt if `bytes` is malformed.
         */
        static std::optional<NetworkMembershipPayload> FromBytes( const std::vector<uint8_t> &bytes );

        std::vector<uint8_t> SerializeToBytes() const override;
        /// @note Prefer FromBytes() in concrete call sites. This mutating
        ///       override remains for ISignedCRDTData consumers.
        bool DeserializeFromBytes( const std::vector<uint8_t> &bytes ) override;
        bool Verify( const std::vector<uint8_t> &payload ) const override;
        /// @brief No-op: cache overwrite happens in NetworkRegistry::TryConfirm
        ///        (TrustedPeerListPayload::Apply convention -- a standalone
        ///        payload instance owns no registry state).
        void Apply() override;

        /// @return The libp2p PeerId base58 membership list.
        const std::vector<std::string> &GetNetworkPeers() const
        {
            return network_peers;
        }

        /// @return The 128-hex member signing-address list.
        const std::vector<std::string> &GetNetworkSigners() const
        {
            return network_signers;
        }

        /// @return The offline credential version (informational metadata).
        uint32_t GetPnetKeyVersion() const
        {
            return pnet_key_version;
        }

        /// @return The short hex credential fingerprint ("" when absent).
        const std::string &GetPnetKeyFingerprint() const
        {
            return pnet_key_fingerprint;
        }

        // Payload fields -- public so tests / seeders can construct records
        // directly. NOTHING except these non-secret metadata fields may ever
        // be serialized into a membership record (D-03).
        std::vector<std::string> network_peers;     ///< libp2p PeerId base58 strings (gater allow-list).
        std::vector<std::string> network_signers;   ///< Member account addresses (128-hex, self-governance signers).
        uint32_t                 pnet_key_version = 1; ///< Offline credential version (informational).
        std::string              pnet_key_fingerprint; ///< Short hex credential fingerprint (optional).
    };

    /**
     * @brief Per-privateNetworkId membership registry (D-06): a child trust
     *        domain of the global TrustedPeerRegistry. Delegates ALL
     *        signature/quorum logic to SecureCrdt/SecureCrdtRegistry.
     *
     *        Lifecycle: pre-confirmation the authorized signer set is the
     *        TPR's current peers snapshotted at construction, at a
     *        TPR-strict-majority threshold -- so the bootstrap record
     *        confirms only with a majority of the global trusted peers. Once
     *        TryConfirm() confirms a record, the registry resolves its signer
     *        set from its OWN cached member signers at its own quorum
     *        threshold (self-governance); a single member can never admit
     *        itself.
     *
     *        Signer-set resolution reads cached state ONLY and never
     *        re-enters the SecureCrdt quorum-read path (Pitfall 9).
     */
    class NetworkRegistry : public std::enable_shared_from_this<NetworkRegistry>,
                            public sgns::peerregistry::PeerRegistry
    {
    public:
        /**
         * @brief Constructs a NetworkRegistry (prefer New()).
         * @param[in] secure_crdt SecureCrdt wrapper to delegate propose/sign/
         *            quorum operations to.
         * @param[in] global_trusted_peers Global root trust domain (D-05) --
         *            the bootstrap authority; never null.
         * @param[in] private_network_id Public network identity (0x-hex 32B
         *            per D-01/D-02) scoping this registry's base key.
         * @param[in] initial_network_peers Initial libp2p PeerId membership
         *            (cached immediately, TPR genesis-cache pattern).
         * @param[in] network_quorum_threshold Self-governance quorum applied
         *            once the bootstrap record is confirmed.
         * @param[in] initial_network_signers Initial member signing addresses
         *            (128-hex). May be empty -- an empty signer set then
         *            fail-closes all post-confirmation updates.
         * @param[in] pnet_key_fingerprint Optional short hex credential
         *            fingerprint carried in records (never raw material).
         * @param[in] base_key Registered CRDT key this instance owns.
         * @param[in] global_db Optional GlobalDB used to register the
         *            change-callback that refreshes the cache when new
         *            quorum-signed membership elements arrive (BurnConfig
         *            pattern); null disables the callback.
         */
        NetworkRegistry( std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt,
                         std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> global_trusted_peers,
                         std::string                                            private_network_id,
                         std::vector<std::string>                               initial_network_peers,
                         uint64_t                                               network_quorum_threshold,
                         std::vector<std::string>                               initial_network_signers,
                         std::string                                            pnet_key_fingerprint,
                         sgns::crdt::HierarchicalKey                            base_key,
                         std::shared_ptr<sgns::crdt::GlobalDB>                   global_db = nullptr );

        ~NetworkRegistry();

        /**
         * @brief Constructs a NetworkRegistry and registers its signer-set
         *        source with SecureCrdtRegistry. Validates the quorum floor
         *        TWICE (D-06): once for the bootstrap threshold -- the strict
         *        majority floor ceil(0.51*TPR_SIZE) over the TPR's current
         *        peers -- and once for `network_quorum_threshold` over
         *        `initial_network_peers.size()` (and, when member signers are
         *        provisioned, over their count as well).
         * @param[in] secure_crdt SecureCrdt wrapper (never null).
         * @param[in] global_trusted_peers Bootstrap authority (never null).
         * @param[in] private_network_id Public network identity.
         * @param[in] initial_network_peers Initial libp2p PeerId membership.
         * @param[in] network_quorum_threshold Self-governance quorum.
         * @param[in] initial_network_signers Initial member signing addresses
         *            (optional; empty fail-closes post-confirmation updates).
         * @param[in] pnet_key_fingerprint Optional credential fingerprint
         *            (optional).
         * @param[in] global_db Optional GlobalDB for the change-callback
         *            cache refresh (optional).
         * @return outcome::success(instance), or
         *         outcome::failure(SecureCrdt::Error::QUORUM_THRESHOLD_BELOW_FLOOR)
         *         if either floor check fails -- construction fails, no
         *         instance is produced -- or
         *         outcome::failure(std::errc::address_in_use) if a registry
         *         for this private network id is already registered (the
         *         duplicate attempt registers NOTHING and the live entry --
         *         and the registry using it -- is left fully functional), or
         *         if the CRDT change callback could not be registered on the
         *         provided global_db (G-WR-02: fail-closed construction --
         *         live membership refresh never silently degrades; the failed
         *         construction explicitly unregisters its just-registered
         *         policy entry before returning, leaving nothing behind).
         */
        static outcome::result<std::shared_ptr<NetworkRegistry>> New(
            std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt,
            std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> global_trusted_peers,
            std::string                                            private_network_id,
            std::vector<std::string>                               initial_network_peers,
            uint64_t                                               network_quorum_threshold,
            std::vector<std::string> initial_network_signers       = {},
            std::string              pnet_key_fingerprint          = {},
            std::shared_ptr<sgns::crdt::GlobalDB> global_db         = nullptr );

        /**
         * @brief Per-network CRDT base key: "network-registry/<id>". One
         *        registration per active network -- no defaulted,
         *        collision-prone key argument (D-06/Pitfall 7).
         * @param[in] private_network_id Public network identity.
         * @return HierarchicalKey of this network's registry branch.
         */
        static sgns::crdt::HierarchicalKey DefaultBaseKey( const std::string &private_network_id );

        /**
         * @brief Proposes the bootstrap membership record through SecureCrdt.
         *        Does NOT self-sign: TPR member nodes add their signatures
         *        through the standard propose/sign flow (SignMembershipChange
         *         / SecureCrdt::AddSignature) until the TPR-majority quorum
         *         is met.
         * @param[in] initial_network_peers Membership list to seed.
         * @return outcome::success on success, or the failing SecureCrdt
         *         call's error.
         */
        outcome::result<void> SeedBootstrap( const std::vector<std::string> &initial_network_peers );

        /**
         * @brief Proposes a membership-change record (full replacement list).
         * @param[in] new_peers Proposed new full libp2p PeerId membership.
         * @param[in] new_signers Proposed member signing addresses; empty
         *            keeps the currently-cached signer list (proposing an
         *            empty signer list would permanently fail-close the
         *            network's self-governance).
         * @return outcome::success on success, or the failing SecureCrdt
         *         call's error.
         */
        outcome::result<void> ProposeMembershipChange( const std::vector<std::string> &new_peers,
                                                       const std::vector<std::string> &new_signers = {} );

        /**
         * @brief Adds a signature over the currently-proposed record.
         * @param[in] signer_address Address claimed to have produced `signature`.
         * @param[in] signature Raw signature bytes.
         * @return outcome::success on success, or the failing SecureCrdt
         *         call's error.
         */
        outcome::result<void> SignMembershipChange( const std::string          &signer_address,
                                                    const std::vector<uint8_t> &signature );

        /**
         * @brief Attempts to confirm the currently-proposed record against
         *        quorum. On confirmation, decodes/verifies/applies the
         *        payload and overwrites BOTH cached membership lists -- never
         *        speculatively before quorum is independently confirmed.
         * @return outcome::success(true) if this call newly confirmed a
         *         record, outcome::success(false) if quorum is not yet met,
         *         or a failure if the confirmed payload is malformed/invalid.
         */
        outcome::result<bool> TryConfirm();

        /**
         * @brief PeerRegistry override: resolves the current authorized
         *        signer set from cached state ONLY -- the TPR snapshot at
         *        TPR-majority pre-confirmation, the cached member signers at
         *        `network_quorum_threshold_` afterwards. NEVER re-enters the
         *        SecureCrdt quorum-read path (Pitfall 9).
         * @return Signer set snapshot for the current state.
         */
        outcome::result<sgns::securecrdt::SignerSetSnapshot> CurrentSignerSet() const override;

        /**
         * @brief Returns a copy of the cached libp2p PeerId membership list
         *        (connection-gater allow-list source, D-07).
         * @return Current PeerId membership list.
         */
        std::vector<std::string> GetCurrentPeers() const override;

        /**
         * @brief PeerRegistry override: returns this registry's CRDT base key.
         * @return HierarchicalKey of the "network-registry/<id>" branch.
         */
        sgns::crdt::HierarchicalKey BaseKey() const override
        {
            return base_key_;
        }

        /**
         * @brief Reports whether the bootstrap record has been confirmed.
         * @return true once TryConfirm has confirmed a record.
         */
        bool IsBootstrapConfirmed() const;

        /**
         * @brief TEST SEAM: number of TryConfirm attempts the refresh thread
         *        has performed (one increment per attempt). Lets regression
         *        tests observe drain-once refresh semantics -- after a
         *        notification is consumed the thread must return to waiting
         *        instead of spinning (WR-02).
         * @return Total refresh-loop TryConfirm attempt count.
         */
        uint64_t RefreshAttemptsForTesting() const;

        /**
         * @brief Unregisters this instance's signer-set source and change
         *        callback (test-fixture teardown helper).
         */
        void Unregister();

    private:
        /**
         * @brief Registers this instance's signer-set source with
         *        SecureCrdtRegistry under the per-network base key.
         */
        bool RegisterSignerSetSource();

        /**
         * @brief Registers the GlobalDB new-element callback that refreshes
         *        the cache when new base_key / sig-child elements arrive
         *        (BurnConfig RegisterNewElementCallback pattern), and starts
         *        the refresh thread that drains those notifications.
         *
         *        The datastore callback itself NEVER runs the quorum-read
         *        path: ReadIfQuorum prunes stale signature children via
         *        GlobalDB::Remove, and executing that re-entrantly from
         *        inside a datastore Put callback corrupts the datastore.
         *        The callback therefore only sets a pending flag and nudges
         *        the condition variable; the dedicated refresh thread calls
         *        TryConfirm() outside any datastore callback context.
         * @return true when the callback was registered AND the refresh
         *         thread started; false when the callback pattern could not
         *         be registered (e.g. an identical pattern is already live
         *         on the GlobalDB) -- in that case NO refresh thread is
         *         started and the caller (New) must fail closed (G-WR-02:
         *         live membership refresh never silently degrades on a
         *         "successful" node).
         */
        bool RegisterCrdtChangeCallback();

        /// @brief Refresh-thread loop: waits for pending notifications and
        ///        runs TryConfirm() outside datastore-callback context.
        void RefreshLoop();

        /**
         * @brief Resolves the current authorized signer set: cached-only
         *        (see CurrentSignerSet). Reads ONLY the cached snapshot
         *        state under cache_mutex_ -- calling ReadIfQuorum from here
         *        would re-enter the quorum-read path mid-verification
         *        (Pitfall 9) and is a bug.
         * @return Signer set snapshot for the current state.
         */
        outcome::result<sgns::securecrdt::SignerSetSnapshot> ResolveSignerSet() const;

        std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt_;
        std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> global_trusted_peers_;
        sgns::crdt::HierarchicalKey                            base_key_;
        uint64_t                                               network_quorum_threshold_;

        // TPR bootstrap authority snapshotted at construction (D-06): the
        // signer set authorizing the FIRST membership record.
        std::vector<std::string> tpr_bootstrap_peers_;
        uint64_t                 tpr_majority_threshold_;

        mutable std::shared_mutex cache_mutex_;
        std::vector<std::string>  cached_network_peers_; ///< libp2p PeerIds (gater allow-list).
        std::vector<std::string>  cached_network_signers_; ///< 128-hex member signing addresses.
        bool                      bootstrap_confirmed_ = false;
        int                       registry_token_     = 0;

        std::string                         pnet_key_fingerprint_;
        std::string                         private_network_id_;
        std::shared_ptr<sgns::crdt::GlobalDB> global_db_;
        std::string                         change_callback_pattern_;

        // Change-callback refresh machinery: the datastore callback only
        // flags + notifies; the refresh thread runs TryConfirm outside any
        // datastore callback (re-entrancy guard -- see
        // RegisterCrdtChangeCallback).
        std::thread             refresh_thread_;
        std::mutex              refresh_mutex_;
        std::condition_variable refresh_cv_;
        std::atomic<bool>       refresh_pending_{ false };
        std::atomic<bool>       refresh_stopping_{ false };
        /// @brief Total refresh-thread TryConfirm attempts (test seam, WR-02).
        std::atomic<uint64_t>   refresh_attempts_{ 0 };

        sgns::base::Logger logger_ = sgns::base::createLogger( "networkregistry" );
    };
} // namespace sgns::networkregistry

#endif // SGNS_NETWORKREGISTRY_NETWORKREGISTRY_HPP
