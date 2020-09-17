
#include "runtime/binaryen/runtime_api/metadata_impl.hpp"

namespace sgns::runtime::binaryen {
  using primitives::OpaqueMetadata;

  MetadataImpl::MetadataImpl(
      const std::shared_ptr<WasmProvider> &wasm_provider,
      const std::shared_ptr<RuntimeManager> &runtime_manager)
      : RuntimeApi(wasm_provider, runtime_manager) {}

  outcome::result<OpaqueMetadata> MetadataImpl::metadata() {
    return execute<OpaqueMetadata>("Metadata_metadata",
                                   CallPersistency::EPHEMERAL);
  }
}  // namespace sgns::runtime::binaryen
