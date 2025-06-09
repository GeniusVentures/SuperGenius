//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Format.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    enum class TypeEnum : int;
}

namespace sgns {
    using nlohmann::json;

    class Format {
        public:
        Format() = default;
        virtual ~Format() = default;

        private:
        TypeEnum type;
        boost::optional<std::string> description;
        std::vector<std::string> format_enum;
        boost::optional<std::string> format_default;

        public:
        const TypeEnum & get_type() const { return type; }
        TypeEnum & get_mutable_type() { return type; }
        void set_type(const TypeEnum & value) { this->type = value; }

        boost::optional<std::string> get_description() const { return description; }
        void set_description(boost::optional<std::string> value) { this->description = value; }

        const std::vector<std::string> & get_format_enum() const { return format_enum; }
        std::vector<std::string> & get_mutable_format_enum() { return format_enum; }
        void set_format_enum(const std::vector<std::string> & value) { this->format_enum = value; }

        boost::optional<std::string> get_format_default() const { return format_default; }
        void set_format_default(boost::optional<std::string> value) { this->format_default = value; }
    };
}
