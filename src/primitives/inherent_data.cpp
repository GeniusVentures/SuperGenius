

#include "primitives/inherent_data.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::primitives, InherentDataError, e) {
  using E = sgns::primitives::InherentDataError;
  switch (e) {
    case E::IDENTIFIER_ALREADY_EXISTS:
      return "This identifier already exists";
    case E::IDENTIFIER_DOES_NOT_EXIST:
      return "This identifier does not exist";
  }
  return "Unknow error";
}

namespace sgns::primitives {

  bool InherentData::operator==(
      const sgns::primitives::InherentData &rhs) const {
    return data == rhs.data;
  }

  bool InherentData::operator!=(
      const sgns::primitives::InherentData &rhs) const {
    return !operator==(rhs);
  }

}  // namespace sgns::primitives
