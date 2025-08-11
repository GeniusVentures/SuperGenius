//
// Created by Kinder.
//

#include "rlp_decoder.hpp"

using namespace evm::data;

auto evm::rlp::Decoder::decode(const Bytes& data) -> Option<Decoded> { }

auto evm::rlp::Decoder::decode_bytes(const Bytes& data) -> Option<Bytes> {
	usize length = 0;
	usize position = 0;
	if (!read_length(data, EmptyData, length, position)) {
		return std::nullopt;
	}
	if (position + length > data.size()) {
		return std::nullopt;
	}

	const auto start = data.begin() + position;
	const auto end = start + length;
	return Bytes{start, end};
}

auto evm::rlp::Decoder::decode_number(const Bytes& data) -> Option<u64> {
	const auto bytes = decode_bytes(data);
	if (!bytes) {
		return std::nullopt;
	}

	u64 value = 0;
	for (auto byte: *bytes) {
		value = (value << 8) | byte;
	}

	return value;
}

auto evm::rlp::Decoder::decode_list(const Bytes& data) -> Option<Vec<Bytes>> { }

auto evm::rlp::Decoder::read_length(const Bytes& data, const usize offset, usize& length, usize& position) -> bool {
	if (data.empty()) {
		return false;
	}

	const u8 prefix = data[offset];
	if (prefix >= offset && prefix <= offset + ShortLenLimit) {
		length = prefix - offset;
		position = 1;
		return true;
	}

	const usize len = prefix - offset - ShortLenLimit;
	if (data.size() < (1 + len)) {
		return false;
	}

	length = 0;
	for (usize i = 0; i < len; ++i) {
		length = (length << 8) | data[i + 1];
	}
	position = 1 + len;
	return true;
}
