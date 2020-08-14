

#ifndef SUPERGENIUS_TEST_MOCK_SRC_AUTHORSHIP_BLOCK_BUILDER_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_AUTHORSHIP_BLOCK_BUILDER_MOCK_HPP

#include "authorship/block_builder.hpp"

#include <gmock/gmock.h>

namespace sgns::authorship {

  class BlockBuilderMock : public BlockBuilder {
   public:
    MOCK_METHOD1(pushExtrinsic,
                 outcome::result<void>(const primitives::Extrinsic &));
    MOCK_CONST_METHOD0(bake, outcome::result<primitives::Block>());
  };

}  // namespace sgns::authorship

#endif  // SUPERGENIUS_TEST_MOCK_SRC_AUTHORSHIP_BLOCK_BUILDER_MOCK_HPP
