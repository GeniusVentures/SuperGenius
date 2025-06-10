//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     LossFunction.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    /**
     * Loss function for training
     */

    using nlohmann::json;

    /**
     * Loss function for training
     */
    enum class LossFunction : int { BINARY_CROSS_ENTROPY, CROSS_ENTROPY, CUSTOM, HUBER_LOSS, L1_LOSS, MEAN_SQUARED_ERROR };
}
