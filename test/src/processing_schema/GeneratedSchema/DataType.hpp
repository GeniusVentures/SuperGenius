//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     DataType.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    using nlohmann::json;

    enum class DataType : int { BOOL, BUFFER, FLOAT, INT, MAT2, MAT3, MAT4, STRING, TENSOR, TEXTURE1_D, TEXTURE1_D_ARRAY, TEXTURE2_D, TEXTURE2_D_ARRAY, TEXTURE3_D, TEXTURE3_D_ARRAY, TEXTURE_CUBE, VEC2, VEC3, VEC4 };
}
