#include "authorship/impl/block_builder_error.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::authorship, BlockBuilderError, e) {
  using sgns::authorship::BlockBuilderError;

  switch (e) {
    case BlockBuilderError::EXTRINSIC_APPLICATION_FAILED:
      return "extrinsic was not applied";
  }
  return "unknown error";
}
