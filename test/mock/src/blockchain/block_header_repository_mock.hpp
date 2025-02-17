#ifndef SUPERGENIUS_TEST_MOCK_BLOCKCHAIN_HEADER_BACKEND_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_BLOCKCHAIN_HEADER_BACKEND_MOCK_HPP

#include "blockchain/block_header_repository.hpp"

#include <gmock/gmock.h>

namespace sgns::blockchain {

  class BlockHeaderRepositoryMock : public BlockHeaderRepository {
   public:
    MOCK_CONST_METHOD1(getNumberByHash, outcome::result<primitives::BlockNumber> (
        const base::Hash256 &hash));
    MOCK_CONST_METHOD1(getHashByNumber, outcome::result<base::Hash256> (
        const primitives::BlockNumber &number));
    MOCK_CONST_METHOD1(getBlockHeader, outcome::result<primitives::BlockHeader> (
        const primitives::BlockId &id));
    MOCK_METHOD1(removeBlockHeader, outcome::result<void> (
        const primitives::BlockId &id));
    MOCK_METHOD1(putBlockHeader, outcome::result<primitives::BlockHash> (
        const primitives::BlockHeader &header));
    MOCK_CONST_METHOD1(getBlockStatus, outcome::result<sgns::blockchain::BlockStatus> (
        const primitives::BlockId &id));
    MOCK_CONST_METHOD1(getHashById, outcome::result<base::Hash256> (
        const primitives::BlockId &id));
    MOCK_CONST_METHOD1(getNumberById, outcome::result<primitives::BlockNumber> (
        const primitives::BlockId &id));

    MOCK_METHOD0(GetName, std::string());
  };
}  // namespace sgns::blockchain

#endif  // SUPERGENIUS_TEST_MOCK_BLOCKCHAIN_HEADER_BACKEND_MOCK_HPP
