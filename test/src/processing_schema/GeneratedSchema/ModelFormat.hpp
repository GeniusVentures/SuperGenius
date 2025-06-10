//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     ModelFormat.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    /**
     * Model format
     */

    using nlohmann::json;

    /**
     * Model format
     */
    enum class ModelFormat : int { MNN, ONNX, PY_TORCH, TENSOR_FLOW };
}
