//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     OptimizerType.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    using nlohmann::json;

    enum class OptimizerType : int { ADADELTA, ADAGRAD, ADAM, ADAMW, RMSPROP, SGD };
}
