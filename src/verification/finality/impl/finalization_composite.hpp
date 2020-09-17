
#ifndef SUPERGENIUS_VERIFICATION_FINALIZATION_COMPOSITE
#define SUPERGENIUS_VERIFICATION_FINALIZATION_COMPOSITE

#include "verification/finality/finalization_observer.hpp"

#include "base/type_traits.hpp"

namespace sgns::verification::finality {

  /**
   * @brief Special wrapper for aggregate anyone finalisation observers.
   * Needed in order to have single endpoint to handle finalization.
   */
  class FinalizationComposite final : public FinalizationObserver {
   public:
    template <class... Args,
              typename =
                  std::enable_if_t<(is_shared_ptr<Args>::value() && ...), void>>
    explicit FinalizationComposite(Args &&... args)
        : observers_{std::forward<Args>(args)...} {};

    ~FinalizationComposite() override = default;

    /**
     * Doing something at block finalized
     * @param message
     * @return failure or nothing
     */
    outcome::result<void> onFinalize(
        const primitives::BlockInfo &block) override;

   private:
    std::vector<std::shared_ptr<FinalizationObserver>> observers_;
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_VERIFICATION_FINALIZATION_COMPOSITE
