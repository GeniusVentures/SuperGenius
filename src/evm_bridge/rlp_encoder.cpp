//
// Created by Kinder.
//

#include "rlp_encoder.hpp"

using namespace evm::data;

auto evm::rlp::Encoder::encode(const Bytes& data) -> Bytes {
	// Data is empty
	if (data.empty()) {
		return {EmptyData};
	}

	if (data.size() == 1 && data[0] < EmptyData) {
		return data;
	}

	// Data is short string.
	if (data.size() <= ShortLenLimit) {
		Bytes result = {static_cast<u8>(EmptyData)};
		result.insert(result.end(), static_cast<u8>(data.size()));
		result.insert(result.end(), data.begin(), data.end());
		return result;
	}

	// TODO: Data is long string.

	return data;
}

auto evm::rlp::Encoder::encode(const u64 value) -> Bytes {
	if (value == 0) {
		return {EmptyData};
	}

	return encode(to_bytes(value));
}

auto evm::rlp::Encoder::encode(const String& string) -> Bytes {
	return encode(Bytes{string.begin(), string.end()});
}

auto evm::rlp::Encoder::encode_length(const usize length, const u8 offset) -> Bytes {
	if (length <= ShortLenLimit) {
		return {static_cast<u8>(offset + length)};
	}

	// TODO: Long length
	return {};
}

auto evm::rlp::Encoder::to_bytes(u64 value) -> Bytes {
	if (value == 0) {
		return {};
	}

	// Convert a 64-bit unsigned integer into vector of bytes in big-endian
	// order, byte by byte.
	Bytes result;
	while (value > 0) {
		auto last_byte = static_cast<u8>(value & ByteMask);
		result.push_back(last_byte);

		value >>= 8;
	}
	std::reverse(result.begin(), result.end());

	return result;
}
