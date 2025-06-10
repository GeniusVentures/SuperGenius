//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     InputFormat.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    /**
     * Data format (e.g., RGBA8, FLOAT32)
     */

    using nlohmann::json;

    /**
     * Data format (e.g., RGBA8, FLOAT32)
     */
    enum class InputFormat : int { FLOAT16, FLOAT32, INT16, INT32, INT8, RGB8, RGBA8 };
}
