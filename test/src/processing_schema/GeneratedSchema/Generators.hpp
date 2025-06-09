//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Generators.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "Welcome.hpp"
#include "WelcomeProperties.hpp"
#include "Tags.hpp"
#include "Metadata.hpp"
#include "Definitions.hpp"
#include "ShaderConfig.hpp"
#include "ShaderConfigProperties.hpp"
#include "Uniforms.hpp"
#include "AdditionalProperties.hpp"
#include "AdditionalPropertiesProperties.hpp"
#include "Value.hpp"
#include "EntryPoint.hpp"
#include "PassIoBinding.hpp"
#include "PassIoBindingProperties.hpp"
#include "Pass.hpp"
#include "PassProperties.hpp"
#include "Enabled.hpp"
#include "AllOf.hpp"
#include "Then.hpp"
#include "If.hpp"
#include "IfProperties.hpp"
#include "PurpleType.hpp"
#include "Parameter.hpp"
#include "ParameterProperties.hpp"
#include "Default.hpp"
#include "Constraints.hpp"
#include "ConstraintsProperties.hpp"
#include "OptimizerConfig.hpp"
#include "OptimizerConfigProperties.hpp"
#include "Beta1.hpp"
#include "ModelNode.hpp"
#include "ModelNodeProperties.hpp"
#include "Shape.hpp"
#include "ModelConfig.hpp"
#include "ModelConfigProperties.hpp"
#include "Optimizer.hpp"
#include "Inputs.hpp"
#include "BatchSize.hpp"
#include "IoDeclaration.hpp"
#include "IoDeclarationProperties.hpp"
#include "TypeClass.hpp"
#include "GnusSpecVersion.hpp"
#include "Format.hpp"
#include "Dimensions.hpp"
#include "DimensionsProperties.hpp"
#include "Epsilon.hpp"
#include "DataTransform.hpp"
#include "DataTransformProperties.hpp"
#include "DataTypeClass.hpp"
#include "Params.hpp"
#include "ParamsProperties.hpp"
#include "Axes.hpp"
#include "DescriptionClass.hpp"
#include "Author.hpp"
#include "TypeEnum.hpp"

namespace sgns {
    void from_json(const json & j, Author & x);
    void to_json(json & j, const Author & x);

    void from_json(const json & j, DescriptionClass & x);
    void to_json(json & j, const DescriptionClass & x);

    void from_json(const json & j, Axes & x);
    void to_json(json & j, const Axes & x);

    void from_json(const json & j, ParamsProperties & x);
    void to_json(json & j, const ParamsProperties & x);

    void from_json(const json & j, Params & x);
    void to_json(json & j, const Params & x);

    void from_json(const json & j, DataTypeClass & x);
    void to_json(json & j, const DataTypeClass & x);

    void from_json(const json & j, DataTransformProperties & x);
    void to_json(json & j, const DataTransformProperties & x);

    void from_json(const json & j, DataTransform & x);
    void to_json(json & j, const DataTransform & x);

    void from_json(const json & j, Epsilon & x);
    void to_json(json & j, const Epsilon & x);

    void from_json(const json & j, DimensionsProperties & x);
    void to_json(json & j, const DimensionsProperties & x);

    void from_json(const json & j, Dimensions & x);
    void to_json(json & j, const Dimensions & x);

    void from_json(const json & j, Format & x);
    void to_json(json & j, const Format & x);

    void from_json(const json & j, GnusSpecVersion & x);
    void to_json(json & j, const GnusSpecVersion & x);

    void from_json(const json & j, TypeClass & x);
    void to_json(json & j, const TypeClass & x);

    void from_json(const json & j, IoDeclarationProperties & x);
    void to_json(json & j, const IoDeclarationProperties & x);

    void from_json(const json & j, IoDeclaration & x);
    void to_json(json & j, const IoDeclaration & x);

    void from_json(const json & j, BatchSize & x);
    void to_json(json & j, const BatchSize & x);

    void from_json(const json & j, Inputs & x);
    void to_json(json & j, const Inputs & x);

    void from_json(const json & j, Optimizer & x);
    void to_json(json & j, const Optimizer & x);

    void from_json(const json & j, ModelConfigProperties & x);
    void to_json(json & j, const ModelConfigProperties & x);

    void from_json(const json & j, ModelConfig & x);
    void to_json(json & j, const ModelConfig & x);

    void from_json(const json & j, Shape & x);
    void to_json(json & j, const Shape & x);

    void from_json(const json & j, ModelNodeProperties & x);
    void to_json(json & j, const ModelNodeProperties & x);

    void from_json(const json & j, ModelNode & x);
    void to_json(json & j, const ModelNode & x);

    void from_json(const json & j, Beta1 & x);
    void to_json(json & j, const Beta1 & x);

    void from_json(const json & j, OptimizerConfigProperties & x);
    void to_json(json & j, const OptimizerConfigProperties & x);

    void from_json(const json & j, OptimizerConfig & x);
    void to_json(json & j, const OptimizerConfig & x);

    void from_json(const json & j, ConstraintsProperties & x);
    void to_json(json & j, const ConstraintsProperties & x);

    void from_json(const json & j, Constraints & x);
    void to_json(json & j, const Constraints & x);

    void from_json(const json & j, Default & x);
    void to_json(json & j, const Default & x);

    void from_json(const json & j, ParameterProperties & x);
    void to_json(json & j, const ParameterProperties & x);

    void from_json(const json & j, Parameter & x);
    void to_json(json & j, const Parameter & x);

    void from_json(const json & j, PurpleType & x);
    void to_json(json & j, const PurpleType & x);

    void from_json(const json & j, IfProperties & x);
    void to_json(json & j, const IfProperties & x);

    void from_json(const json & j, If & x);
    void to_json(json & j, const If & x);

    void from_json(const json & j, Then & x);
    void to_json(json & j, const Then & x);

    void from_json(const json & j, AllOf & x);
    void to_json(json & j, const AllOf & x);

    void from_json(const json & j, Enabled & x);
    void to_json(json & j, const Enabled & x);

    void from_json(const json & j, PassProperties & x);
    void to_json(json & j, const PassProperties & x);

    void from_json(const json & j, Pass & x);
    void to_json(json & j, const Pass & x);

    void from_json(const json & j, PassIoBindingProperties & x);
    void to_json(json & j, const PassIoBindingProperties & x);

    void from_json(const json & j, PassIoBinding & x);
    void to_json(json & j, const PassIoBinding & x);

    void from_json(const json & j, EntryPoint & x);
    void to_json(json & j, const EntryPoint & x);

    void from_json(const json & j, Value & x);
    void to_json(json & j, const Value & x);

    void from_json(const json & j, AdditionalPropertiesProperties & x);
    void to_json(json & j, const AdditionalPropertiesProperties & x);

    void from_json(const json & j, AdditionalProperties & x);
    void to_json(json & j, const AdditionalProperties & x);

    void from_json(const json & j, Uniforms & x);
    void to_json(json & j, const Uniforms & x);

    void from_json(const json & j, ShaderConfigProperties & x);
    void to_json(json & j, const ShaderConfigProperties & x);

    void from_json(const json & j, ShaderConfig & x);
    void to_json(json & j, const ShaderConfig & x);

    void from_json(const json & j, Definitions & x);
    void to_json(json & j, const Definitions & x);

    void from_json(const json & j, Metadata & x);
    void to_json(json & j, const Metadata & x);

    void from_json(const json & j, Tags & x);
    void to_json(json & j, const Tags & x);

    void from_json(const json & j, WelcomeProperties & x);
    void to_json(json & j, const WelcomeProperties & x);

    void from_json(const json & j, Welcome & x);
    void to_json(json & j, const Welcome & x);

    void from_json(const json & j, TypeEnum & x);
    void to_json(json & j, const TypeEnum & x);

    inline void from_json(const json & j, Author& x) {
        x.set_type(j.at("type").get<TypeEnum>());
        x.set_description(j.at("description").get<std::string>());
    }

    inline void to_json(json & j, const Author & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["description"] = x.get_description();
    }

    inline void from_json(const json & j, DescriptionClass& x) {
        x.set_type(j.at("type").get<TypeEnum>());
    }

    inline void to_json(json & j, const DescriptionClass & x) {
        j = json::object();
        j["type"] = x.get_type();
    }

    inline void from_json(const json & j, Axes& x) {
        x.set_type(j.at("type").get<TypeEnum>());
        x.set_items(j.at("items").get<DescriptionClass>());
    }

    inline void to_json(json & j, const Axes & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["items"] = x.get_items();
    }

    inline void from_json(const json & j, ParamsProperties& x) {
        x.set_mean(j.at("mean").get<Axes>());
        x.set_std(j.at("std").get<Axes>());
        x.set_width(j.at("width").get<DescriptionClass>());
        x.set_height(j.at("height").get<DescriptionClass>());
        x.set_method(j.at("method").get<DescriptionClass>());
        x.set_color_space(j.at("color_space").get<DescriptionClass>());
        x.set_axes(j.at("axes").get<Axes>());
        x.set_angle(j.at("angle").get<DescriptionClass>());
        x.set_custom_function(j.at("custom_function").get<DescriptionClass>());
    }

    inline void to_json(json & j, const ParamsProperties & x) {
        j = json::object();
        j["mean"] = x.get_mean();
        j["std"] = x.get_std();
        j["width"] = x.get_width();
        j["height"] = x.get_height();
        j["method"] = x.get_method();
        j["color_space"] = x.get_color_space();
        j["axes"] = x.get_axes();
        j["angle"] = x.get_angle();
        j["custom_function"] = x.get_custom_function();
    }

    inline void from_json(const json & j, Params& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_description(j.at("description").get<std::string>());
        x.set_properties(j.at("properties").get<ParamsProperties>());
    }

    inline void to_json(json & j, const Params & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["description"] = x.get_description();
        j["properties"] = x.get_properties();
    }

    inline void from_json(const json & j, DataTypeClass& x) {
        x.set_type(j.at("type").get<TypeEnum>());
        x.set_type_enum(j.at("enum").get<std::vector<std::string>>());
    }

    inline void to_json(json & j, const DataTypeClass & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["enum"] = x.get_type_enum();
    }

    inline void from_json(const json & j, DataTransformProperties& x) {
        x.set_type(j.at("type").get<DataTypeClass>());
        x.set_input(j.at("input").get<Author>());
        x.set_output(j.at("output").get<Author>());
        x.set_params(j.at("params").get<Params>());
    }

    inline void to_json(json & j, const DataTransformProperties & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["input"] = x.get_input();
        j["output"] = x.get_output();
        j["params"] = x.get_params();
    }

    inline void from_json(const json & j, DataTransform& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_required(j.at("required").get<std::vector<std::string>>());
        x.set_properties(j.at("properties").get<DataTransformProperties>());
    }

    inline void to_json(json & j, const DataTransform & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["required"] = x.get_required();
        j["properties"] = x.get_properties();
    }

    inline void from_json(const json & j, Epsilon& x) {
        x.set_type(j.at("type").get<TypeEnum>());
        x.set_minimum(j.at("minimum").get<int64_t>());
    }

    inline void to_json(json & j, const Epsilon & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["minimum"] = x.get_minimum();
    }

    inline void from_json(const json & j, DimensionsProperties& x) {
        x.set_width(j.at("width").get<Epsilon>());
        x.set_height(j.at("height").get<Epsilon>());
        x.set_channels(j.at("channels").get<Epsilon>());
        x.set_batch(j.at("batch").get<Epsilon>());
    }

    inline void to_json(json & j, const DimensionsProperties & x) {
        j = json::object();
        j["width"] = x.get_width();
        j["height"] = x.get_height();
        j["channels"] = x.get_channels();
        j["batch"] = x.get_batch();
    }

    inline void from_json(const json & j, Dimensions& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_description(j.at("description").get<std::string>());
        x.set_properties(j.at("properties").get<DimensionsProperties>());
    }

    inline void to_json(json & j, const Dimensions & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["description"] = x.get_description();
        j["properties"] = x.get_properties();
    }

    inline void from_json(const json & j, Format& x) {
        x.set_type(j.at("type").get<TypeEnum>());
        x.set_description(get_stack_optional<std::string>(j, "description"));
        x.set_format_enum(j.at("enum").get<std::vector<std::string>>());
        x.set_format_default(get_stack_optional<std::string>(j, "default"));
    }

    inline void to_json(json & j, const Format & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["description"] = x.get_description();
        j["enum"] = x.get_format_enum();
        j["default"] = x.get_format_default();
    }

    inline void from_json(const json & j, GnusSpecVersion& x) {
        x.set_type(j.at("type").get<TypeEnum>());
        x.set_description(j.at("description").get<std::string>());
        x.set_pattern(j.at("pattern").get<std::string>());
        x.set_gnus_spec_version_const(get_stack_optional<std::string>(j, "const"));
    }

    inline void to_json(json & j, const GnusSpecVersion & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["description"] = x.get_description();
        j["pattern"] = x.get_pattern();
        j["const"] = x.get_gnus_spec_version_const();
    }

    inline void from_json(const json & j, TypeClass& x) {
        x.set_ref(j.at("$ref").get<std::string>());
    }

    inline void to_json(json & j, const TypeClass & x) {
        j = json::object();
        j["$ref"] = x.get_ref();
    }

    inline void from_json(const json & j, IoDeclarationProperties& x) {
        x.set_name(j.at("name").get<GnusSpecVersion>());
        x.set_type(j.at("type").get<TypeClass>());
        x.set_description(j.at("description").get<Author>());
        x.set_dimensions(j.at("dimensions").get<Dimensions>());
        x.set_format(j.at("format").get<Format>());
    }

    inline void to_json(json & j, const IoDeclarationProperties & x) {
        j = json::object();
        j["name"] = x.get_name();
        j["type"] = x.get_type();
        j["description"] = x.get_description();
        j["dimensions"] = x.get_dimensions();
        j["format"] = x.get_format();
    }

    inline void from_json(const json & j, IoDeclaration& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_required(j.at("required").get<std::vector<std::string>>());
        x.set_properties(j.at("properties").get<IoDeclarationProperties>());
    }

    inline void to_json(json & j, const IoDeclaration & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["required"] = x.get_required();
        j["properties"] = x.get_properties();
    }

    inline void from_json(const json & j, BatchSize& x) {
        x.set_type(j.at("type").get<TypeEnum>());
        x.set_minimum(j.at("minimum").get<int64_t>());
        x.set_batch_size_default(j.at("default").get<double>());
    }

    inline void to_json(json & j, const BatchSize & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["minimum"] = x.get_minimum();
        j["default"] = x.get_batch_size_default();
    }

    inline void from_json(const json & j, Inputs& x) {
        x.set_type(j.at("type").get<TypeEnum>());
        x.set_min_items(get_stack_optional<int64_t>(j, "minItems"));
        x.set_items(j.at("items").get<TypeClass>());
        x.set_description(get_stack_optional<std::string>(j, "description"));
    }

    inline void to_json(json & j, const Inputs & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["minItems"] = x.get_min_items();
        j["items"] = x.get_items();
        j["description"] = x.get_description();
    }

    inline void from_json(const json & j, Optimizer& x) {
        x.set_ref(j.at("$ref").get<std::string>());
        x.set_description(j.at("description").get<std::string>());
    }

    inline void to_json(json & j, const Optimizer & x) {
        j = json::object();
        j["$ref"] = x.get_ref();
        j["description"] = x.get_description();
    }

    inline void from_json(const json & j, ModelConfigProperties& x) {
        x.set_source_uri_param(j.at("source_uri_param").get<Author>());
        x.set_format(j.at("format").get<Format>());
        x.set_input_nodes(j.at("input_nodes").get<Inputs>());
        x.set_output_nodes(j.at("output_nodes").get<Inputs>());
        x.set_optimizer(j.at("optimizer").get<Optimizer>());
        x.set_loss_function(j.at("loss_function").get<Format>());
        x.set_batch_size(j.at("batch_size").get<BatchSize>());
    }

    inline void to_json(json & j, const ModelConfigProperties & x) {
        j = json::object();
        j["source_uri_param"] = x.get_source_uri_param();
        j["format"] = x.get_format();
        j["input_nodes"] = x.get_input_nodes();
        j["output_nodes"] = x.get_output_nodes();
        j["optimizer"] = x.get_optimizer();
        j["loss_function"] = x.get_loss_function();
        j["batch_size"] = x.get_batch_size();
    }

    inline void from_json(const json & j, ModelConfig& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_required(j.at("required").get<std::vector<std::string>>());
        x.set_properties(j.at("properties").get<ModelConfigProperties>());
    }

    inline void to_json(json & j, const ModelConfig & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["required"] = x.get_required();
        j["properties"] = x.get_properties();
    }

    inline void from_json(const json & j, Shape& x) {
        x.set_type(j.at("type").get<TypeEnum>());
        x.set_description(j.at("description").get<std::string>());
        x.set_items(j.at("items").get<Epsilon>());
    }

    inline void to_json(json & j, const Shape & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["description"] = x.get_description();
        j["items"] = x.get_items();
    }

    inline void from_json(const json & j, ModelNodeProperties& x) {
        x.set_name(j.at("name").get<Author>());
        x.set_type(j.at("type").get<TypeClass>());
        x.set_source(j.at("source").get<GnusSpecVersion>());
        x.set_target(j.at("target").get<GnusSpecVersion>());
        x.set_shape(j.at("shape").get<Shape>());
    }

    inline void to_json(json & j, const ModelNodeProperties & x) {
        j = json::object();
        j["name"] = x.get_name();
        j["type"] = x.get_type();
        j["source"] = x.get_source();
        j["target"] = x.get_target();
        j["shape"] = x.get_shape();
    }

    inline void from_json(const json & j, ModelNode& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_required(j.at("required").get<std::vector<std::string>>());
        x.set_properties(j.at("properties").get<ModelNodeProperties>());
    }

    inline void to_json(json & j, const ModelNode & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["required"] = x.get_required();
        j["properties"] = x.get_properties();
    }

    inline void from_json(const json & j, Beta1& x) {
        x.set_type(j.at("type").get<TypeEnum>());
        x.set_minimum(j.at("minimum").get<int64_t>());
        x.set_maximum(j.at("maximum").get<int64_t>());
    }

    inline void to_json(json & j, const Beta1 & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["minimum"] = x.get_minimum();
        j["maximum"] = x.get_maximum();
    }

    inline void from_json(const json & j, OptimizerConfigProperties& x) {
        x.set_type(j.at("type").get<DataTypeClass>());
        x.set_learning_rate(j.at("learning_rate").get<BatchSize>());
        x.set_momentum(j.at("momentum").get<Beta1>());
        x.set_weight_decay(j.at("weight_decay").get<Epsilon>());
        x.set_beta1(j.at("beta1").get<Beta1>());
        x.set_beta2(j.at("beta2").get<Beta1>());
        x.set_epsilon(j.at("epsilon").get<Epsilon>());
    }

    inline void to_json(json & j, const OptimizerConfigProperties & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["learning_rate"] = x.get_learning_rate();
        j["momentum"] = x.get_momentum();
        j["weight_decay"] = x.get_weight_decay();
        j["beta1"] = x.get_beta1();
        j["beta2"] = x.get_beta2();
        j["epsilon"] = x.get_epsilon();
    }

    inline void from_json(const json & j, OptimizerConfig& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_required(j.at("required").get<std::vector<std::string>>());
        x.set_properties(j.at("properties").get<OptimizerConfigProperties>());
    }

    inline void to_json(json & j, const OptimizerConfig & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["required"] = x.get_required();
        j["properties"] = x.get_properties();
    }

    inline void from_json(const json & j, ConstraintsProperties& x) {
        x.set_min(j.at("min").get<DescriptionClass>());
        x.set_max(j.at("max").get<DescriptionClass>());
        x.set_properties_enum(j.at("enum").get<DescriptionClass>());
        x.set_pattern(j.at("pattern").get<DescriptionClass>());
    }

    inline void to_json(json & j, const ConstraintsProperties & x) {
        j = json::object();
        j["min"] = x.get_min();
        j["max"] = x.get_max();
        j["enum"] = x.get_properties_enum();
        j["pattern"] = x.get_pattern();
    }

    inline void from_json(const json & j, Constraints& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_properties(j.at("properties").get<ConstraintsProperties>());
    }

    inline void to_json(json & j, const Constraints & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["properties"] = x.get_properties();
    }

    inline void from_json(const json & j, Default& x) {
        x.set_description(j.at("description").get<std::string>());
    }

    inline void to_json(json & j, const Default & x) {
        j = json::object();
        j["description"] = x.get_description();
    }

    inline void from_json(const json & j, ParameterProperties& x) {
        x.set_name(j.at("name").get<GnusSpecVersion>());
        x.set_type(j.at("type").get<DataTypeClass>());
        x.set_properties_default(j.at("default").get<Default>());
        x.set_description(j.at("description").get<DescriptionClass>());
        x.set_constraints(j.at("constraints").get<Constraints>());
    }

    inline void to_json(json & j, const ParameterProperties & x) {
        j = json::object();
        j["name"] = x.get_name();
        j["type"] = x.get_type();
        j["default"] = x.get_properties_default();
        j["description"] = x.get_description();
        j["constraints"] = x.get_constraints();
    }

    inline void from_json(const json & j, Parameter& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_required(j.at("required").get<std::vector<std::string>>());
        x.set_properties(j.at("properties").get<ParameterProperties>());
    }

    inline void to_json(json & j, const Parameter & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["required"] = x.get_required();
        j["properties"] = x.get_properties();
    }

    inline void from_json(const json & j, PurpleType& x) {
        x.set_type_enum(j.at("enum").get<std::vector<std::string>>());
    }

    inline void to_json(json & j, const PurpleType & x) {
        j = json::object();
        j["enum"] = x.get_type_enum();
    }

    inline void from_json(const json & j, IfProperties& x) {
        x.set_type(j.at("type").get<PurpleType>());
    }

    inline void to_json(json & j, const IfProperties & x) {
        j = json::object();
        j["type"] = x.get_type();
    }

    inline void from_json(const json & j, If& x) {
        x.set_properties(j.at("properties").get<IfProperties>());
    }

    inline void to_json(json & j, const If & x) {
        j = json::object();
        j["properties"] = x.get_properties();
    }

    inline void from_json(const json & j, Then& x) {
        x.set_required(j.at("required").get<std::vector<std::string>>());
    }

    inline void to_json(json & j, const Then & x) {
        j = json::object();
        j["required"] = x.get_required();
    }

    inline void from_json(const json & j, AllOf& x) {
        x.set_all_of_if(j.at("if").get<If>());
        x.set_then(j.at("then").get<Then>());
    }

    inline void to_json(json & j, const AllOf & x) {
        j = json::object();
        j["if"] = x.get_all_of_if();
        j["then"] = x.get_then();
    }

    inline void from_json(const json & j, Enabled& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_enabled_default(j.at("default").get<bool>());
        x.set_description(j.at("description").get<std::string>());
    }

    inline void to_json(json & j, const Enabled & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["default"] = x.get_enabled_default();
        j["description"] = x.get_description();
    }

    inline void from_json(const json & j, PassProperties& x) {
        x.set_name(j.at("name").get<GnusSpecVersion>());
        x.set_type(j.at("type").get<Format>());
        x.set_description(j.at("description").get<DescriptionClass>());
        x.set_enabled(j.at("enabled").get<Enabled>());
        x.set_model(j.at("model").get<Optimizer>());
        x.set_shader(j.at("shader").get<Optimizer>());
        x.set_data_transforms(j.at("data_transforms").get<Inputs>());
        x.set_inputs(j.at("inputs").get<Inputs>());
        x.set_outputs(j.at("outputs").get<Inputs>());
    }

    inline void to_json(json & j, const PassProperties & x) {
        j = json::object();
        j["name"] = x.get_name();
        j["type"] = x.get_type();
        j["description"] = x.get_description();
        j["enabled"] = x.get_enabled();
        j["model"] = x.get_model();
        j["shader"] = x.get_shader();
        j["data_transforms"] = x.get_data_transforms();
        j["inputs"] = x.get_inputs();
        j["outputs"] = x.get_outputs();
    }

    inline void from_json(const json & j, Pass& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_required(j.at("required").get<std::vector<std::string>>());
        x.set_properties(j.at("properties").get<PassProperties>());
        x.set_all_of(j.at("allOf").get<std::vector<AllOf>>());
    }

    inline void to_json(json & j, const Pass & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["required"] = x.get_required();
        j["properties"] = x.get_properties();
        j["allOf"] = x.get_all_of();
    }

    inline void from_json(const json & j, PassIoBindingProperties& x) {
        x.set_name(j.at("name").get<Author>());
        x.set_source(j.at("source").get<GnusSpecVersion>());
        x.set_target(j.at("target").get<GnusSpecVersion>());
    }

    inline void to_json(json & j, const PassIoBindingProperties & x) {
        j = json::object();
        j["name"] = x.get_name();
        j["source"] = x.get_source();
        j["target"] = x.get_target();
    }

    inline void from_json(const json & j, PassIoBinding& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_required(j.at("required").get<std::vector<std::string>>());
        x.set_properties(j.at("properties").get<PassIoBindingProperties>());
    }

    inline void to_json(json & j, const PassIoBinding & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["required"] = x.get_required();
        j["properties"] = x.get_properties();
    }

    inline void from_json(const json & j, EntryPoint& x) {
        x.set_type(j.at("type").get<TypeEnum>());
        x.set_entry_point_default(j.at("default").get<std::string>());
    }

    inline void to_json(json & j, const EntryPoint & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["default"] = x.get_entry_point_default();
    }

    inline void from_json(const json & j, Value& x) {
    }

    inline void to_json(json & j, const Value & x) {
        j = json::object();
    }

    inline void from_json(const json & j, AdditionalPropertiesProperties& x) {
        x.set_type(j.at("type").get<TypeClass>());
        x.set_value(j.at("value").get<Value>());
        x.set_source(j.at("source").get<DescriptionClass>());
    }

    inline void to_json(json & j, const AdditionalPropertiesProperties & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["value"] = x.get_value();
        j["source"] = x.get_source();
    }

    inline void from_json(const json & j, AdditionalProperties& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_properties(j.at("properties").get<AdditionalPropertiesProperties>());
    }

    inline void to_json(json & j, const AdditionalProperties & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["properties"] = x.get_properties();
    }

    inline void from_json(const json & j, Uniforms& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_description(j.at("description").get<std::string>());
        x.set_additional_properties(j.at("additionalProperties").get<AdditionalProperties>());
    }

    inline void to_json(json & j, const Uniforms & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["description"] = x.get_description();
        j["additionalProperties"] = x.get_additional_properties();
    }

    inline void from_json(const json & j, ShaderConfigProperties& x) {
        x.set_source(j.at("source").get<Author>());
        x.set_type(j.at("type").get<Format>());
        x.set_entry_point(j.at("entry_point").get<EntryPoint>());
        x.set_uniforms(j.at("uniforms").get<Uniforms>());
    }

    inline void to_json(json & j, const ShaderConfigProperties & x) {
        j = json::object();
        j["source"] = x.get_source();
        j["type"] = x.get_type();
        j["entry_point"] = x.get_entry_point();
        j["uniforms"] = x.get_uniforms();
    }

    inline void from_json(const json & j, ShaderConfig& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_required(j.at("required").get<std::vector<std::string>>());
        x.set_properties(j.at("properties").get<ShaderConfigProperties>());
    }

    inline void to_json(json & j, const ShaderConfig & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["required"] = x.get_required();
        j["properties"] = x.get_properties();
    }

    inline void from_json(const json & j, Definitions& x) {
        x.set_io_declaration(j.at("io_declaration").get<IoDeclaration>());
        x.set_parameter(j.at("parameter").get<Parameter>());
        x.set_pass(j.at("pass").get<Pass>());
        x.set_model_config(j.at("model_config").get<ModelConfig>());
        x.set_model_node(j.at("model_node").get<ModelNode>());
        x.set_shader_config(j.at("shader_config").get<ShaderConfig>());
        x.set_pass_io_binding(j.at("pass_io_binding").get<PassIoBinding>());
        x.set_data_transform(j.at("data_transform").get<DataTransform>());
        x.set_optimizer_config(j.at("optimizer_config").get<OptimizerConfig>());
        x.set_data_type(j.at("data_type").get<DataTypeClass>());
    }

    inline void to_json(json & j, const Definitions & x) {
        j = json::object();
        j["io_declaration"] = x.get_io_declaration();
        j["parameter"] = x.get_parameter();
        j["pass"] = x.get_pass();
        j["model_config"] = x.get_model_config();
        j["model_node"] = x.get_model_node();
        j["shader_config"] = x.get_shader_config();
        j["pass_io_binding"] = x.get_pass_io_binding();
        j["data_transform"] = x.get_data_transform();
        j["optimizer_config"] = x.get_optimizer_config();
        j["data_type"] = x.get_data_type();
    }

    inline void from_json(const json & j, Metadata& x) {
        x.set_type(j.at("type").get<std::string>());
        x.set_description(j.at("description").get<std::string>());
        x.set_additional_properties(j.at("additionalProperties").get<bool>());
    }

    inline void to_json(json & j, const Metadata & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["description"] = x.get_description();
        j["additionalProperties"] = x.get_additional_properties();
    }

    inline void from_json(const json & j, Tags& x) {
        x.set_type(j.at("type").get<TypeEnum>());
        x.set_description(j.at("description").get<std::string>());
        x.set_items(j.at("items").get<DescriptionClass>());
    }

    inline void to_json(json & j, const Tags & x) {
        j = json::object();
        j["type"] = x.get_type();
        j["description"] = x.get_description();
        j["items"] = x.get_items();
    }

    inline void from_json(const json & j, WelcomeProperties& x) {
        x.set_name(j.at("name").get<GnusSpecVersion>());
        x.set_version(j.at("version").get<GnusSpecVersion>());
        x.set_gnus_spec_version(j.at("gnus_spec_version").get<GnusSpecVersion>());
        x.set_author(j.at("author").get<Author>());
        x.set_description(j.at("description").get<Author>());
        x.set_tags(j.at("tags").get<Tags>());
        x.set_inputs(j.at("inputs").get<Inputs>());
        x.set_outputs(j.at("outputs").get<Inputs>());
        x.set_parameters(j.at("parameters").get<Inputs>());
        x.set_passes(j.at("passes").get<Inputs>());
        x.set_metadata(j.at("metadata").get<Metadata>());
    }

    inline void to_json(json & j, const WelcomeProperties & x) {
        j = json::object();
        j["name"] = x.get_name();
        j["version"] = x.get_version();
        j["gnus_spec_version"] = x.get_gnus_spec_version();
        j["author"] = x.get_author();
        j["description"] = x.get_description();
        j["tags"] = x.get_tags();
        j["inputs"] = x.get_inputs();
        j["outputs"] = x.get_outputs();
        j["parameters"] = x.get_parameters();
        j["passes"] = x.get_passes();
        j["metadata"] = x.get_metadata();
    }

    inline void from_json(const json & j, Welcome& x) {
        x.set_schema(j.at("$schema").get<std::string>());
        x.set_id(j.at("$id").get<std::string>());
        x.set_title(j.at("title").get<std::string>());
        x.set_description(j.at("description").get<std::string>());
        x.set_type(j.at("type").get<std::string>());
        x.set_required(j.at("required").get<std::vector<std::string>>());
        x.set_properties(j.at("properties").get<WelcomeProperties>());
        x.set_definitions(j.at("definitions").get<Definitions>());
    }

    inline void to_json(json & j, const Welcome & x) {
        j = json::object();
        j["$schema"] = x.get_schema();
        j["$id"] = x.get_id();
        j["title"] = x.get_title();
        j["description"] = x.get_description();
        j["type"] = x.get_type();
        j["required"] = x.get_required();
        j["properties"] = x.get_properties();
        j["definitions"] = x.get_definitions();
    }

    inline void from_json(const json & j, TypeEnum & x) {
        if (j == "array") x = TypeEnum::ARRAY;
        else if (j == "integer") x = TypeEnum::INTEGER;
        else if (j == "number") x = TypeEnum::NUMBER;
        else if (j == "string") x = TypeEnum::STRING;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const TypeEnum & x) {
        switch (x) {
            case TypeEnum::ARRAY: j = "array"; break;
            case TypeEnum::INTEGER: j = "integer"; break;
            case TypeEnum::NUMBER: j = "number"; break;
            case TypeEnum::STRING: j = "string"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"TypeEnum\": " + std::to_string(static_cast<int>(x)));
        }
    }
}
