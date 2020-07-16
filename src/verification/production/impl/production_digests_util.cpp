

#include "verification/production/impl/production_digests_util.hpp"

#include "verification/production/types/verification_log.hpp"
#include "scale/scale.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::verification, DigestError, e) {
  using E = sgns::verification::DigestError;
  switch (e) {
    case E::INVALID_DIGESTS:
      return "the block does not contain valid PRODUCTION digests";
    case E::MULTIPLE_EPOCH_CHANGE_DIGESTS:
      return "the block contains multiple epoch change digests";
    case E::NEXT_EPOCH_DIGEST_DOES_NOT_EXIST:
      return "next epoch digest does not exist";
  }
  return "unknown error";
}

namespace sgns::verification {

  outcome::result<std::pair<Seal, ProductionBlockHeader>> getProductionDigests(
      const primitives::BlockHeader &block_header) {
    // valid PRODUCTION block has at least two digests: ProductionHeader and a seal
    if (block_header.digest.size() < 2) {
      return DigestError::INVALID_DIGESTS;
    }
    const auto &digests = block_header.digest;

    // last digest of the block must be a seal - signature
    auto seal_res = getFromVariant<primitives::Seal>(digests.back());
    if (!seal_res) {
      return DigestError::INVALID_DIGESTS;
    }

    OUTCOME_TRY(production_seal_res, scale::decode<Seal>(seal_res.value().data));

    for (const auto &digest :
         gsl::make_span(digests).subspan(0, digests.size() - 1)) {
      if (auto verification_dig = getFromVariant<primitives::PreRuntime>(digest);
          verification_dig) {
        if (auto header = scale::decode<ProductionBlockHeader>(verification_dig->data);
            header) {
          // found the ProductionBlockHeader digest; return
          return {production_seal_res, header.value()};
        }
      }
    }

    return DigestError::INVALID_DIGESTS;
  }

  outcome::result<NextEpochDescriptor> getNextEpochDigest(
      const primitives::BlockHeader &header) {
    // https://github.com/paritytech/substrate/blob/d8df977d024ebeb5330bacac64cf7193a7c242ed/core/verification/production/src/lib.rs#L497
    outcome::result<NextEpochDescriptor> epoch_digest =
        DigestError::NEXT_EPOCH_DIGEST_DOES_NOT_EXIST;

    for (const auto &log : header.digest) {
      visit_in_place(
          log,
          [&epoch_digest](const primitives::Consensus &verification) {
            if (verification.verification_engine_id == primitives::kProductionEngineId) {
              auto verification_log_res =
                  scale::decode<VerificationLog>(verification.data);
              if (not verification_log_res) {
                return;
              }

              visit_in_place(
                  verification_log_res.value(),
                  [&epoch_digest](const NextEpochDescriptor &next_epoch) {
                    if (not epoch_digest) {
                      epoch_digest = next_epoch;
                    } else {
                      epoch_digest = DigestError::MULTIPLE_EPOCH_CHANGE_DIGESTS;
                    }
                  },
                  [](const auto &) {});
            }
          },
          [](const auto &) {});
    }
    return epoch_digest;
  }
}  // namespace sgns::verification
