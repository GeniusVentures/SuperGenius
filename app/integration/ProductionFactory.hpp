/**
 * @file       ProductionFactory.hpp
 * @brief      
 * @date       2024-02-22
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _PRODUCTION_FACTORY_HPP_
#define _PRODUCTION_FACTORY_HPP_

class ProductionFactory
{
public:
    static std::shared_ptr<sgns::verification::Production> create()
    {

        BlockExecutor
        return std::make_shared<verification::ProductionImpl>( //
            AppStateManagerFactory::create(),//
            ProductionLotteryFactory::create(),//

        );

        /**
         *              std::shared_ptr<BlockExecutor> block_executor,
             std::shared_ptr<storage::trie::TrieStorage> trie_db,
             std::shared_ptr<EpochStorage> epoch_storage,
             std::shared_ptr<primitives::ProductionConfiguration> configuration,
             std::shared_ptr<authorship::Proposer> proposer,
             std::shared_ptr<blockchain::BlockTree> block_tree,
             std::shared_ptr<ProductionGossiper> gossiper,
             crypto::SR25519Keypair keypair,
             std::shared_ptr<clock::SystemClock> clock,
             std::shared_ptr<crypto::Hasher> hasher,
             std::unique_ptr<clock::Timer> timer,
             std::shared_ptr<authority::AuthorityUpdateObserver>
         */
    }
};

#endif
