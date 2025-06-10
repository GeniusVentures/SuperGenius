//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     GnusSpecVersion.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    /**
     * Version of the GNUS processing definition specification
     */

    using nlohmann::json;

    /**
     * Version of the GNUS processing definition specification
     */
    enum class GnusSpecVersion : int { THE_10 };
}
