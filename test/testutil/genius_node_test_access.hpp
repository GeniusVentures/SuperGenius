#ifndef SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP
#define SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP

#include <chrono>
#include <memory>
#include <vector>

#include "account/GeniusNode.hpp"

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
    };
} // namespace sgns

#endif // SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP
