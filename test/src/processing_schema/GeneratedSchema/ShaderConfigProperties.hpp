//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     ShaderConfigProperties.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "Author.hpp"
#include "Format.hpp"
#include "EntryPoint.hpp"
#include "Uniforms.hpp"

namespace sgns {
    using nlohmann::json;

    class ShaderConfigProperties {
        public:
        ShaderConfigProperties() = default;
        virtual ~ShaderConfigProperties() = default;

        private:
        Author source;
        Format type;
        EntryPoint entry_point;
        Uniforms uniforms;

        public:
        const Author & get_source() const { return source; }
        Author & get_mutable_source() { return source; }
        void set_source(const Author & value) { this->source = value; }

        const Format & get_type() const { return type; }
        Format & get_mutable_type() { return type; }
        void set_type(const Format & value) { this->type = value; }

        const EntryPoint & get_entry_point() const { return entry_point; }
        EntryPoint & get_mutable_entry_point() { return entry_point; }
        void set_entry_point(const EntryPoint & value) { this->entry_point = value; }

        const Uniforms & get_uniforms() const { return uniforms; }
        Uniforms & get_mutable_uniforms() { return uniforms; }
        void set_uniforms(const Uniforms & value) { this->uniforms = value; }
    };
}
