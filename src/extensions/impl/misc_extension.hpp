
#ifndef SUPERGENIUS_SRC_MISC_EXTENSION_HPP
#define SUPERGENIUS_SRC_MISC_EXTENSION_HPP

#include <cstdint>

namespace sgns::extensions {
  /**
   * Implements miscellaneous extension functions
   */
  class MiscExtension {
   public:
    MiscExtension() = default;
    ~MiscExtension() = default;
    explicit MiscExtension(uint64_t chain_id);

    /**
     * @return id (a 64-bit unsigned integer) of the current chain
     */
    uint64_t ext_chain_id() const;

   private:
    const uint64_t chain_id_ = 42;
  };
}  // namespace sgns::extensions

#endif  // SUPERGENIUS_SRC_MISC_EXTENSION_HPP
