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

        /// Stage the ELM rate-record CRDT transaction (Phase 01-02).
        /// GeniusNode::CreateElmRateRecordCRDTTransaction is private like its
        /// sibling CreateEscrowInfoCRDTTransaction; Phase 4 will wire it into
        /// ProcessImage, until then this is the only caller.
        static outcome::result<std::shared_ptr<crdt::AtomicTransaction>> CreateElmRateRecord(
            const std::shared_ptr<GeniusNode> &node,
            const std::string                 &escrow_path,
            double                             maximum_processing_hours )
        {
            if ( !node )
            {
                return outcome::failure( GeniusNode::Error::TRANSACTIONS_NOT_READY );
            }
            return node->CreateElmRateRecordCRDTTransaction( escrow_path, maximum_processing_hours );
        }
    };
} // namespace sgns

#endif // SGNS_TESTUTIL_GENIUS_NODE_TEST_ACCESS_HPP
