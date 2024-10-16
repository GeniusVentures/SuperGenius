

#ifndef SUPERGENIUS_PRODUCTION_SYNCHRONIZER_IMPL_HPP
#define SUPERGENIUS_PRODUCTION_SYNCHRONIZER_IMPL_HPP

#include "verification/production/production_synchronizer.hpp"

#include "base/logger.hpp"
#include "network/types/sync_clients_set.hpp"

namespace sgns::verification {

  /**
   * Implementation of production synchronizer that requests blocks from provided
   * peers
   */
  class ProductionSynchronizerImpl
      : public ProductionSynchronizer,
        public std::enable_shared_from_this<ProductionSynchronizerImpl> {
   public:
    ~ProductionSynchronizerImpl() override = default;

    explicit ProductionSynchronizerImpl(
        std::shared_ptr<network::SyncClientsSet> sync_clients);

    void request(const primitives::BlockId &from,
                 const primitives::BlockHash &to,
                 primitives::AuthorityIndex authority_index,
                 const BlocksHandler &block_list_handler) override;

    std::string GetName() override
    {
      return "ProductionSynchronizerImpl";
    }

   private:
    /**
     * Select next client to be polled
     * @param polled_clients clients that we already polled
     * @return next clint to be polled
     */
    std::shared_ptr<network::SyncProtocolClient> selectNextClient(
        std::unordered_set<std::shared_ptr<network::SyncProtocolClient>>
            &polled_clients) const;
    /**
     * Request blocks from provided peers
     * @param request block request message
     * @param polled_clients peers that were already requested
     * @param requested_blocks_handler handler of received blocks
     */
    void pollClients(
        network::BlocksRequest request,
        primitives::AuthorityIndex authority_index,
        const BlocksHandler &requested_blocks_handler) const;

    std::shared_ptr<network::SyncClientsSet> sync_clients_;
    base::Logger logger_;
  };
}  // namespace sgns::verification

#endif  // SUPERGENIUS_PRODUCTION_SYNCHRONIZER_IMPL_HPP
