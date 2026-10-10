#ifndef SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP
#define SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP

#include <chrono>
#include <memory>
#include <vector>

#include "account/BurnConfig.hpp"
#include "account/GeniusNode.hpp"
#include "account/TrustStartupController.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "trustedpeer/GenesisManifest.hpp"

namespace sgns::networkregistry
{
    class NetworkRegistry; // test-accessor return type only; callers include the full header
}

namespace sgns
{
    class GeniusNodeTestAccess
    {
    public:
        /// Resolved value of the "bootstrap_background_multiplier" network_config.json key.
        /// There is no public getter because the value has no runtime consumer today (see the
        /// note on the test that uses this), so a test accessor is the only way to observe it.
        static double BootstrapBackgroundMultiplier( const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->reconnect_config_.background_multiplier : 0.0;
        }

        /// "private_network_id" retained by InitNetwork (empty = public node). No public getter
        /// exists yet because the value's consumers (NetworkRegistry, scoped CRDT paths) land in
        /// later Phase-15 plans; a test accessor is the only way to observe retention.
        static std::string PrivateNetworkId( const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->private_network_id_ : std::string();
        }

        /// "network_bootstrap_peers" retained by InitNetwork (empty unless provisioned).
        static std::vector<std::string> NetworkBootstrapPeers( const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->network_bootstrap_peers_ : std::vector<std::string>{};
        }

        /// NetworkRegistry constructed by the INITIALIZING_TRANSACTIONS path when a
        /// private_network_id is provisioned (15-05); null on a public node. No public
        /// getter exists because the registry's runtime consumers land in later Phase-15
        /// plans (the vendored gater allow-list binding was descoped), so a test accessor
        /// is the only way to observe the wiring.
        static std::shared_ptr<sgns::networkregistry::NetworkRegistry> NetworkRegistry(
            const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->network_registry_ : nullptr;
        }

        /// Whether the node's GlobalDB broadcaster currently enforces the
        /// registry-backed membership filter (15-12): private nodes install it at
        /// NetworkRegistry construction, public nodes never do, and teardown clears
        /// it. No public getter exists because this observes private wiring state.
        static bool BroadcasterMembershipFilterInstalled( const std::shared_ptr<GeniusNode> &node )
        {
            return node && node->tx_globaldb_ && node->tx_globaldb_->GetBroadcaster()
                 && node->tx_globaldb_->GetBroadcaster()->HasMembershipFilter();
        }

        /// The node's GlobalDB broadcaster, captured BY VALUE so the shared_ptr keeps
        /// the broadcaster object alive across GlobalDB shutdown: ShutdownNow MOVES
        /// m_broadcaster out and Stops it, so a post-shutdown GetBroadcaster() through
        /// the node returns null and any filter assertion through the node would pass
        /// vacuously. Stop() does not touch the membership filter, so
        /// HasMembershipFilter() on the held handle observes exactly the Set/Clear
        /// calls made on that object.
        static std::shared_ptr<sgns::crdt::PubSubBroadcasterExt> BroadcasterOf(
            const std::shared_ptr<GeniusNode> &node )
        {
            return node && node->tx_globaldb_ ? node->tx_globaldb_->GetBroadcaster() : nullptr;
        }

        /// Drives the REAL destruction teardown route (ShutdownForDestruction is
        /// PRIVATE at GeniusNode.hpp, so friend access is the only route).
        /// ~GeniusNode calls it again; the shutdown_started_ compare_exchange makes
        /// that second call a no-op, so explicit-call-then-destroy is safe.
        static void RequestShutdownForDestruction( const std::shared_ptr<GeniusNode> &node )
        {
            if ( node )
            {
                node->ShutdownForDestruction();
            }
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

        /// Ordered pre-destruction shutdown for GeniusNode-based test fixtures.
        ///
        /// Cross-test node leakage (CI 2026-09-07, child_tokens_test): tests
        /// create nodes per test with port_seed=0, i.e. the SAME deterministic
        /// ports every test. When a node's last shared_ptr is released by a
        /// background dispatch (a round-timer / async callback capturing the
        /// node) instead of the test thread, ~GeniusNode runs concurrently
        /// with the NEXT test's node creation, and the zombie keeps its
        /// listening port and consensus round timer alive across the test
        /// boundary. The next test's peers mesh with it, head-sync its stale
        /// epoch-0 genesis registry, and then every escrow-release proposal
        /// binds to a registry whose sole validator no longer exists —
        /// quorum unreachable, WaitForEscrowRelease burns its full 300s
        /// timeout. Same bug class as the cert-fallback teardown segfault
        /// fixed in 0132a8b2d: transient strong refs outliving the fixture.
        ///
        /// Mirrors ~GeniusNode, but runs SYNCHRONOUSLY on the test thread
        /// while the fixture still owns the node:
        ///   1. ShutdownForDestruction() — idempotent (shutdown_started_ CAS):
        ///      stops processing, joins the consensus round timer
        ///      (Blockchain::Stop -> ConsensusManager::Close), stops the
        ///      TransactionManager and the account messenger, drains the tx
        ///      GlobalDB, and closes the GraphSync peers. Members stay alive;
        ///      the delayed ~GeniusNode re-call is a no-op.
        ///   2. GetPubSub()->Stop() — call_once-idempotent and thread-safe:
        ///      closes every connection, the listener, the gossip core, and
        ///      the pubsub worker thread. The delayed ~GeniusNode re-Stop is
        ///      a no-op.
        ///
        /// The pubsub asio context is parked in a process-lifetime sink
        /// BEFORE the Stop — the keepalive dance ~GeniusNode performs via
        /// pubsub_context_keepalive_. StopImpl drops GossipPubSub's context
        /// reference while the GraphSync/GlobalDB chain still co-owns the
        /// libp2p host; without a keepalive the later ~BasicHost (when the
        /// delayed destructor finally releases those members) would touch a
        /// freed reactor. This is the FinalityFaultNetwork::Peer::Stop
        /// invariant: the context must outlive every host co-owner.
        static void StopNode( const std::shared_ptr<GeniusNode> &node )
        {
            if ( !node )
            {
                return;
            }

            node->ShutdownForDestruction();

            if ( auto pubsub = node->GetPubSub() )
            {
                // Test-thread only (gtest TearDown), so the sink needs no lock.
                static std::vector<std::shared_ptr<boost::asio::io_context>> context_sink;
                if ( auto context = pubsub->GetAsioContext() )
                {
                    context_sink.push_back( std::move( context ) );
                }
                pubsub->Stop();
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

            securecrdt::CandidateCore core = trustedpeer::GenesisCandidateCore(
                *canonical, *payload, *fingerprint );
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
