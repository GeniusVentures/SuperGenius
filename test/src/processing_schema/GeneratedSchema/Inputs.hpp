//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Inputs.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "TypeClass.hpp"

namespace sgns {
    enum class TypeEnum : int;
}

namespace sgns {
    using nlohmann::json;

    class Inputs {
        public:
        Inputs() = default;
        virtual ~Inputs() = default;

        private:
        TypeEnum type;
        boost::optional<int64_t> min_items;
        TypeClass items;
        boost::optional<std::string> description;

        public:
        const TypeEnum & get_type() const { return type; }
        TypeEnum & get_mutable_type() { return type; }
        void set_type(const TypeEnum & value) { this->type = value; }

        boost::optional<int64_t> get_min_items() const { return min_items; }
        void set_min_items(boost::optional<int64_t> value) { this->min_items = value; }

        const TypeClass & get_items() const { return items; }
        TypeClass & get_mutable_items() { return items; }
        void set_items(const TypeClass & value) { this->items = value; }

        boost::optional<std::string> get_description() const { return description; }
        void set_description(boost::optional<std::string> value) { this->description = value; }
    };
}
