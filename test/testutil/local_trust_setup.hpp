#ifndef SGNS_TESTUTIL_LOCAL_TRUST_SETUP_HPP
#define SGNS_TESTUTIL_LOCAL_TRUST_SETUP_HPP

#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <boost/filesystem.hpp>
#include <gtest/gtest.h>

#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "account/TokenID.hpp"
#include "testutil/genius_node_test_access.hpp"
#include "testutil/wait_condition.hpp"

namespace sgns::test
{
    /// Derives the 128-hex Genius account address for a 64-hex private key by probing
    /// account creation, so the address matches GeniusAccount's KDF exactly (the account
    /// address is NOT GeniusSigner(raw key) — the key is a seed for the account KDF).
    /// Call only after GeniusAccount::SetSecureStorageFactory (e.g. MemorySecureStorage).
    inline std::string TrustAddressFromPrivateKey( const boost::filesystem::path &base_path,
                                                   const std::string             &private_key_hex )
    {
        auto probe = GeniusAccount::NewFromPrivateKey(
            TokenID::FromBytes( { 0x00 } ), private_key_hex.c_str(), base_path );
        EXPECT_NE( probe, nullptr );
        return probe ? probe->GetAddress() : std::string{};
    }

    /// Writes sgns_config.json with an explicit trust policy. Nodes fail closed without
    /// one (FATAL_TRUST_MISMATCH). Every node sharing a CRDT topic must carry identical
    /// values so all derive the same genesis fingerprint.
    inline void WriteTrustedSgnsConfig( const boost::filesystem::path    &base_path,
                                        const std::string                &node_type,
                                        bool                              is_processor,
                                        bool                              rpc_catchup,
                                        const std::vector<std::string>   &peers,
                                        const std::string                &bootstrapper,
                                        uint64_t                          membership_threshold,
                                        uint64_t                          burn_threshold )
    {
        boost::filesystem::create_directories( base_path );
        std::ofstream config( ( base_path / "sgns_config.json" ).string() );
        ASSERT_TRUE( config.good() );
        config << R"({"node_type":")" << node_type << R"(","is_processor":)"
               << ( is_processor ? "true" : "false" ) << R"(,"rpc_catchup":)" << ( rpc_catchup ? "true" : "false" )
               << R"(,"trusted_peers":[)";
        for ( size_t i = 0; i < peers.size(); ++i )
        {
            if ( i != 0 )
            {
                config << ',';
            }
            config << '"' << peers[i] << '"';
        }
        config << R"(],"bootstrapper_node":")" << bootstrapper
               << R"(","trusted_peer_quorum_threshold":)" << membership_threshold
               << R"(,"burn_config_quorum_threshold":)" << burn_threshold << '}';
    }

    /// Single-node convenience: the node's own account (derived from private_key_hex) is the
    /// sole trusted peer and bootstrapper, thresholds 1/1 — a fully self-contained trust
    /// policy that MakeNodeReadyWithLocalTrust can activate without any other participant.
    inline void WriteLocalTrustSgnsConfig( const boost::filesystem::path &base_path,
                                           const std::string             &node_type,
                                           bool                           is_processor,
                                           bool                           rpc_catchup,
                                           const std::string             &private_key_hex )
    {
        const std::string self = TrustAddressFromPrivateKey( base_path, private_key_hex );
        WriteTrustedSgnsConfig( base_path, node_type, is_processor, rpc_catchup, { self }, self, 1, 1 );
    }

    /// Drives a trust-configured node to READY without the genesis ceremony:
    /// waits for the restricted lifecycle state, submits the bootstrapper's genesis
    /// approval through the node's own SecureCrdt, then polls with explicit controller
    /// refreshes until READY. For multi-node setups sharing one manifest, call this on the
    /// bootstrapper node first (its approval activates the shared genesis), then on the
    /// remaining nodes — their refresh picks up the replicated approval.
    inline void MakeNodeReadyWithLocalTrust( const std::shared_ptr<GeniusNode> &node,
                                             std::chrono::milliseconds          timeout = std::chrono::seconds( 50 ) )
    {
        ASSERT_NO_FATAL_FAILURE( assertWaitForCondition(
            [&]()
            {
                return node->GetState() == GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS ||
                       node->GetState() == GeniusNode::NodeState::WAITING_FOR_BURN_GENESIS ||
                       node->GetState() == GeniusNode::NodeState::READY;
            },
            timeout,
            "node did not reach a trust lifecycle state" ) );
        if ( node->GetState() == GeniusNode::NodeState::READY )
        {
            return;
        }
        if ( node->GetState() == GeniusNode::NodeState::WAITING_FOR_TRUST_GENESIS )
        {
            auto approved = GeniusNodeTestAccess::ApproveConfiguredTrustGenesis( node );
            ASSERT_FALSE( approved.has_error() ) << approved.error().message();
        }
        // WAITING_FOR_BURN_GENESIS needs no genesis approval; the controller
        // self-approves the initial burn on refresh.
        ASSERT_NO_FATAL_FAILURE( assertWaitForCondition(
            [&]()
            {
                GeniusNodeTestAccess::RefreshTrust( node );
                return node->GetState() == GeniusNode::NodeState::READY;
            },
            timeout,
            "node did not become READY after local trust genesis" ) );
    }
} // namespace sgns::test

#endif // SGNS_TESTUTIL_LOCAL_TRUST_SETUP_HPP
