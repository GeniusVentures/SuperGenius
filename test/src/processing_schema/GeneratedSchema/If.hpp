//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     If.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "IfProperties.hpp"

namespace sgns {
    using nlohmann::json;

    class If {
        public:
        If() = default;
        virtual ~If() = default;

        private:
        IfProperties properties;

        public:
        const IfProperties & get_properties() const { return properties; }
        IfProperties & get_mutable_properties() { return properties; }
        void set_properties(const IfProperties & value) { this->properties = value; }
    };
}
