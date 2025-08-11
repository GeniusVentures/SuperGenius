//
// Created by Kinder.
//

#ifndef SUPERGENIUS_ENV_TYPES_HPP
#define SUPERGENIUS_ENV_TYPES_HPP

#pragma once
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace evm {
	using Bytes = std::vector<uint8_t>;
	using Hash = std::array<uint8_t, 32>;
	using Address = std::array<uint8_t, 32>;

	namespace rs {
		// Unsigned integers.
		using u8 = std::uint8_t;
		using u16 = std::uint16_t;
		using u32 = std::uint32_t;
		using u64 = std::uint64_t;

		// Signed integers.
		using i8 = std::int8_t;
		using i16 = std::int16_t;
		using i32 = std::int32_t;
		using i64 = std::int64_t;

		// Floating point numbers.
		using f32 = float;
		using f64 = double;

		// Platform dependent
		using usize = size_t;
	}

	namespace types {
		using String = std::string;

		template<typename T>
		using Vec = std::vector<T>;

		template <typename T>
		using Option = std::optional<T>;

		template <typename T>
		using Variant = std::variant<T>;
	}

	namespace data {
		static constexpr auto ByteMask = 0xff;

		static constexpr auto EmptyData = 0x80;
		static constexpr auto EmptyList = 0xc0;

		static constexpr rs::usize ShortLenLimit = 55;
	}
}

#endif //SUPERGENIUS_ENV_TYPES_HPP
