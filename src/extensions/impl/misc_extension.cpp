
#include <exception>

#include "extensions/impl/misc_extension.hpp"

namespace sgns::extensions {

  MiscExtension::MiscExtension(uint64_t chain_id):
    chain_id_ {chain_id} {}

  uint64_t MiscExtension::ext_chain_id() const {
    return chain_id_;
  }
}  // namespace extensions
