
#ifndef SUPERGENIUS_SRC_EXTENSIONS_STORAGE_EXTENSION_HPP
#define SUPERGENIUS_SRC_EXTENSIONS_STORAGE_EXTENSION_HPP

#include <cstdint>

#include "base/logger.hpp"
#include "runtime/trie_storage_provider.hpp"
#include "runtime/wasm_memory.hpp"
#include "storage/changes_trie/changes_tracker.hpp"

namespace sgns::extensions {
  /**
   * Implements extension functions related to storage
   */
  class StorageExtension {
   public:
    StorageExtension(
        std::shared_ptr<runtime::TrieStorageProvider> storage_provider,
        std::shared_ptr<runtime::WasmMemory> memory,
        std::shared_ptr<storage::changes_trie::ChangesTracker> changes_tracker);

    // -------------------------Data storage--------------------------

    /**
     * @see Extension::ext_clear_prefix
     */
    void ext_clear_prefix(runtime::WasmPointer prefix_data,
                          runtime::WasmSize prefix_length);

    /**
     * @see Extension::ext_clear_storage
     */
    void ext_clear_storage(runtime::WasmPointer key_data,
                           runtime::WasmSize key_length);

    /**
     * @see Extension::ext_exists_storage
     */
    runtime::WasmSize ext_exists_storage(runtime::WasmPointer key_data,
                                         runtime::WasmSize key_length) const;

    /**
     * @see Extension::ext_get_allocated_storage
     */
    runtime::WasmPointer ext_get_allocated_storage(
        runtime::WasmPointer key_data,
        runtime::WasmSize key_length,
        runtime::WasmPointer len_ptr);

    /**
     * @see Extension::ext_get_storage_into
     */
    runtime::WasmSize ext_get_storage_into(runtime::WasmPointer key_data,
                                           runtime::WasmSize key_length,
                                           runtime::WasmPointer value_data,
                                           runtime::WasmSize value_length,
                                           runtime::WasmSize value_offset);

    /**
     * @see Extension::ext_storage_read_version_1
     */
    runtime::WasmSpan ext_storage_read_version_1(runtime::WasmSpan key,
                                                 runtime::WasmSpan value_out,
                                                 runtime::WasmOffset offset);

    /**
     * @see Extension::ext_set_storage
     */
    void ext_set_storage(runtime::WasmPointer key_data,
                         runtime::WasmSize key_length,
                         runtime::WasmPointer value_data,
                         runtime::WasmSize value_length);

    // -------------------------Trie operations--------------------------

    /**
     * @see Extension::ext_blake2_256_enumerated_trie_root
     */
    void ext_blake2_256_enumerated_trie_root(runtime::WasmPointer values_data,
                                             runtime::WasmPointer lengths_data,
                                             runtime::WasmSize values_num,
                                             runtime::WasmPointer result);

    /**
     * @see Extension::ext_storage_changes_root
     */
    runtime::WasmPointer ext_storage_changes_root(
        runtime::WasmPointer parent_hash, runtime::WasmPointer result);

    /**
     * @see Extension::ext_storage_root
     */
    void ext_storage_root(runtime::WasmPointer result) const;

    // ------------------------ VERSION 1 ------------------------

    /**
     * @see Extension::ext_storage_set_version_1
     */
    void ext_storage_set_version_1(runtime::WasmSpan key,
                                   runtime::WasmSpan value);

    /**
     * @see Extension::ext_storage_get_version_1
     */
    runtime::WasmSpan ext_storage_get_version_1(runtime::WasmSpan key);

    /**
     * @see Extension::ext_storage_clear_version_1
     */
    void ext_storage_clear_version_1(runtime::WasmSpan key_data);

    /**
     * @see Extension::ext_storage_exists_version_1
     */
    runtime::WasmSize ext_storage_exists_version_1(
        runtime::WasmSpan key_data) const;

    /**
     * @see Extension::ext_storage_clear_prefix_version_1
     */
    void ext_storage_clear_prefix_version_1(runtime::WasmSpan prefix);

    /**
     * @see Extension::ext_storage_root_version_1
     */
    runtime::WasmSpan ext_storage_root_version_1() const;

    /**
     * @see Extension::ext_storage_changes_root_version_1
     */
    runtime::WasmSpan ext_storage_changes_root_version_1(
        runtime::WasmSpan parent_hash);

    /**
     * @see Extension::ext_storage_next_key
     */
    runtime::WasmSpan ext_storage_next_key_version_1(
        runtime::WasmSpan key) const;

    /**
     * @see Extension::ext_trie_blake2_256_root_version_1
     */
    runtime::WasmPointer ext_trie_blake2_256_root_version_1(
        runtime::WasmSpan values_data);

    /**
     * @see Extension::ext_trie_blake2_256_ordered_root_version_1
     */
    runtime::WasmPointer ext_trie_blake2_256_ordered_root_version_1(
        runtime::WasmSpan values_data);

   private:
    /**
     * Find the value by given key and the return the part of it starting from
     * given offset
     *
     * @param key Buffer representation of the key
     * @param offset SizeType pointing to the beginning of the value
     * @param max_length SizeType defining the maximum possible length of the
     * returned result
     * @return result containing Buffer with the part of the value, or error in
     * case value by give key does not exist
     */
    outcome::result<base::Buffer> get(const base::Buffer &key,
                                        runtime::WasmSize offset,
                                        runtime::WasmSize max_length) const;

    outcome::result<base::Buffer> get(const base::Buffer &key) const;

    outcome::result<boost::optional<base::Buffer>> getStorageNextKey(
        const base::Buffer &key) const;

    boost::optional<base::Buffer> calcStorageChangesRoot(base::Hash256 parent) const;

    std::shared_ptr<runtime::TrieStorageProvider> storage_provider_;
    std::shared_ptr<runtime::WasmMemory> memory_;
    std::shared_ptr<storage::changes_trie::ChangesTracker> changes_tracker_;
    base::Logger logger_;

    constexpr static auto kDefaultLoggerTag = "WASM Runtime [StorageExtension]";
  };
}  // namespace sgns::extensions

#endif  // SUPERGENIUS_SRC_STORAGE_EXTENSIONS_EXTENSION_HPP
