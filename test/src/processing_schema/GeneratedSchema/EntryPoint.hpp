//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     EntryPoint.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    enum class TypeEnum : int;
}

namespace sgns {
    using nlohmann::json;

    class EntryPoint {
        public:
        EntryPoint() = default;
        virtual ~EntryPoint() = default;

        private:
        TypeEnum type;
        std::string entry_point_default;

        public:
        const TypeEnum & get_type() const { return type; }
        TypeEnum & get_mutable_type() { return type; }
        void set_type(const TypeEnum & value) { this->type = value; }

        const std::string & get_entry_point_default() const { return entry_point_default; }
        std::string & get_mutable_entry_point_default() { return entry_point_default; }
        void set_entry_point_default(const std::string & value) { this->entry_point_default = value; }
    };
}
