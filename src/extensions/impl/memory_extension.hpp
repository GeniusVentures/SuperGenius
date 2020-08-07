#ifndef SUPERGENIUS_SRC_MEMORY_EXTENSIONS_HPP
#define SUPERGENIUS_SRC_MEMORY_EXTENSIONS_HPP

#include "base/logger.hpp"
#include "runtime/wasm_memory.hpp"

namespace sgns::extensions {
  /**
   * Implements extension functions related to memory
   * Works with memory of wasm runtime
   */
  class MemoryExtension {
   public:
    explicit MemoryExtension(std::shared_ptr<runtime::WasmMemory> memory);

    /**
     * @see Extension::ext_malloc
     */
    runtime::WasmPointer ext_malloc(runtime::WasmSize size);

    /**
     * @see Extension::ext_free
     */
    void ext_free(runtime::WasmPointer ptr);

   private:
    constexpr static auto kDefaultLoggerTag = "WASM Runtime [MemoryExtension]";
    std::shared_ptr<runtime::WasmMemory> memory_;
    base::Logger logger_;
  };
}  // namespace sgns::extensions

#endif  // SUPERGENIUS_SRC_MEMORY_EXTENSIONS_HPP
