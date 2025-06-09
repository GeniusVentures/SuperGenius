//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     PassIoBindingProperties.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "Author.hpp"
#include "GnusSpecVersion.hpp"

namespace sgns {
    using nlohmann::json;

    class PassIoBindingProperties {
        public:
        PassIoBindingProperties() = default;
        virtual ~PassIoBindingProperties() = default;

        private:
        Author name;
        GnusSpecVersion source;
        GnusSpecVersion target;

        public:
        const Author & get_name() const { return name; }
        Author & get_mutable_name() { return name; }
        void set_name(const Author & value) { this->name = value; }

        const GnusSpecVersion & get_source() const { return source; }
        GnusSpecVersion & get_mutable_source() { return source; }
        void set_source(const GnusSpecVersion & value) { this->source = value; }

        const GnusSpecVersion & get_target() const { return target; }
        GnusSpecVersion & get_mutable_target() { return target; }
        void set_target(const GnusSpecVersion & value) { this->target = value; }
    };
}
