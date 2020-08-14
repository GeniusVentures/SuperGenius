
#ifndef SUPERGENIUS_SRC_IO_EXTENSION_HPP
#define SUPERGENIUS_SRC_IO_EXTENSION_HPP

#include <cstdint>

#include "base/logger.hpp"
#include "runtime/wasm_memory.hpp"

namespace sgns::extensions {
  /**
   * Implements extension functions related to IO
   */
  class IOExtension {
   public:
    explicit IOExtension(std::shared_ptr<runtime::WasmMemory> memory);

    /**
     * @see Extension::ext_print_hex
     */
    void ext_print_hex(runtime::WasmPointer data, runtime::WasmSize length);

    /**
     * @see Extension::ext_logging_log_version_1
     */
    void ext_logging_log_version_1(
        runtime::WasmEnum level,
        runtime::WasmSpan target,
        runtime::WasmSpan message);

    /**
     * @see Extension::ext_print_num
     */
    void ext_print_num(uint64_t value);

    /**
     * @see Extension::ext_print_utf8
     */
    void ext_print_utf8(runtime::WasmPointer utf8_data,
                        runtime::WasmSize utf8_length);

   private:
    constexpr static auto kDefaultLoggerTag = "WASM Runtime [IOExtension]";
    std::shared_ptr<runtime::WasmMemory> memory_;
    base::Logger logger_;
  };
}  // namespace sgns::extensions

#endif  // SUPERGENIUS_SRC_IO_EXTENSION_HPP
