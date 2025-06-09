//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     ParameterProperties.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "GnusSpecVersion.hpp"
#include "DataTypeClass.hpp"
#include "Default.hpp"
#include "DescriptionClass.hpp"
#include "Constraints.hpp"

namespace sgns {
    using nlohmann::json;

    class ParameterProperties {
        public:
        ParameterProperties() = default;
        virtual ~ParameterProperties() = default;

        private:
        GnusSpecVersion name;
        DataTypeClass type;
        Default properties_default;
        DescriptionClass description;
        Constraints constraints;

        public:
        const GnusSpecVersion & get_name() const { return name; }
        GnusSpecVersion & get_mutable_name() { return name; }
        void set_name(const GnusSpecVersion & value) { this->name = value; }

        const DataTypeClass & get_type() const { return type; }
        DataTypeClass & get_mutable_type() { return type; }
        void set_type(const DataTypeClass & value) { this->type = value; }

        const Default & get_properties_default() const { return properties_default; }
        Default & get_mutable_properties_default() { return properties_default; }
        void set_properties_default(const Default & value) { this->properties_default = value; }

        const DescriptionClass & get_description() const { return description; }
        DescriptionClass & get_mutable_description() { return description; }
        void set_description(const DescriptionClass & value) { this->description = value; }

        const Constraints & get_constraints() const { return constraints; }
        Constraints & get_mutable_constraints() { return constraints; }
        void set_constraints(const Constraints & value) { this->constraints = value; }
    };
}
