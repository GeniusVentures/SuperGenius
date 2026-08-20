#ifndef SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP
#define SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP

#include <chrono>
#include <memory>
#include <mutex>

#include "account/BurnConfig.hpp"
#include "account/GeniusNode.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "trustedpeer/GenesisManifest.hpp"

namespace sgns
{
    class GeniusNodeTestAccess
    {
    public:
        static void SetAccountLifecycleForTest( const std::shared_ptr<GeniusNode> &node,
                                                GeniusNode::AccountLifecycle        lifecycle )
        {
            if ( node )
            {
                std::lock_guard<std::recursive_mutex> lock( node->lifecycle_mutex_ );
                node->account_lifecycle_ = lifecycle;
            }
        }

        static void CacheGnusPrice( const std::shared_ptr<GeniusNode> &node, double price )
        {
            if ( node )
            {
                node->m_tokenPriceCache["genius-ai"] = { price, std::chrono::system_clock::now() };
            }
        }

        static outcome::result<void> ApproveConfiguredTrustGenesis( const std::shared_ptr<GeniusNode> &node )
        {
            if ( !node || !node->secure_crdt_ || !node->account_ )
            {
                return outcome::failure( std::errc::invalid_argument );
            }

            trustedpeer::GenesisManifest manifest;
            manifest.network_id              = node->subnet_id_;
            manifest.bootstrapper_public_key = node->bootstrapper_node_address_;
            manifest.peers                   = node->trusted_peers_genesis_;
            manifest.membership_threshold    = node->trusted_peer_quorum_threshold_;
            manifest.burn_threshold          = node->burn_config_quorum_threshold_;
            const auto canonical = manifest.Canonicalized();
            if ( !canonical )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            const auto fingerprint = canonical->Fingerprint();
            const auto payload     = canonical->CanonicalBytes();
            if ( !fingerprint || !payload )
            {
                return outcome::failure( std::errc::invalid_argument );
            }

            securecrdt::CandidateCore core{ securecrdt::CandidateCore::ENCODING_VERSION,
                                            "trusted-peer-genesis",
                                            canonical->network_id,
                                            securecrdt::CandidateKind::TrustedPeerGenesis,
                                            canonical->policy_version,
                                            *fingerprint,
                                            *fingerprint,
                                            *payload };
            const auto bytes = core.CanonicalBytes();
            if ( !bytes )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            auto submitted = node->secure_crdt_->SubmitCandidateApproval(
                { securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
                  std::move( core ),
                  node->account_->GetAddress(),
                  node->account_->Sign( *bytes ) } );
            if ( submitted.has_error() )
            {
                return submitted.error();
            }
            return outcome::success();
        }

        static outcome::result<void> ConfirmInitialBurn( const std::shared_ptr<GeniusNode> &node )
        {
            if ( !node || !node->burn_config_ )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            auto confirmed = node->burn_config_->OnTrustedPeerGenesisConfirmed();
            if ( confirmed.has_error() )
            {
                return confirmed.error();
            }
            return outcome::success();
        }
    };
} // namespace sgns

#endif // SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP
