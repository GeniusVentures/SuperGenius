//
// Created by Kinder.
//

#ifndef SUPERGENIUS_RLP_DECODER_HPP
#define SUPERGENIUS_RLP_DECODER_HPP

#pragma once

#include "evm_types.hpp"

using namespace evm::rs;
using namespace evm::types;

namespace evm::rlp {
	/**
	* @brief Variant type for decoded RLP values.
	*/
	using Decoded = std::variant<Bytes, u64, String, Vec<Bytes>>;

	class Decoder {
	public:
		Decoder() = default;

		~Decoder() = default;

	public:
		/**
		 * @brief Decodes an RLP-encoded byte sequence.
		 *
		 * @param data Input RLP-encoded data.
		 * @return Optional containing decoded value if successful, std::nullopt otherwise.
		 */
		static auto decode(const Bytes& data) -> Option<Decoded>;

	private:
		/**
		 * @brief Decodes RLP-encoded byte string.
		 *
		 * @param data Input RLP-encoded data.
		 * @return Optional containing decoded bytes if successful, std::nullopt otherwise.
		 */
		static auto decode_bytes(const Bytes& data) -> Option<Bytes>;

		/**
		 * @brief Decodes RLP-encoded integer value.
		 *
		 * @param data Input RLP-encoded data.
		 * @return Optional containing decoded number if successful, std::nullopt otherwise.
		 */
		static auto decode_number(const Bytes& data) -> Option<u64>;

		/**
		 * @brief Decodes RLP-encoded list.
		 *
		 * @param data Input RLP-encoded data.
		 * @return Optional containing vector of decoded list items if successful, std::nullopt otherwise.
		 */
		static auto decode_list(const Bytes& data) -> Option<Vec<Bytes>>;

	private:
		/**
		 * @brief Reads length prefix from RLP-encoded data.
		 *
		 * @param data Input RLP-encoded data.
		 * @param offset Type offset (0x80 for strings, 0xC0 for lists).
		 * @param length Extracted length of the data.
		 * @param position Position where payload starts.
		 * @return true if length was successfully read, false otherwise.
		 */
		static auto read_length(const Bytes& data, const usize offset, usize& length, usize& position) -> bool;
	};
}

#endif //SUPERGENIUS_RLP_DECODER_HPP
