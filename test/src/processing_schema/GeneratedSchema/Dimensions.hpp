//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Dimensions.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "DimensionsProperties.hpp"

namespace sgns {
    using nlohmann::json;

    class Dimensions {
        public:
        Dimensions() = default;
        virtual ~Dimensions() = default;

        private:
        std::string type;
        std::string description;
        DimensionsProperties properties;

        public:
        const std::string & get_type() const { return type; }
        std::string & get_mutable_type() { return type; }
        void set_type(const std::string & value) { this->type = value; }

        const std::string & get_description() const { return description; }
        std::string & get_mutable_description() { return description; }
        void set_description(const std::string & value) { this->description = value; }

        const DimensionsProperties & get_properties() const { return properties; }
        DimensionsProperties & get_mutable_properties() { return properties; }
        void set_properties(const DimensionsProperties & value) { this->properties = value; }
    };
}
