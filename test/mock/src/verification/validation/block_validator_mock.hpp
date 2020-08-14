
#ifndef SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_VALIDATION_BLOCK_VALIDATOR_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_VALIDATION_BLOCK_VALIDATOR_MOCK_HPP

#include "verification/validation/production_block_validator.hpp"

#include <gmock/gmock.h>

namespace sgns::verification {

  class BlockValidatorMock : public BlockValidator {
   public:
    MOCK_CONST_METHOD4(
        validateBlock,
        outcome::result<void>(const primitives::Block &block,
                              const primitives::AuthorityId &authority_id,
                              const Threshold &threshold,
                              const Randomness &randomness));

    MOCK_CONST_METHOD4(
        validateHeader,
        outcome::result<void>(const primitives::BlockHeader &header,
                              const primitives::AuthorityId &authority_id,
                              const Threshold &threshold,
                              const Randomness &randomness));
  };

}  // namespace sgns::verification

#endif  // SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_VALIDATION_BLOCK_VALIDATOR_MOCK_HPP
