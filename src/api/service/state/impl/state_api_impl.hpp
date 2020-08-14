
#ifndef SUPERGENIUS_SRC_STATE_API_IMPL_HPP
#define SUPERGENIUS_SRC_STATE_API_IMPL_HPP

#include "api/service/state/state_api.hpp"
#include "blockchain/block_header_repository.hpp"
#include "blockchain/block_tree.hpp"
#include "runtime/core.hpp"
#include "storage/trie/trie_storage.hpp"

namespace sgns::api {

  class StateApiImpl final : public StateApi {
   public:
    StateApiImpl(std::shared_ptr<blockchain::BlockHeaderRepository> block_repo,
                 std::shared_ptr<const storage::trie::TrieStorage> trie_storage,
                 std::shared_ptr<blockchain::BlockTree> block_tree,
                 std::shared_ptr<runtime::Core> r_core);

    outcome::result<base::Buffer> getStorage(
        const base::Buffer &key) const override;
    outcome::result<base::Buffer> getStorage(
        const base::Buffer &key,
        const primitives::BlockHash &at) const override;

    outcome::result<primitives::Version> getRuntimeVersion(
        const boost::optional<primitives::BlockHash> &at) const override;

   private:
    std::shared_ptr<blockchain::BlockHeaderRepository> block_repo_;
    std::shared_ptr<const storage::trie::TrieStorage> storage_;
    std::shared_ptr<blockchain::BlockTree> block_tree_;
    std::shared_ptr<runtime::Core> r_core_;
  };

}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_STATE_API_IMPL_HPP
