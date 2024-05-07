#include "base/blob.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::base, BlobError, e) {
  using sgns::base::BlobError;

  switch(e) {
    case BlobError::INCORRECT_LENGTH:
      return "Input string has incorrect length, not matching the blob size";
  }

  return "Unknown error";
}

namespace sgns::base {

  // explicit instantiations for the most frequently used blobs
  template class Blob<8ul>;
  template class Blob<16ul>;
  template class Blob<32ul>;
  template class Blob<64ul>;

}  // namespace sgns::base
