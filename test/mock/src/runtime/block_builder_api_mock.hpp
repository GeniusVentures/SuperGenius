
#ifndef SUPERGENIUS_TEST_MOCK_SRC_RUNTIME_BLOCK_BUILDER_API_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_RUNTIME_BLOCK_BUILDER_API_MOCK_HPP

#include <gmock/gmock.h>
#include "runtime/block_builder.hpp"

namespace sgns::runtime {

  class BlockBuilderApiMock : public BlockBuilder {
   public:
    MOCK_METHOD1(apply_extrinsic,
                 outcome::result<primitives::ApplyResult>(
                     const primitives::Extrinsic &));
    MOCK_METHOD0(finalise_block, outcome::result<primitives::BlockHeader>());
    MOCK_METHOD1(inherent_extrinsics,
                 outcome::result<std::vector<primitives::Extrinsic>>(
                     const primitives::InherentData &));
    MOCK_METHOD2(check_inherents,
                 outcome::result<primitives::CheckInherentsResult>(
                     const primitives::Block &,
                     const primitives::InherentData &));
    MOCK_METHOD0(random_seed, outcome::result<base::Hash256>());
  };

}  // namespace sgns::runtime

#endif  // SUPERGENIUS_TEST_MOCK_SRC_RUNTIME_BLOCK_BUILDER_API_MOCK_HPP
