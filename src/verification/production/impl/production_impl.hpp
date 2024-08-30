

#ifndef SUPERGENIUS_PRODUCTION_IMPL_HPP
#define SUPERGENIUS_PRODUCTION_IMPL_HPP

#include "verification/production.hpp"

#include <memory>

#include <boost/asio/basic_waitable_timer.hpp>
#include "outcome/outcome.hpp"
#include "application/app_state_manager.hpp"
#include "authorship/proposer.hpp"
#include "blockchain/block_tree.hpp"
#include "clock/timer.hpp"
#include "base/logger.hpp"
#include "verification/production/types/production_meta.hpp"
#include "verification/production/production_gossiper.hpp"
#include "verification/production/production_lottery.hpp"
#include "verification/production/epoch_storage.hpp"
#include "verification/production/impl/block_executor.hpp"
#include "crypto/hasher.hpp"
#include "crypto/sr25519_types.hpp"
#include "primitives/production_configuration.hpp"
#include "storage/trie/trie_storage.hpp"

namespace sgns::verification {

  enum class ProductionState {
    WAIT_BLOCK,   // Node is just executed and waits for the new block to sync
                  // missing blocks
    CATCHING_UP,  // Node received first block announce and started fetching
                  // blocks between announced one and the latest finalized one
    NEED_SLOT_TIME,  // Missing blocks were received, now slot time should be
                     // calculated
    SYNCHRONIZED  // All missing blocks were received and applied, slot time was
                  // calculated, current peer can start block production
  };

  inline const auto kTimestampId =
      primitives::InherentIdentifier::fromString("timstap0").value();
  inline const auto kProdSlotId =
      primitives::InherentIdentifier::fromString("prodslot").value();

  class ProductionImpl : public Production, public std::enable_shared_from_this<ProductionImpl> {
   public:
    /**Node configuration must contain 'genesis' option
     * Create an instance of Production implementation
     * @param lottery - implementation of Production Lottery
     * @param proposer - block proposer
     * @param block_tree - tree of the blocks
     * @param gossiper of this verification
     * @param keypair - SR25519 keypair of this node
     * @param authority_index of this node
     * @param clock to measure time
     * @param hasher to take hashes
     * @param timer to be used by the implementation; the recommended one is
     * sgns::clock::BasicWaitableTimer
     * @param event_bus to deliver events over
     */
    ProductionImpl(std::shared_ptr<application::AppStateManager> app_state_manager,
             std::shared_ptr<ProductionLottery> lottery,
             std::shared_ptr<BlockExecutor> block_executor,
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
                 authority_update_observer);

    ~ProductionImpl() override = default;

    bool start();

    void setExecutionStrategy(ExecutionStrategy strategy) override {
      execution_strategy_ = strategy;
    }

    void runEpoch(Epoch epoch,
                  ProductionTimePoint starting_slot_finish_time) override;

    void onBlockAnnounce(const network::BlockAnnounce &announce) override;

    std::string GetName() override
    {
      return "ProductionImpl";
    }

    ProductionMeta getProductionMeta() const;

   private:
    /**
     * Run the next Production slot
     */
    void runSlot();

    /**
     * Finish the current Production slot
     */
    void finishSlot();

    /**
     * Gather the block and broadcast it
     * @param output that we are the leader of this slot
     */
    void processSlotLeadership(const crypto::VRFOutput &output);

    /**
     * Finish the Production epoch
     */
    void finishEpoch();

    ProductionLottery::SlotsLeadership getEpochLeadership(const Epoch &epoch) const;

    outcome::result<primitives::PreRuntime> productionPreDigest(
        const crypto::VRFOutput &output,
        primitives::AuthorityIndex authority_index) const;

    primitives::Seal sealBlock(const primitives::Block &block) const;

    /**
     * To be called if we are far behind other nodes to skip some slots and
     * finally synchronize with the network
     */
    void synchronizeSlots(const primitives::BlockHeader &new_header);

    std::shared_ptr<application::AppStateManager> app_state_manager_;
    std::shared_ptr<ProductionLottery> lottery_;
    std::shared_ptr<BlockExecutor> block_executor_;
    std::shared_ptr<storage::trie::TrieStorage> trie_storage_;
    std::shared_ptr<EpochStorage> epoch_storage_;
    std::shared_ptr<primitives::ProductionConfiguration> genesis_configuration_;
    std::shared_ptr<authorship::Proposer> proposer_;
    std::shared_ptr<blockchain::BlockTree> block_tree_;
    std::shared_ptr<ProductionGossiper> gossiper_;
    crypto::SR25519Keypair keypair_;
    std::shared_ptr<clock::SystemClock> clock_;
    std::shared_ptr<crypto::Hasher> hasher_;
    std::unique_ptr<clock::Timer> timer_;
    std::shared_ptr<authority::AuthorityUpdateObserver>
        authority_update_observer_;

    ProductionState current_state_{ProductionState::WAIT_BLOCK};

    Epoch current_epoch_;

    /// Estimates of the first block production slot time. Input for the median
    /// algorithm
    std::vector<ProductionTimePoint> first_slot_times_;

    /// Number of blocks we need to use in median algorithm to get the slot time
    const uint32_t kSlotTail = 30;

    ProductionSlotNumber current_slot_{};
    ProductionLottery::SlotsLeadership slots_leadership_;
    ProductionTimePoint next_slot_finish_time_;

    boost::optional<ExecutionStrategy> execution_strategy_;

    base::Logger log_;
  };
}  // namespace sgns::verification

#endif  // SUPERGENIUS_PRODUCTION_IMPL_HPP
