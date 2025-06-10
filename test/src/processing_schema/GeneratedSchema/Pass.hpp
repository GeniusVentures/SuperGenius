//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Pass.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "DataTransform.hpp"
#include "PassIoBinding.hpp"
#include "ModelConfig.hpp"
#include "ShaderConfig.hpp"

namespace sgns {
    enum class PassType : int;
}

namespace sgns {
    using nlohmann::json;

    class Pass {
        public:
        Pass() :
            name_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, std::string("^[a-zA-Z][a-zA-Z0-9_]*$"))
        {}
        virtual ~Pass() = default;

        private:
        boost::optional<std::vector<DataTransform>> data_transforms;
        boost::optional<std::string> description;
        boost::optional<bool> enabled;
        boost::optional<std::vector<PassIoBinding>> inputs;
        boost::optional<ModelConfig> model;
        std::string name;
        ClassMemberConstraints name_constraint;
        boost::optional<std::vector<PassIoBinding>> outputs;
        boost::optional<ShaderConfig> shader;
        PassType type;

        public:
        /**
         * Data transformation pipeline
         */
        boost::optional<std::vector<DataTransform>> get_data_transforms() const { return data_transforms; }
        void set_data_transforms(boost::optional<std::vector<DataTransform>> value) { this->data_transforms = value; }

        boost::optional<std::string> get_description() const { return description; }
        void set_description(boost::optional<std::string> value) { this->description = value; }

        /**
         * Whether this pass is enabled by default
         */
        boost::optional<bool> get_enabled() const { return enabled; }
        void set_enabled(boost::optional<bool> value) { this->enabled = value; }

        /**
         * Input bindings for non-model passes
         */
        boost::optional<std::vector<PassIoBinding>> get_inputs() const { return inputs; }
        void set_inputs(boost::optional<std::vector<PassIoBinding>> value) { this->inputs = value; }

        /**
         * Model configuration for inference/retrain passes
         */
        boost::optional<ModelConfig> get_model() const { return model; }
        void set_model(boost::optional<ModelConfig> value) { this->model = value; }

        /**
         * Unique name for this pass
         */
        const std::string & get_name() const { return name; }
        std::string & get_mutable_name() { return name; }
        void set_name(const std::string & value) { CheckConstraint("name", name_constraint, value); this->name = value; }

        /**
         * Output bindings for non-model passes
         */
        boost::optional<std::vector<PassIoBinding>> get_outputs() const { return outputs; }
        void set_outputs(boost::optional<std::vector<PassIoBinding>> value) { this->outputs = value; }

        /**
         * Shader configuration for compute/render passes
         */
        boost::optional<ShaderConfig> get_shader() const { return shader; }
        void set_shader(boost::optional<ShaderConfig> value) { this->shader = value; }

        /**
         * Type of processing pass
         */
        const PassType & get_type() const { return type; }
        PassType & get_mutable_type() { return type; }
        void set_type(const PassType & value) { this->type = value; }
    };
}
