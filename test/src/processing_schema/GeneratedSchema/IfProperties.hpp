//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     IfProperties.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "PurpleType.hpp"

namespace sgns {
    using nlohmann::json;

    class IfProperties {
        public:
        IfProperties() = default;
        virtual ~IfProperties() = default;

        private:
        PurpleType type;

        public:
        const PurpleType & get_type() const { return type; }
        PurpleType & get_mutable_type() { return type; }
        void set_type(const PurpleType & value) { this->type = value; }
    };
}
