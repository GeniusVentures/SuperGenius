
#ifndef SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_CHAIN_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_CHAIN_MOCK_HPP

#include <gmock/gmock.h>
#include "verification/finality/chain.hpp"

namespace sgns::verification::finality {

  struct ChainMock : public Chain {
    ~ChainMock() override = default;

    MOCK_CONST_METHOD2(getAncestry,
                       outcome::result<std::vector<primitives::BlockHash>>(
                           const primitives::BlockHash &base,
                           const BlockHash &block));

    MOCK_CONST_METHOD1(bestChainContaining,
                       outcome::result<BlockInfo>(const BlockHash &base));
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_CHAIN_MOCK_HPP
