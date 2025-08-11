//
// Created by Kinder.
//

#include "rlp_encoder.hpp"

using namespace evm::data;

auto evm::rlp::Encoder::encode(const Bytes& data) -> Bytes {
	// Data is empty.
	if (data.empty()) {
		return {EmptyData};
	}

	// Single byte less than 0x80 encodes as itself.
	if (data.size() == 1 && data[0] < EmptyData) {
		return data;
	}

	// Short string (1-55 bytes).
	if (data.size() <= ShortLenLimit) {
		Bytes result = {static_cast<u8>(EmptyData + data.size())};
		result.insert(result.end(), data.begin(), data.end());
		return result;
	}

	// Long string (56+ bytes).
	Bytes result = encode_length(data.size(), EmptyData);
	result.insert(result.end(), data.begin(), data.end());
	return result;
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

auto evm::rlp::Encoder::encode(const Vec<Bytes>& items) -> Bytes {
	if (items.empty()) {
		return {EmptyList};
	}

	// Combine all elements into one payload.
	Bytes payload = {};
	for (const auto& item: items) {
		Bytes encoded_item = encode(item);
		payload.insert(payload.end(), encoded_item.begin(), encoded_item.end());
	}

	Bytes result = {};
	if (payload.size() <= ShortLenLimit) {
		// Short list (1-55 bytes).
		result = {static_cast<u8>(EmptyList + payload.size())};
		result.insert(result.end(), payload.begin(), payload.end());
	}
	else {
		// Long list (56+ bytes).
		result = encode_length(payload.size(), EmptyList);
		result.insert(result.end(), payload.begin(), payload.end());
	}

	return result;
}

auto evm::rlp::Encoder::encode_length(const usize length, const u8 offset) -> Bytes {
	// Short length (0-55 bytes).
	if (length <= ShortLenLimit) {
		return {static_cast<u8>(offset + length)};
	}

	// Long length (56+ bytes).
	const Bytes length_bytes = to_bytes(length);
	Bytes result = {static_cast<u8>(offset + ShortLenLimit + length_bytes.size())};
	result.insert(result.end(), length_bytes.begin(), length_bytes.end());

	return result;
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
