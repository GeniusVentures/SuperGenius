

#include "network/adapters/adapter_errors.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::network, AdaptersError, e) {
  using E = sgns::network::AdaptersError;
  switch (e) {
    case E::EMPTY_DATA:
      return "No data in buffer.";
    case E::DATA_SIZE_CORRUPTED:
      return "UVAR size does not equal to data size in buffer.";
    case E::PARSE_FAILED:
      return "Got an error while parsing.";
    case E::UNEXPECTED_VARIANT:
      return "Unexpected variant type.";
  }
  return "unknown error (invalid AdaptersError)";
}
