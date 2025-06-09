//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Pass.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "PassProperties.hpp"
#include "AllOf.hpp"

namespace sgns {
    using nlohmann::json;

    class Pass {
        public:
        Pass() = default;
        virtual ~Pass() = default;

        private:
        std::string type;
        std::vector<std::string> required;
        PassProperties properties;
        std::vector<AllOf> all_of;

        public:
        const std::string & get_type() const { return type; }
        std::string & get_mutable_type() { return type; }
        void set_type(const std::string & value) { this->type = value; }

        const std::vector<std::string> & get_required() const { return required; }
        std::vector<std::string> & get_mutable_required() { return required; }
        void set_required(const std::vector<std::string> & value) { this->required = value; }

        const PassProperties & get_properties() const { return properties; }
        PassProperties & get_mutable_properties() { return properties; }
        void set_properties(const PassProperties & value) { this->properties = value; }

        const std::vector<AllOf> & get_all_of() const { return all_of; }
        std::vector<AllOf> & get_mutable_all_of() { return all_of; }
        void set_all_of(const std::vector<AllOf> & value) { this->all_of = value; }
    };
}
