//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     GnusSpecVersion.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    enum class TypeEnum : int;
}

namespace sgns {
    using nlohmann::json;

    class GnusSpecVersion {
        public:
        GnusSpecVersion() = default;
        virtual ~GnusSpecVersion() = default;

        private:
        TypeEnum type;
        std::string description;
        std::string pattern;
        boost::optional<std::string> gnus_spec_version_const;

        public:
        const TypeEnum & get_type() const { return type; }
        TypeEnum & get_mutable_type() { return type; }
        void set_type(const TypeEnum & value) { this->type = value; }

        const std::string & get_description() const { return description; }
        std::string & get_mutable_description() { return description; }
        void set_description(const std::string & value) { this->description = value; }

        const std::string & get_pattern() const { return pattern; }
        std::string & get_mutable_pattern() { return pattern; }
        void set_pattern(const std::string & value) { this->pattern = value; }

        boost::optional<std::string> get_gnus_spec_version_const() const { return gnus_spec_version_const; }
        void set_gnus_spec_version_const(boost::optional<std::string> value) { this->gnus_spec_version_const = value; }
    };
}
