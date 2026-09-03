/**
 * @file       NetworkMembershipFilter.hpp
 * @brief      Application-layer gossip membership gate (D-07 replacement).
 *             Per the owner direction recorded in deferred-items.md section 3
 *             (2026-09-02), the NetworkRegistry membership is NOT injected
 *             into libp2p (no gater allow-list inside the vendored fork);
 *             instead peer/membership filtering happens at the SuperGenius
 *             application layer, at message-handling points that consult the
 *             NetworkRegistry's cached membership (its GetCurrentPeers()
 *             PeerId set). This header provides that predicate: a
 *             fail-closed MembershipFilter built from a weak_ptr to a
 *             NetworkRegistry -- an expired registry denies, an empty
 *             membership set denies (the 15-05 fail-closed posture: empty
 *             membership NEVER fails open), else the peer's base58 id is
 *             tested against the cached member set on every call (per-message
 *             consultation, so runtime membership widening admits previously
 *             denied peers with no filter reinstall).
 *             The gossip ingest enforcement lives in
 *             PubSubBroadcasterExt::OnMessage (src/crdt/globaldb), which
 *             stores a plain std::function and mirrors -- but never calls --
 *             AuthorizeGossipSender: only this header couples the crdt and
 *             networkregistry layers (layering rule).
 * @date       2026-09-03
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_NETWORKREGISTRY_NETWORKMEMBERSHIPFILTER_HPP
#define SGNS_NETWORKREGISTRY_NETWORKMEMBERSHIPFILTER_HPP

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <gsl/span>
#include <libp2p/common/byteutil.hpp>
#include <libp2p/peer/peer_id.hpp>

#include "networkregistry/NetworkRegistry.hpp"

namespace sgns::networkregistry
{
    /**
     * @brief Predicate deciding whether a libp2p peer is an authorized
     *        member of the private network. Installed into
     *        PubSubBroadcasterExt (SetMembershipFilter) and consumed by the
     *        processing-path sender checks (15-13).
     */
    using MembershipFilter = std::function<bool( const libp2p::peer::PeerId & )>;

    /**
     * @brief Builds a registry-backed, fail-closed MembershipFilter.
     *
     *        Semantics per invocation:
     *        - registry weak_ptr expired -> DENY (the registry is gone; the
     *          filter never fails open);
     *        - GetCurrentPeers() empty -> DENY (15-05 fail-closed posture);
     *        - otherwise -> allow iff the peer's base58 id is in the cached
     *          membership snapshot copied under the call (the registry stays
     *          lock-free on this path; membership sets are tens of peers, so
     *          the per-message copy is acceptable).
     * @param[in] registry Weak reference to the NetworkRegistry consulted
     *            per message (kept weak so the filter never extends the
     *            registry's lifetime).
     * @return The membership predicate (never null).
     */
    inline MembershipFilter MakeNetworkMembershipFilter( std::weak_ptr<NetworkRegistry> registry )
    {
        if ( registry.expired() )
        {
            return []( const libp2p::peer::PeerId & ) { return false; };
        }
        return [weak_registry = std::move( registry )]( const libp2p::peer::PeerId &peer ) {
            auto locked = weak_registry.lock();
            if ( !locked )
            {
                return false; // registry gone -> fail closed
            }
            const auto members = locked->GetCurrentPeers();
            if ( members.empty() )
            {
                return false; // empty membership never fails open
            }
            const std::unordered_set<std::string> member_set( members.begin(), members.end() );
            return member_set.count( peer.toBase58() ) > 0;
        };
    }

    /**
     * @brief Builds a config-backed, fail-closed MembershipFilter over the
     *        provisioned bootstrap membership (network_bootstrap_peers_).
     *
     *        Boot-window gate (CR-G02b / G-WR-03): on a private node the
     *        GlobalDB goes live and subscribes its topics from
     *        INITIALIZING_DATABASE, while the registry-backed filter installs
     *        only at NetworkRegistry construction in INITIALIZING_TRANSACTIONS
     *        -- this predicate covers that startup window from the first live
     *        subscription. The argument strings are the SAME base58 PeerId
     *        strings the NetworkRegistry stores verbatim as its cached
     *        membership (cached_network_peers_ = initial_network_peers), and
     *        membership matching is base58 string comparison against
     *        PeerId::toBase58(), so the interim verdict matches the
     *        registry-backed verdict for the provisioned set; the registry
     *        filter later REPLACES this one via SetMembershipFilter.
     *
     *        Semantics per invocation:
     *        - empty bootstrap set -> DENY everything (fail-closed: a private
     *          node with no bootstrap membership can never reach READY anyway
     *          per the 15-05 posture -- empty membership NEVER fails open);
     *        - otherwise -> allow iff the peer's base58 id is in the set.
     * @param[in] bootstrap_peer_ids Provisioned bootstrap member PeerId base58
     *            strings (copied ONCE into a shared_ptr-held unordered_set for
     *            cheap per-message consultation).
     * @return The membership predicate (never null).
     */
    inline MembershipFilter MakeBootstrapMembershipFilter( const std::vector<std::string> &bootstrap_peer_ids )
    {
        auto member_set = std::make_shared<const std::unordered_set<std::string>>(
            bootstrap_peer_ids.begin(), bootstrap_peer_ids.end() );
        return [member_set = std::move( member_set )]( const libp2p::peer::PeerId &peer ) {
            if ( member_set->empty() )
            {
                return false; // empty bootstrap membership never fails open
            }
            return member_set->count( peer.toBase58() ) > 0;
        };
    }

    /**
     * @brief Authorizes a gossip message by its TRANSPORT sender field
     *        (Gossip::Message::from -- a ByteArray carrying the serialized
     *        PeerId of the message creator).
     *
     *        Fail-closed decision table:
     *        - no filter installed (empty std::function) -> ALLOW (public
     *          pass-through; public nodes keep byte-identical behavior);
     *        - PeerId::fromBytes(from_bytes) failure -> DENY. This INCLUDES
     *          the empty-from_bytes case: fromBytes of an empty span fails,
     *          so under a set filter a message with no transport sender is
     *          DENIED, never skipped;
     *        - otherwise -> the filter's verdict on the derived PeerId.
     *
     *        Consumed by the 15-13 processing-path handlers; kept
     *        dependency-light (libp2p peer types only).
     * @param[in] filter Installed MembershipFilter (may be empty).
     * @param[in] from_bytes Transport `from` field bytes (may be empty).
     * @return true when the sender is authorized to participate.
     */
    inline bool AuthorizeGossipSender( const MembershipFilter           &filter,
                                       const libp2p::common::ByteArray &from_bytes )
    {
        if ( !filter )
        {
            return true; // no filter -> public pass-through
        }
        auto from_peer_res = libp2p::peer::PeerId::fromBytes(
            gsl::span<const uint8_t>( from_bytes.data(), from_bytes.size() ) );
        if ( !from_peer_res )
        {
            return false; // empty or malformed from -> fail closed
        }
        return filter( from_peer_res.value() );
    }

} // namespace sgns::networkregistry

#endif // SGNS_NETWORKREGISTRY_NETWORKMEMBERSHIPFILTER_HPP
