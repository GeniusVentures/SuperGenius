

#ifndef SUPERGENIUS_PROPOSER_MOCK_HPP
#define SUPERGENIUS_PROPOSER_MOCK_HPP

#include "authorship/proposer.hpp"

#include <gmock/gmock.h>

namespace sgns::authorship {
  class ProposerMock : public Proposer {
   public:
    MOCK_METHOD3(
        propose,
        outcome::result<primitives::Block>(const primitives::BlockId &,
                                           const primitives::InherentData &,
                                           const primitives::Digest &));
    
    MOCK_METHOD0(GetName, std::string());
  };
}  // namespace sgns::authorship

#endif  // SUPERGENIUS_PROPOSER_MOCK_HPP
