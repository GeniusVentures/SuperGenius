
#ifndef SUPERGENIUS_SRC_AUTHORSHIP_IMPL_AITHORING_IMPL_HPP
#define SUPERGENIUS_SRC_AUTHORSHIP_IMPL_AITHORING_IMPL_HPP

#include "authorship/proposer.hpp"

#include "authorship/block_builder_factory.hpp"
#include "base/logger.hpp"
#include "runtime/block_builder.hpp"
#include "transaction_pool/transaction_pool.hpp"

namespace sgns::authorship {

  class ProposerImpl : public Proposer {
   public:
    ~ProposerImpl() override = default;

    ProposerImpl(
        std::shared_ptr<BlockBuilderFactory> block_builder_factory,
        std::shared_ptr<transaction_pool::TransactionPool> transaction_pool,
        std::shared_ptr<runtime::BlockBuilder> r_block_builder);

    outcome::result<primitives::Block> propose(
        const primitives::BlockId &parent_block_id,
        const primitives::InherentData &inherent_data,
        const primitives::Digest &inherent_digest) override;

   private:
    std::shared_ptr<BlockBuilderFactory> block_builder_factory_;
    std::shared_ptr<transaction_pool::TransactionPool> transaction_pool_;
    std::shared_ptr<runtime::BlockBuilder> r_block_builder_;
    base::Logger logger_ = base::createLogger("Proposer");
  };

}  // namespace sgns::authorship

#endif  // SUPERGENIUS_SRC_AUTHORSHIP_IMPL_AITHORING_IMPL_HPP
