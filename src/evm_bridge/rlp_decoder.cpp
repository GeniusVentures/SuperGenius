//
// Created by Kinder.
//

#include "rlp_decoder.hpp"

using namespace evm::data;

auto evm::rlp::Decoder::decode(const Bytes& data) -> Option<Decoded> {
	if (data.empty()) {
		return std::nullopt;
	}

	u8 prefix = data[0];

	// Single byte less than 0x80 returns itself.
	if (prefix < EmptyData) {
		return prefix;
	}

	// Bytes or string
	if (prefix < EmptyList) {
		auto bytes = decode_bytes(data);
		if (!bytes) {
			return std::nullopt;
		}

		if (const auto number = (*bytes)[0];
			bytes->size() == 1 && number < EmptyData) {
			return static_cast<u64>(number);
		}

		return String{bytes->begin(), bytes->end()};
	}

	// In other cases it will be list.
	const auto list = decode_list(data);
	if (!list) {
		return std::nullopt;
	}

	return *list;
}

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

auto evm::rlp::Decoder::decode_list(const Bytes& data) -> Option<Vec<Bytes>> {
	usize length = 0;
	usize position = 0;
	if (!read_length(data, EmptyList, length, position)) {
		return std::nullopt;
	}
	if (position + length > data.size()) {
		return std::nullopt;
	}

	Vec<Bytes> items = {};
	usize pos = position;
	while (pos < position + length) {
		auto sub = Bytes{data.begin() + pos, data.end()};
		auto val = decode(sub);
		if (!val) {
			return std::nullopt;
		}

		usize consumed = 0;
		if (auto b = std::get_if<Bytes>(&*val)) {
			consumed = b->size();
		}
		else if (auto s = std::get_if<String>(&*val)) {
			consumed = s->size();
		}
		else if (auto v = std::get_if<u64>(&*val)) {
			consumed = (*v < EmptyData) ? 1 : decode_bytes(sub)->size();
		}
		else if (std::get_if<Vec<Bytes>>(&*val)) {
			return std::nullopt;
		}

		const auto start = data.begin() + position;
		const auto end = start + consumed;
		auto bytes = Bytes{start, end};

		items.push_back(bytes);
		pos += consumed;
	}

	return items;
}

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
