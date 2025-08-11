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
	class Decoder {

	public:
		using Decoded = std::variant<Bytes, u64, String, Vec<Bytes>>;

	public:
		Decoder() = default;

		~Decoder() = default;

	public:
		static auto decode(const Bytes& data) -> Option<Decoded>;

	private:
		static auto decode_bytes(const Bytes& data) -> Option<Bytes>;

		static auto decode_number(const Bytes& data) -> Option<u64>;

		static auto decode_list(const Bytes& data) -> Option<Vec<Bytes>>;

	private:
		static auto read_length(const Bytes& data, const usize offset, usize& length, usize& position) -> bool;
	};
}

#endif //SUPERGENIUS_RLP_DECODER_HPP
