#ifndef SUPERGENIUS_SRC_RUNTIME_BINARYEN_METADATA_IMPL_HPP
#define SUPERGENIUS_SRC_RUNTIME_BINARYEN_METADATA_IMPL_HPP

#include "runtime/binaryen/runtime_api/runtime_api.hpp"
#include "runtime/metadata.hpp"

namespace sgns::runtime::binaryen {

  class MetadataImpl : public RuntimeApi, public Metadata {
   public:
    explicit MetadataImpl(
        const std::shared_ptr<WasmProvider> &wasm_provider,
        const std::shared_ptr<RuntimeManager> &runtime_manager);

    ~MetadataImpl() override = default;

    outcome::result<OpaqueMetadata> metadata() override;
  };
}  // namespace sgns::runtime::binaryen

#endif  // SUPERGENIUS_SRC_RUNTIME_BINARYEN_METADATA_IMPL_HPP
