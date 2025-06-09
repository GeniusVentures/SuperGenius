//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     ModelNodeProperties.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "Author.hpp"
#include "TypeClass.hpp"
#include "GnusSpecVersion.hpp"
#include "Shape.hpp"

namespace sgns {
    using nlohmann::json;

    class ModelNodeProperties {
        public:
        ModelNodeProperties() = default;
        virtual ~ModelNodeProperties() = default;

        private:
        Author name;
        TypeClass type;
        GnusSpecVersion source;
        GnusSpecVersion target;
        Shape shape;

        public:
        const Author & get_name() const { return name; }
        Author & get_mutable_name() { return name; }
        void set_name(const Author & value) { this->name = value; }

        const TypeClass & get_type() const { return type; }
        TypeClass & get_mutable_type() { return type; }
        void set_type(const TypeClass & value) { this->type = value; }

        const GnusSpecVersion & get_source() const { return source; }
        GnusSpecVersion & get_mutable_source() { return source; }
        void set_source(const GnusSpecVersion & value) { this->source = value; }

        const GnusSpecVersion & get_target() const { return target; }
        GnusSpecVersion & get_mutable_target() { return target; }
        void set_target(const GnusSpecVersion & value) { this->target = value; }

        const Shape & get_shape() const { return shape; }
        Shape & get_mutable_shape() { return shape; }
        void set_shape(const Shape & value) { this->shape = value; }
    };
}
