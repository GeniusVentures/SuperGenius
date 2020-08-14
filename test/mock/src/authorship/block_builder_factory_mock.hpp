

#ifndef SUPERGENIUS_TEST_MOCK_SRC_AUTHORSHIP_BLOCK_BUILDER_FACTORY_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_AUTHORSHIP_BLOCK_BUILDER_FACTORY_MOCK_HPP

#include "authorship/block_builder_factory.hpp"

#include <gmock/gmock.h>

namespace sgns::authorship {

  class BlockBuilderFactoryMock : public BlockBuilderFactory {
   public:
    // Dirty hack from https://stackoverflow.com/a/11548191 to overcome issue
    // with returning unique_ptr from gmock
    outcome::result<std::unique_ptr<BlockBuilder>> create(
        const primitives::BlockId &parent_id,
        primitives::Digest inherent_digest) const override {
      return std::unique_ptr<BlockBuilder>(
          createProxy(parent_id, std::move(inherent_digest)));
    }

    MOCK_CONST_METHOD2(createProxy,
                       BlockBuilder *(const primitives::BlockId &,
                                      primitives::Digest));
  };

}  // namespace sgns::authorship

#endif  // SUPERGENIUS_TEST_MOCK_SRC_AUTHORSHIP_BLOCK_BUILDER_FACTORY_MOCK_HPP
