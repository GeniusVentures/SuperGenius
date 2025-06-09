//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     ConstraintsProperties.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "DescriptionClass.hpp"

namespace sgns {
    using nlohmann::json;

    class ConstraintsProperties {
        public:
        ConstraintsProperties() = default;
        virtual ~ConstraintsProperties() = default;

        private:
        DescriptionClass min;
        DescriptionClass max;
        DescriptionClass properties_enum;
        DescriptionClass pattern;

        public:
        const DescriptionClass & get_min() const { return min; }
        DescriptionClass & get_mutable_min() { return min; }
        void set_min(const DescriptionClass & value) { this->min = value; }

        const DescriptionClass & get_max() const { return max; }
        DescriptionClass & get_mutable_max() { return max; }
        void set_max(const DescriptionClass & value) { this->max = value; }

        const DescriptionClass & get_properties_enum() const { return properties_enum; }
        DescriptionClass & get_mutable_properties_enum() { return properties_enum; }
        void set_properties_enum(const DescriptionClass & value) { this->properties_enum = value; }

        const DescriptionClass & get_pattern() const { return pattern; }
        DescriptionClass & get_mutable_pattern() { return pattern; }
        void set_pattern(const DescriptionClass & value) { this->pattern = value; }
    };
}
