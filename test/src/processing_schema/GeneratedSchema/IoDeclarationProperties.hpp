//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     IoDeclarationProperties.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "GnusSpecVersion.hpp"
#include "TypeClass.hpp"
#include "Author.hpp"
#include "Dimensions.hpp"
#include "Format.hpp"

namespace sgns {
    using nlohmann::json;

    class IoDeclarationProperties {
        public:
        IoDeclarationProperties() = default;
        virtual ~IoDeclarationProperties() = default;

        private:
        GnusSpecVersion name;
        TypeClass type;
        Author description;
        Dimensions dimensions;
        Format format;

        public:
        const GnusSpecVersion & get_name() const { return name; }
        GnusSpecVersion & get_mutable_name() { return name; }
        void set_name(const GnusSpecVersion & value) { this->name = value; }

        const TypeClass & get_type() const { return type; }
        TypeClass & get_mutable_type() { return type; }
        void set_type(const TypeClass & value) { this->type = value; }

        const Author & get_description() const { return description; }
        Author & get_mutable_description() { return description; }
        void set_description(const Author & value) { this->description = value; }

        const Dimensions & get_dimensions() const { return dimensions; }
        Dimensions & get_mutable_dimensions() { return dimensions; }
        void set_dimensions(const Dimensions & value) { this->dimensions = value; }

        const Format & get_format() const { return format; }
        Format & get_mutable_format() { return format; }
        void set_format(const Format & value) { this->format = value; }
    };
}
