//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     PassProperties.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "GnusSpecVersion.hpp"
#include "Format.hpp"
#include "DescriptionClass.hpp"
#include "Enabled.hpp"
#include "Optimizer.hpp"
#include "Inputs.hpp"

namespace sgns {
    using nlohmann::json;

    class PassProperties {
        public:
        PassProperties() = default;
        virtual ~PassProperties() = default;

        private:
        GnusSpecVersion name;
        Format type;
        DescriptionClass description;
        Enabled enabled;
        Optimizer model;
        Optimizer shader;
        Inputs data_transforms;
        Inputs inputs;
        Inputs outputs;

        public:
        const GnusSpecVersion & get_name() const { return name; }
        GnusSpecVersion & get_mutable_name() { return name; }
        void set_name(const GnusSpecVersion & value) { this->name = value; }

        const Format & get_type() const { return type; }
        Format & get_mutable_type() { return type; }
        void set_type(const Format & value) { this->type = value; }

        const DescriptionClass & get_description() const { return description; }
        DescriptionClass & get_mutable_description() { return description; }
        void set_description(const DescriptionClass & value) { this->description = value; }

        const Enabled & get_enabled() const { return enabled; }
        Enabled & get_mutable_enabled() { return enabled; }
        void set_enabled(const Enabled & value) { this->enabled = value; }

        const Optimizer & get_model() const { return model; }
        Optimizer & get_mutable_model() { return model; }
        void set_model(const Optimizer & value) { this->model = value; }

        const Optimizer & get_shader() const { return shader; }
        Optimizer & get_mutable_shader() { return shader; }
        void set_shader(const Optimizer & value) { this->shader = value; }

        const Inputs & get_data_transforms() const { return data_transforms; }
        Inputs & get_mutable_data_transforms() { return data_transforms; }
        void set_data_transforms(const Inputs & value) { this->data_transforms = value; }

        const Inputs & get_inputs() const { return inputs; }
        Inputs & get_mutable_inputs() { return inputs; }
        void set_inputs(const Inputs & value) { this->inputs = value; }

        const Inputs & get_outputs() const { return outputs; }
        Inputs & get_mutable_outputs() { return outputs; }
        void set_outputs(const Inputs & value) { this->outputs = value; }
    };
}
