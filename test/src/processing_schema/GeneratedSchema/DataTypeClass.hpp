//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     DataTypeClass.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    enum class TypeEnum : int;
}

namespace sgns {
    using nlohmann::json;

    class DataTypeClass {
        public:
        DataTypeClass() = default;
        virtual ~DataTypeClass() = default;

        private:
        TypeEnum type;
        std::vector<std::string> type_enum;

        public:
        const TypeEnum & get_type() const { return type; }
        TypeEnum & get_mutable_type() { return type; }
        void set_type(const TypeEnum & value) { this->type = value; }

        const std::vector<std::string> & get_type_enum() const { return type_enum; }
        std::vector<std::string> & get_mutable_type_enum() { return type_enum; }
        void set_type_enum(const std::vector<std::string> & value) { this->type_enum = value; }
    };
}
