//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Constraints.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "ConstraintsProperties.hpp"

namespace sgns {
    using nlohmann::json;

    class Constraints {
        public:
        Constraints() = default;
        virtual ~Constraints() = default;

        private:
        std::string type;
        ConstraintsProperties properties;

        public:
        const std::string & get_type() const { return type; }
        std::string & get_mutable_type() { return type; }
        void set_type(const std::string & value) { this->type = value; }

        const ConstraintsProperties & get_properties() const { return properties; }
        ConstraintsProperties & get_mutable_properties() { return properties; }
        void set_properties(const ConstraintsProperties & value) { this->properties = value; }
    };
}
