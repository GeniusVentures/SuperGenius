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
	class Encoder {
	public:
		Encoder() = default;

		~Encoder() = default;

	public:
		/**
		 * @brief Encodes a byte string according to the RLP specification.
		 *
		 * @param data Raw bytes to encode as an RLP string.
		 * @return RLP-encoded representation of the input data.
		 */
		static auto encode(const Bytes& data) -> Bytes;

		/**
		 * @brief Encodes an unsigned 64-bit integer.
		 *
		 * @param value unsigned 64-bit integer to encode.
		 * @return RLP-encoded representation of the integer.
		 */
		static auto encode(const u64 value) -> Bytes;

		/**
		 * @brief Encodes a UTF-8 string according to the RLP specification.
		 *
		 * @param string UTF-8 string to encode.
		 * @return RLP-encoded representation of the string.
		 */
		static auto encode(const String& string) -> Bytes;

		/**
		 * @brief Encodes a list of items to the RLP specification.
		 *
		 * @param items Vector of items to encode.
		 * @return RLP-encoded representation of the list.
		 */
		static auto encode(const Vec<Bytes>& items) -> Bytes;

	private:
		/**
		 * @brief Encodes the length prefix according to the RLP specification.
		 *
		 * @param length The length of the payload to encode.
		 * @param offset Base offset value(0x80 for strings, 0xc0 for lists).
		 * @return RLP length prefix bytes.
		 */
		static auto encode_length(const usize length, const u8 offset) -> Bytes;

		/**
		 * @brief Converts a 64-bit unsigned integer to a big-endian byte array.
		 *
		 * @param value The unsigned 64-bit integer to convert.
		 * @return Vector of bytes in big-endian order. Empty if value is 0.
		 */
		static auto to_bytes(u64 value) -> Bytes;
	};
}

#endif //SUPERGENIUS_RLP_DECODER_HPP
