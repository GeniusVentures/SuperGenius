
#ifndef SUPERGENIUS_SRC_API_SERVICE_AUTHOR_IMPL_AUTHOR_API_IMPL_HPP
#define SUPERGENIUS_SRC_API_SERVICE_AUTHOR_IMPL_AUTHOR_API_IMPL_HPP



#include "api/service/author/author_api.hpp"
#include "blockchain/block_tree.hpp"
#include "base/logger.hpp"
#include "crypto/hasher.hpp"
#include "network/extrinsic_gossiper.hpp"
#include "outcome/outcome.hpp"
#include "storage/trie/trie_storage.hpp"

namespace sgns::transaction_pool {
  class TransactionPool;
}

namespace sgns::runtime {
  class TaggedTransactionQueue;
}

namespace sgns::api {
  class AuthorApiImpl : public AuthorApi {
    template <class T>
    using sptr = std::shared_ptr<T>;

   public:
    /**
     * @constructor
     * @param api ttq instance shared ptr
     * @param pool transaction pool instance shared ptr
     * @param hasher hasher instance shared ptr
     * @param block_tree block tree instance shared ptr
     */
    AuthorApiImpl(std::shared_ptr<runtime::TaggedTransactionQueue> api,
                  std::shared_ptr<transaction_pool::TransactionPool> pool,
                  std::shared_ptr<crypto::Hasher> hasher,
                  std::shared_ptr<network::ExtrinsicGossiper> gossiper);

    ~AuthorApiImpl() override = default;

    outcome::result<base::Hash256> submitExtrinsic(
        const primitives::Extrinsic &extrinsic) override;

    outcome::result<std::vector<primitives::Extrinsic>> pendingExtrinsics()
        override;

    // TODO(yuraz): probably will be documented later (no task yet)
    outcome::result<std::vector<base::Hash256>> removeExtrinsic(
        const std::vector<primitives::ExtrinsicKey> &keys) override;

   private:
    sptr<runtime::TaggedTransactionQueue> api_;
    sptr<transaction_pool::TransactionPool> pool_;
    sptr<crypto::Hasher> hasher_;
    sptr<blockchain::BlockTree> block_tree_;
    std::shared_ptr<network::ExtrinsicGossiper> gossiper_;
    base::Logger logger_;
  };
}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_API_SERVICE_AUTHOR_IMPL_AUTHOR_API_IMPL_HPP
