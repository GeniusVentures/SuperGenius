//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Definitions.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "IoDeclaration.hpp"
#include "Parameter.hpp"
#include "Pass.hpp"
#include "ModelConfig.hpp"
#include "ModelNode.hpp"
#include "ShaderConfig.hpp"
#include "PassIoBinding.hpp"
#include "DataTransform.hpp"
#include "OptimizerConfig.hpp"
#include "DataTypeClass.hpp"

namespace sgns {
    using nlohmann::json;

    class Definitions {
        public:
        Definitions() = default;
        virtual ~Definitions() = default;

        private:
        IoDeclaration io_declaration;
        Parameter parameter;
        Pass pass;
        ModelConfig model_config;
        ModelNode model_node;
        ShaderConfig shader_config;
        PassIoBinding pass_io_binding;
        DataTransform data_transform;
        OptimizerConfig optimizer_config;
        DataTypeClass data_type;

        public:
        const IoDeclaration & get_io_declaration() const { return io_declaration; }
        IoDeclaration & get_mutable_io_declaration() { return io_declaration; }
        void set_io_declaration(const IoDeclaration & value) { this->io_declaration = value; }

        const Parameter & get_parameter() const { return parameter; }
        Parameter & get_mutable_parameter() { return parameter; }
        void set_parameter(const Parameter & value) { this->parameter = value; }

        const Pass & get_pass() const { return pass; }
        Pass & get_mutable_pass() { return pass; }
        void set_pass(const Pass & value) { this->pass = value; }

        const ModelConfig & get_model_config() const { return model_config; }
        ModelConfig & get_mutable_model_config() { return model_config; }
        void set_model_config(const ModelConfig & value) { this->model_config = value; }

        const ModelNode & get_model_node() const { return model_node; }
        ModelNode & get_mutable_model_node() { return model_node; }
        void set_model_node(const ModelNode & value) { this->model_node = value; }

        const ShaderConfig & get_shader_config() const { return shader_config; }
        ShaderConfig & get_mutable_shader_config() { return shader_config; }
        void set_shader_config(const ShaderConfig & value) { this->shader_config = value; }

        const PassIoBinding & get_pass_io_binding() const { return pass_io_binding; }
        PassIoBinding & get_mutable_pass_io_binding() { return pass_io_binding; }
        void set_pass_io_binding(const PassIoBinding & value) { this->pass_io_binding = value; }

        const DataTransform & get_data_transform() const { return data_transform; }
        DataTransform & get_mutable_data_transform() { return data_transform; }
        void set_data_transform(const DataTransform & value) { this->data_transform = value; }

        const OptimizerConfig & get_optimizer_config() const { return optimizer_config; }
        OptimizerConfig & get_mutable_optimizer_config() { return optimizer_config; }
        void set_optimizer_config(const OptimizerConfig & value) { this->optimizer_config = value; }

        const DataTypeClass & get_data_type() const { return data_type; }
        DataTypeClass & get_mutable_data_type() { return data_type; }
        void set_data_type(const DataTypeClass & value) { this->data_type = value; }
    };
}
