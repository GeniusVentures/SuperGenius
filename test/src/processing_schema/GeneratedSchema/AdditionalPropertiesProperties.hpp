//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     AdditionalPropertiesProperties.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "TypeClass.hpp"
#include "Value.hpp"
#include "DescriptionClass.hpp"

namespace sgns {
    using nlohmann::json;

    class AdditionalPropertiesProperties {
        public:
        AdditionalPropertiesProperties() = default;
        virtual ~AdditionalPropertiesProperties() = default;

        private:
        TypeClass type;
        Value value;
        DescriptionClass source;

        public:
        const TypeClass & get_type() const { return type; }
        TypeClass & get_mutable_type() { return type; }
        void set_type(const TypeClass & value) { this->type = value; }

        const Value & get_value() const { return value; }
        Value & get_mutable_value() { return value; }
        void set_value(const Value & value) { this->value = value; }

        const DescriptionClass & get_source() const { return source; }
        DescriptionClass & get_mutable_source() { return source; }
        void set_source(const DescriptionClass & value) { this->source = value; }
    };
}
