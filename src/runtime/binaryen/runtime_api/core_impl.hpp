#ifndef CORE_RUNTIME_BINARYEN_CORE_IMPL_HPP
#define CORE_RUNTIME_BINARYEN_CORE_IMPL_HPP

#include "runtime/binaryen/runtime_api/runtime_api.hpp"
#include "runtime/core.hpp"

#include "blockchain/block_header_repository.hpp"
#include "storage/changes_trie/changes_tracker.hpp"
#include "base/logger.hpp"

namespace sgns::runtime::binaryen {

  class CoreImpl : public RuntimeApi, public Core {
   public:
    explicit CoreImpl(
        const std::shared_ptr<RuntimeManager> &runtime_manager,
        std::shared_ptr<storage::changes_trie::ChangesTracker> changes_tracker,
        std::shared_ptr<blockchain::BlockHeaderRepository> header_repo);

    ~CoreImpl() override = default;

    outcome::result<primitives::Version> version(
        const boost::optional<primitives::BlockHash> &block_hash) override;

    outcome::result<void> execute_block(
        const primitives::Block &block) override;

    outcome::result<void> initialise_block(
        const sgns::primitives::BlockHeader &header) override;

    outcome::result<std::vector<primitives::AuthorityId>> authorities(
        const primitives::BlockId &block_id) override;

   private:
    std::shared_ptr<storage::changes_trie::ChangesTracker> changes_tracker_;
    std::shared_ptr<blockchain::BlockHeaderRepository> header_repo_;
	base::Logger logger_;
  };
}  // namespace sgns::runtime::binaryen

#endif  // CORE_RUNTIME_BINARYEN_CORE_IMPL_HPP
