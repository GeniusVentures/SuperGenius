//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     PassType.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    /**
     * Type of processing pass
     */

    using nlohmann::json;

    /**
     * Type of processing pass
     */
    enum class PassType : int { COMPUTE, DATA_TRANSFORM, INFERENCE, RENDER, RETRAIN };
}
