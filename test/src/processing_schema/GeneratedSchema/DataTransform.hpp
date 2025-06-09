//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     DataTransform.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "DataTransformProperties.hpp"

namespace sgns {
    using nlohmann::json;

    class DataTransform {
        public:
        DataTransform() = default;
        virtual ~DataTransform() = default;

        private:
        std::string type;
        std::vector<std::string> required;
        DataTransformProperties properties;

        public:
        const std::string & get_type() const { return type; }
        std::string & get_mutable_type() { return type; }
        void set_type(const std::string & value) { this->type = value; }

        const std::vector<std::string> & get_required() const { return required; }
        std::vector<std::string> & get_mutable_required() { return required; }
        void set_required(const std::vector<std::string> & value) { this->required = value; }

        const DataTransformProperties & get_properties() const { return properties; }
        DataTransformProperties & get_mutable_properties() { return properties; }
        void set_properties(const DataTransformProperties & value) { this->properties = value; }
    };
}
