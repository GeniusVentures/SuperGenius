

#include "storage/database_error.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::storage, DatabaseError, e) {
  using E = sgns::storage::DatabaseError;
  switch (e) {
    case E::OK:
      return "success";
    case E::NOT_SUPPORTED:
      return "operation is not supported";
    case E::CORRUPTION:
      return "data corruption";
    case E::INVALID_ARGUMENT:
      return "invalid argument";
    case E::IO_ERROR:
      return "IO error";
    case E::NOT_FOUND:
      return "not found";
    case E::UNKNOWN:
      break;
  }

  return "unknown error";
}
