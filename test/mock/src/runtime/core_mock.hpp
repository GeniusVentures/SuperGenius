

#ifndef SUPERGENIUS_TEST_MOCK_CORE_RUNTIME_CORE_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_CORE_RUNTIME_CORE_MOCK_HPP

#include "runtime/core.hpp"

#include <gmock/gmock.h>
#include <boost/optional/optional_io.hpp>

namespace sgns::runtime {
  class CoreMock : public Core {
   public:
    MOCK_METHOD1(version,
                 outcome::result<primitives::Version>(
                     boost::optional<primitives::BlockHash> const &));
    MOCK_METHOD1(execute_block,
                 outcome::result<void>(const primitives::Block &));
    MOCK_METHOD1(initialise_block,
                 outcome::result<void>(const primitives::BlockHeader &));
    MOCK_METHOD1(authorities,
                 outcome::result<std::vector<primitives::AuthorityId>>(
                     const primitives::BlockId &));
  };
}  // namespace sgns::runtime

#endif  // SUPERGENIUS_TEST_MOCK_CORE_RUNTIME_CORE_MOCK_HPP
