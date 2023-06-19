#ifndef SUPERGENIUS_TEST_MOCK_SRC_RUNTIME_FINALITY_API_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_RUNTIME_FINALITY_API_MOCK_HPP

#include "runtime/finality_api.hpp"

#include <gmock/gmock.h>

namespace sgns::runtime {
  class FinalityApiMock : public FinalityApi {
  public:
    MOCK_METHOD(outcome::result<boost::optional<ScheduledChange>>, pending_change,
                (const Digest& digest), (override));

    MOCK_METHOD(outcome::result<boost::optional<ForcedChange>>, forced_change,
                (const Digest& digest), (override));

    MOCK_METHOD(outcome::result<primitives::AuthorityList>, authorities,
                (const primitives::BlockId& block_id), (override));
  };
}  // namespace sgns::runtime


#endif  // SUPERGENIUS_TEST_MOCK_SRC_RUNTIME_FINALITY_API_MOCK_HPP

