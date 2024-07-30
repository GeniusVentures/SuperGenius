

#ifndef SUPERGENIUS_SRC_STORAGE_DATABASE_ERROR_DATABASE_ERROR_HPP
#define SUPERGENIUS_SRC_STORAGE_DATABASE_ERROR_DATABASE_ERROR_HPP

#include "outcome/outcome.hpp"

namespace sgns::storage {

  /**
   * @brief universal database interface error
   */
  enum class DatabaseError : int {
    OK = 0,
    NOT_FOUND = 1,
    CORRUPTION = 2,
    NOT_SUPPORTED = 3,
    INVALID_ARGUMENT = 4,
    IO_ERROR = 5,

    UNKNOWN = 1000
  };
  std::ostream &operator<<(std::ostream &out, const DatabaseError &test_struct);
}  // namespace sgns::storage

OUTCOME_HPP_DECLARE_ERROR_2(sgns::storage, DatabaseError);

#endif  // SUPERGENIUS_SRC_STORAGE_DATABASE_ERROR_DATABASE_ERROR_HPP
