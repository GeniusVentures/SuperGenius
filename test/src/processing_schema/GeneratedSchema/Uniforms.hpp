//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Uniforms.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "AdditionalProperties.hpp"

namespace sgns {
    using nlohmann::json;

    class Uniforms {
        public:
        Uniforms() = default;
        virtual ~Uniforms() = default;

        private:
        std::string type;
        std::string description;
        AdditionalProperties additional_properties;

        public:
        const std::string & get_type() const { return type; }
        std::string & get_mutable_type() { return type; }
        void set_type(const std::string & value) { this->type = value; }

        const std::string & get_description() const { return description; }
        std::string & get_mutable_description() { return description; }
        void set_description(const std::string & value) { this->description = value; }

        const AdditionalProperties & get_additional_properties() const { return additional_properties; }
        AdditionalProperties & get_mutable_additional_properties() { return additional_properties; }
        void set_additional_properties(const AdditionalProperties & value) { this->additional_properties = value; }
    };
}
