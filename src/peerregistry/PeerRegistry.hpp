/**
 * @file       PeerRegistry.hpp
 * @brief      Abstract authorization authority resolving the current authorized
 *             peer set and quorum for a set of CRDT keys (D-04). SecureCRDT
 *             policy entries associate each registered key pattern with the
 *             PeerRegistry instance that owns it, so SecureCrdt stays generic
 *             and never assumes TrustedPeerRegistry globally. TrustedPeerRegistry
 *             (global root trust domain, D-05) and NetworkRegistry (per
 *             privateNetworkId, D-06) both implement this interface.
 * @date       2026-09-01
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_PEERREGISTRY_PEERREGISTRY_HPP
#define SGNS_PEERREGISTRY_PEERREGISTRY_HPP

#include <memory>
#include <vector>

#include "crdt/hierarchical_key.hpp"
#include "outcome/outcome.hpp"
#include "securecrdt/SecureCrdtRegistry.hpp"

namespace sgns::peerregistry
{
    /**
     * @brief Interface of an authorization authority that resolves the current
     *        authorized peer set and its quorum threshold from cached state.
     *        SecureCrdtRegistryEntry carries an optional shared_ptr to the
     *        PeerRegistry instance owning the registered key pattern, making
     *        the per-key association explicit instead of implicitly assuming
     *        TrustedPeerRegistry (D-04).
     */
    class PeerRegistry
    {
    public:
        virtual ~PeerRegistry() = default;

        /**
         * @brief Resolves the current authorized signer set and required
         *        signature count.
         *
         *        Implementations MUST resolve from cached state ONLY and MUST
         *        NEVER re-enter the SecureCrdt quorum-read path (re-entrancy
         *        guard): this method is invoked from inside SecureCrdt's
         *        verification flow, so reading quorum state again would
         *        deadlock or double-count (see TrustedPeerRegistry::
         *        ResolveSignerSet's cached-only contract).
         * @return Snapshot of the current signer set + quorum threshold, or an
         *         error if the registry cannot resolve a set yet.
         */
        virtual outcome::result<sgns::securecrdt::SignerSetSnapshot> CurrentSignerSet() const = 0;

        /**
         * @brief Returns the cached current authorized peer set (e.g. for
         *        connection-gater allow-list polling). Cached-only, same
         *        re-entrancy guard as CurrentSignerSet().
         * @return Current authorized peer identifiers.
         */
        virtual std::vector<std::string> GetCurrentPeers() const = 0;

        /**
         * @brief Returns the CRDT base key this registry is registered under
         *        (e.g. "trusted-peer-registry" or "network-registry/<id>").
         * @return HierarchicalKey of this registry's CRDT branch.
         */
        virtual sgns::crdt::HierarchicalKey BaseKey() const = 0;
    };

    /**
     * @brief Builds a securecrdt::SignerSetSource forwarding to `registry`'s
     *        CurrentSignerSet() (the cached-only path). Lives here rather than
     *        in SecureCrdtRegistry.hpp so SecureCrdtRegistry.hpp only needs a
     *        forward declaration of PeerRegistry, avoiding an include cycle.
     * @param[in] registry Registry whose cached signer set authorizes the key.
     * @return SignerSetSource lambda capturing the registry shared_ptr.
     */
    inline sgns::securecrdt::SignerSetSource MakeRegistrySignerSetSource( std::shared_ptr<PeerRegistry> registry )
    {
        return [registry]( const std::string & /*base_key*/ ) -> outcome::result<sgns::securecrdt::SignerSetSnapshot>
        { return registry->CurrentSignerSet(); };
    }
} // namespace sgns::peerregistry

#endif // SGNS_PEERREGISTRY_PEERREGISTRY_HPP
