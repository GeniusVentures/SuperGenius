//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Enabled.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    using nlohmann::json;

    class Enabled {
        public:
        Enabled() = default;
        virtual ~Enabled() = default;

        private:
        std::string type;
        bool enabled_default;
        std::string description;

        public:
        const std::string & get_type() const { return type; }
        std::string & get_mutable_type() { return type; }
        void set_type(const std::string & value) { this->type = value; }

        const bool & get_enabled_default() const { return enabled_default; }
        bool & get_mutable_enabled_default() { return enabled_default; }
        void set_enabled_default(const bool & value) { this->enabled_default = value; }

        const std::string & get_description() const { return description; }
        std::string & get_mutable_description() { return description; }
        void set_description(const std::string & value) { this->description = value; }
    };
}
