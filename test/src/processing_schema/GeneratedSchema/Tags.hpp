//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Tags.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "DescriptionClass.hpp"

namespace sgns {
    enum class TypeEnum : int;
}

namespace sgns {
    using nlohmann::json;

    class Tags {
        public:
        Tags() = default;
        virtual ~Tags() = default;

        private:
        TypeEnum type;
        std::string description;
        DescriptionClass items;

        public:
        const TypeEnum & get_type() const { return type; }
        TypeEnum & get_mutable_type() { return type; }
        void set_type(const TypeEnum & value) { this->type = value; }

        const std::string & get_description() const { return description; }
        std::string & get_mutable_description() { return description; }
        void set_description(const std::string & value) { this->description = value; }

        const DescriptionClass & get_items() const { return items; }
        DescriptionClass & get_mutable_items() { return items; }
        void set_items(const DescriptionClass & value) { this->items = value; }
    };
}
