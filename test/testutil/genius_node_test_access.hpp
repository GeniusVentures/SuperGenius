#ifndef SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP
#define SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP

#include <chrono>
#include <memory>

#include "account/BurnConfig.hpp"
#include "account/GeniusNode.hpp"
#include "account/TrustStartupController.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "trustedpeer/GenesisManifest.hpp"

namespace sgns
{
    class GeniusNodeTestAccess
    {
    public:
        static void CacheGnusPrice( const std::shared_ptr<GeniusNode> &node, double price )
        {
            if ( node )
            {
                node->m_tokenPriceCache["genius-ai"] = { price, std::chrono::system_clock::now() };
            }
        }

        /// Resolved value of the "bootstrap_background_multiplier" network_config.json key.
        /// There is no public getter because the value has no runtime consumer today (see the
        /// note on the test that uses this), so a test accessor is the only way to observe it.
        static double BootstrapBackgroundMultiplier( const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->reconnect_config_.background_multiplier : 0.0;
        }

        /// The node's validator registry, for tests asserting consensus participation.
        /// GeniusNode::blockchain_ is private (hence this friend class) but
        /// Blockchain::GetValidatorRegistry() is public.
        static std::shared_ptr<ValidatorRegistry> GetValidatorRegistry( const std::shared_ptr<GeniusNode> &node )
        {
            return node && node->blockchain_ ? node->blockchain_->GetValidatorRegistry() : nullptr;
        }

        /// Number of blockchain retries scheduled so far. A fresh offline node
        /// schedules its first retry when Blockchain::Start() fails with
        /// BLOCKCHAIN_NOT_INITIALIZED, so count > 0 marks the pending-retry
        /// window the shutdown-race test tears the node down inside.
        static unsigned int BlockchainRetryCount( const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->blockchain_retry_count_.load() : 0;
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
                fprintf( stderr, "ApproveConfiguredTrustGenesis: Canonicalized failed\n" );
                return outcome::failure( std::errc::invalid_argument );
            }
            const auto fingerprint = canonical->Fingerprint();
            const auto payload     = canonical->CanonicalBytes();
            if ( !fingerprint || !payload )
            {
                fprintf( stderr, "ApproveConfiguredTrustGenesis: Fingerprint/CanonicalBytes failed\n" );
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
                fprintf( stderr, "ApproveConfiguredTrustGenesis: core bytes failed\n" );
                return outcome::failure( std::errc::invalid_argument );
            }
            auto submitted = node->secure_crdt_->SubmitCandidateApproval(
                { securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
                  std::move( core ),
                  node->account_->GetAddress(),
                  node->account_->Sign( *bytes ) } );
            if ( submitted.has_error() )
            {
                fprintf( stderr,
                         "ApproveConfiguredTrustGenesis: submit failed: %s\n",
                         submitted.error().message().c_str() );
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

        /// Drives the node's trust startup controller through one refresh pass. A local
        /// genesis approval submitted via ApproveConfiguredTrustGenesis does not fire the
        /// (remote-delta) candidate callback, so tests nudge the controller explicitly.
        /// No-op while the controller has not been created yet.
        static void RefreshTrust( const std::shared_ptr<GeniusNode> &node )
        {
            if ( node && node->trust_startup_controller_ )
            {
                (void)node->trust_startup_controller_->Refresh();
            }
        }
    };
} // namespace sgns

#endif // SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP
