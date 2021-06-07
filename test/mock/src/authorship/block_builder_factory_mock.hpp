

#ifndef SUPERGENIUS_TEST_MOCK_SRC_AUTHORSHIP_BLOCK_BUILDER_FACTORY_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_AUTHORSHIP_BLOCK_BUILDER_FACTORY_MOCK_HPP

#include "authorship/block_builder_factory.hpp"

#include <gmock/gmock.h>

namespace sgns::authorship {

  class BlockBuilderFactoryMock : public BlockBuilderFactory {
   public:
     MOCK_METHOD(outcome::result<std::unique_ptr<BlockBuilder>>, create, (const primitives::BlockId& parent_id, primitives::Digest inherent_digest), (const)); 
  };

}  // namespace sgns::authorship

#endif  // SUPERGENIUS_TEST_MOCK_SRC_AUTHORSHIP_BLOCK_BUILDER_FACTORY_MOCK_HPP
