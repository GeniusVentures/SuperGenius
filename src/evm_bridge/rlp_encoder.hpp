//
// Created by Kinder.
//

#ifndef SUPERGENIUS_RLP_DECODER_HPP
#define SUPERGENIUS_RLP_DECODER_HPP

#pragma once

#include <string>

#include "evm_types.hpp"

using namespace evm::rs;
using String = std::string;

namespace evm::rlp {
	class Encoder {
	public:
		Encoder() = default;

		~Encoder() = default;

	public:
		static auto encode(const Bytes& data) -> Bytes;

		static auto encode(const u64 value) -> Bytes;

		static auto encode(const String& string) -> Bytes;

	private:
		static auto encode_length(const usize length, const u8 offset) -> Bytes;

		/**
		 * @brief Converts a 64-bit unsigned integer to a big-endian byte array.
		 * @param value The unsigned 64-bit integer to convert.
		 * @return Vector of bytes in big-endian order. Empty if value is 0.
		 */
		static auto to_bytes(u64 value) -> Bytes;
	};
}

#endif //SUPERGENIUS_RLP_DECODER_HPP
