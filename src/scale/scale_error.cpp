

#include "scale/scale_error.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::scale, EncodeError, e) {
  using sgns::scale::EncodeError;
  switch (e) {
    case EncodeError::NEGATIVE_COMPACT_INTEGER:
      return "compact integers cannot be negative";
    case EncodeError::COMPACT_INTEGER_TOO_BIG:
      return "compact integers cannot be negative";
    case EncodeError::WRONG_CATEGORY:
      return "wrong compact encoding category";
    case EncodeError::WRONG_ALTERNATIVE:
      return "wrong cast to alternative";
  }
  return "unknown EncodeError";
}

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::scale, DecodeError, e) {
  using sgns::scale::DecodeError;
  switch (e) {
    case DecodeError::NOT_ENOUGH_DATA:
      return "not enough data to decode";
    case DecodeError::UNEXPECTED_VALUE:
      return "unexpected value occured";
    case DecodeError::TOO_MANY_ITEMS:
      return "collection has too many items, unable to unpack";
    case DecodeError::WRONG_TYPE_INDEX:
      return "wrong type index, cannot decode variant";
    case DecodeError::INVALID_DATA:
      return "incorrect source data";
    case DecodeError::OUT_OF_BOUNDARIES:
      return "advance went out of boundaries";
  }
  return "unknown DecodeError";
}

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::scale, CommonError, e) {
  using sgns::scale::CommonError;
  switch (e) {
    case CommonError::UNKNOWN_ERROR:
      return "unknown error";
  }
  return "unknown CommonError";
}
