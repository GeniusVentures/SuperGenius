
#ifndef SUPERGENIUS_TEST_SRC_API_SERVICE_AUTHOR_AUTHOR_API_MOCK_HPP
#define SUPERGENIUS_TEST_SRC_API_SERVICE_AUTHOR_AUTHOR_API_MOCK_HPP

#include <gmock/gmock.h>

#include "api/service/author/author_api.hpp"

namespace sgns::api {

  class AuthorApiMock : public AuthorApi {
   public:
    ~AuthorApiMock() override = default;

    MOCK_METHOD1(submitExtrinsic, outcome::result<Hash256>(const Extrinsic &));

    MOCK_METHOD0(pendingExtrinsics, outcome::result<std::vector<Extrinsic>>());

    MOCK_METHOD1(removeExtrinsic,
                 outcome::result<std::vector<Hash256>>(
                     const std::vector<ExtrinsicKey> &));
  };

}  // namespace sgns::api

#endif  // SUPERGENIUS_TEST_SRC_API_SERVICE_AUTHOR_AUTHOR_API_MOCK_HPP
