#ifndef SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP
#define SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP

#include <chrono>
#include <memory>

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
    };
} // namespace sgns

#endif // SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP
