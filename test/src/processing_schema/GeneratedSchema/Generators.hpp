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

#include "SgnsProcessing.hpp"
#include "Pass.hpp"
#include "PassType.hpp"
#include "ShaderConfig.hpp"
#include "Uniform.hpp"
#include "ShaderType.hpp"
#include "ModelConfig.hpp"
#include "OptimizerConfig.hpp"
#include "OptimizerType.hpp"
#include "LossFunction.hpp"
#include "ModelNode.hpp"
#include "ModelFormat.hpp"
#include "PassIoBinding.hpp"
#include "DataTransform.hpp"
#include "DataTransformType.hpp"
#include "Params.hpp"
#include "Parameter.hpp"
#include "ParameterType.hpp"
#include "Constraints.hpp"
#include "IoDeclaration.hpp"
#include "DataType.hpp"
#include "InputFormat.hpp"
#include "Dimensions.hpp"

namespace sgns {
    void from_json(const json & j, Dimensions & x);
    void to_json(json & j, const Dimensions & x);

    void from_json(const json & j, IoDeclaration & x);
    void to_json(json & j, const IoDeclaration & x);

    void from_json(const json & j, Constraints & x);
    void to_json(json & j, const Constraints & x);

    void from_json(const json & j, Parameter & x);
    void to_json(json & j, const Parameter & x);

    void from_json(const json & j, Params & x);
    void to_json(json & j, const Params & x);

    void from_json(const json & j, DataTransform & x);
    void to_json(json & j, const DataTransform & x);

    void from_json(const json & j, PassIoBinding & x);
    void to_json(json & j, const PassIoBinding & x);

    void from_json(const json & j, ModelNode & x);
    void to_json(json & j, const ModelNode & x);

    void from_json(const json & j, OptimizerConfig & x);
    void to_json(json & j, const OptimizerConfig & x);

    void from_json(const json & j, ModelConfig & x);
    void to_json(json & j, const ModelConfig & x);

    void from_json(const json & j, Uniform & x);
    void to_json(json & j, const Uniform & x);

    void from_json(const json & j, ShaderConfig & x);
    void to_json(json & j, const ShaderConfig & x);

    void from_json(const json & j, Pass & x);
    void to_json(json & j, const Pass & x);

    void from_json(const json & j, SgnsProcessing & x);
    void to_json(json & j, const SgnsProcessing & x);

    void from_json(const json & j, InputFormat & x);
    void to_json(json & j, const InputFormat & x);

    void from_json(const json & j, DataType & x);
    void to_json(json & j, const DataType & x);

    void from_json(const json & j, ParameterType & x);
    void to_json(json & j, const ParameterType & x);

    void from_json(const json & j, DataTransformType & x);
    void to_json(json & j, const DataTransformType & x);

    void from_json(const json & j, ModelFormat & x);
    void to_json(json & j, const ModelFormat & x);

    void from_json(const json & j, LossFunction & x);
    void to_json(json & j, const LossFunction & x);

    void from_json(const json & j, OptimizerType & x);
    void to_json(json & j, const OptimizerType & x);

    void from_json(const json & j, ShaderType & x);
    void to_json(json & j, const ShaderType & x);

    void from_json(const json & j, PassType & x);
    void to_json(json & j, const PassType & x);

    inline void from_json(const json & j, Dimensions& x) {
        x.set_batch(get_stack_optional<int64_t>(j, "batch"));
        x.set_channels(get_stack_optional<int64_t>(j, "channels"));
        x.set_height(get_stack_optional<int64_t>(j, "height"));
        x.set_width(get_stack_optional<int64_t>(j, "width"));
    }

    inline void to_json(json & j, const Dimensions & x) {
        j = json::object();
        j["batch"] = x.get_batch();
        j["channels"] = x.get_channels();
        j["height"] = x.get_height();
        j["width"] = x.get_width();
    }

    inline void from_json(const json & j, IoDeclaration& x) {
        x.set_description(get_stack_optional<std::string>(j, "description"));
        x.set_dimensions(get_stack_optional<Dimensions>(j, "dimensions"));
        x.set_format(get_stack_optional<InputFormat>(j, "format"));
        x.set_name(j.at("name").get<std::string>());
        x.set_type(j.at("type").get<DataType>());
    }

    inline void to_json(json & j, const IoDeclaration & x) {
        j = json::object();
        j["description"] = x.get_description();
        j["dimensions"] = x.get_dimensions();
        j["format"] = x.get_format();
        j["name"] = x.get_name();
        j["type"] = x.get_type();
    }

    inline void from_json(const json & j, Constraints& x) {
        x.set_constraints_enum(get_stack_optional<std::vector<nlohmann::json>>(j, "enum"));
        x.set_max(get_stack_optional<double>(j, "max"));
        x.set_min(get_stack_optional<double>(j, "min"));
        x.set_pattern(get_stack_optional<std::string>(j, "pattern"));
    }

    inline void to_json(json & j, const Constraints & x) {
        j = json::object();
        j["enum"] = x.get_constraints_enum();
        j["max"] = x.get_max();
        j["min"] = x.get_min();
        j["pattern"] = x.get_pattern();
    }

    inline void from_json(const json & j, Parameter& x) {
        x.set_constraints(get_stack_optional<Constraints>(j, "constraints"));
        x.set_parameter_default(get_untyped(j, "default"));
        x.set_description(get_stack_optional<std::string>(j, "description"));
        x.set_name(j.at("name").get<std::string>());
        x.set_type(j.at("type").get<ParameterType>());
    }

    inline void to_json(json & j, const Parameter & x) {
        j = json::object();
        j["constraints"] = x.get_constraints();
        j["default"] = x.get_parameter_default();
        j["description"] = x.get_description();
        j["name"] = x.get_name();
        j["type"] = x.get_type();
    }

    inline void from_json(const json & j, Params& x) {
        x.set_angle(get_stack_optional<double>(j, "angle"));
        x.set_axes(get_stack_optional<std::vector<int64_t>>(j, "axes"));
        x.set_color_space(get_stack_optional<std::string>(j, "color_space"));
        x.set_custom_function(get_stack_optional<std::string>(j, "custom_function"));
        x.set_height(get_stack_optional<int64_t>(j, "height"));
        x.set_mean(get_stack_optional<std::vector<double>>(j, "mean"));
        x.set_method(get_stack_optional<std::string>(j, "method"));
        x.set_std(get_stack_optional<std::vector<double>>(j, "std"));
        x.set_width(get_stack_optional<int64_t>(j, "width"));
    }

    inline void to_json(json & j, const Params & x) {
        j = json::object();
        j["angle"] = x.get_angle();
        j["axes"] = x.get_axes();
        j["color_space"] = x.get_color_space();
        j["custom_function"] = x.get_custom_function();
        j["height"] = x.get_height();
        j["mean"] = x.get_mean();
        j["method"] = x.get_method();
        j["std"] = x.get_std();
        j["width"] = x.get_width();
    }

    inline void from_json(const json & j, DataTransform& x) {
        x.set_input(j.at("input").get<std::string>());
        x.set_output(j.at("output").get<std::string>());
        x.set_params(get_stack_optional<Params>(j, "params"));
        x.set_type(j.at("type").get<DataTransformType>());
    }

    inline void to_json(json & j, const DataTransform & x) {
        j = json::object();
        j["input"] = x.get_input();
        j["output"] = x.get_output();
        j["params"] = x.get_params();
        j["type"] = x.get_type();
    }

    inline void from_json(const json & j, PassIoBinding& x) {
        x.set_name(j.at("name").get<std::string>());
        x.set_source(get_stack_optional<std::string>(j, "source"));
        x.set_target(get_stack_optional<std::string>(j, "target"));
    }

    inline void to_json(json & j, const PassIoBinding & x) {
        j = json::object();
        j["name"] = x.get_name();
        j["source"] = x.get_source();
        j["target"] = x.get_target();
    }

    inline void from_json(const json & j, ModelNode& x) {
        x.set_name(j.at("name").get<std::string>());
        x.set_shape(get_stack_optional<std::vector<int64_t>>(j, "shape"));
        x.set_source(get_stack_optional<std::string>(j, "source"));
        x.set_target(get_stack_optional<std::string>(j, "target"));
        x.set_type(j.at("type").get<DataType>());
    }

    inline void to_json(json & j, const ModelNode & x) {
        j = json::object();
        j["name"] = x.get_name();
        j["shape"] = x.get_shape();
        j["source"] = x.get_source();
        j["target"] = x.get_target();
        j["type"] = x.get_type();
    }

    inline void from_json(const json & j, OptimizerConfig& x) {
        x.set_beta1(get_stack_optional<double>(j, "beta1"));
        x.set_beta2(get_stack_optional<double>(j, "beta2"));
        x.set_epsilon(get_stack_optional<double>(j, "epsilon"));
        x.set_learning_rate(get_stack_optional<double>(j, "learning_rate"));
        x.set_momentum(get_stack_optional<double>(j, "momentum"));
        x.set_type(j.at("type").get<OptimizerType>());
        x.set_weight_decay(get_stack_optional<double>(j, "weight_decay"));
    }

    inline void to_json(json & j, const OptimizerConfig & x) {
        j = json::object();
        j["beta1"] = x.get_beta1();
        j["beta2"] = x.get_beta2();
        j["epsilon"] = x.get_epsilon();
        j["learning_rate"] = x.get_learning_rate();
        j["momentum"] = x.get_momentum();
        j["type"] = x.get_type();
        j["weight_decay"] = x.get_weight_decay();
    }

    inline void from_json(const json & j, ModelConfig& x) {
        x.set_batch_size(get_stack_optional<int64_t>(j, "batch_size"));
        x.set_format(get_stack_optional<ModelFormat>(j, "format"));
        x.set_input_nodes(j.at("input_nodes").get<std::vector<ModelNode>>());
        x.set_loss_function(get_stack_optional<LossFunction>(j, "loss_function"));
        x.set_optimizer(get_stack_optional<OptimizerConfig>(j, "optimizer"));
        x.set_output_nodes(j.at("output_nodes").get<std::vector<ModelNode>>());
        x.set_source_uri_param(j.at("source_uri_param").get<std::string>());
    }

    inline void to_json(json & j, const ModelConfig & x) {
        j = json::object();
        j["batch_size"] = x.get_batch_size();
        j["format"] = x.get_format();
        j["input_nodes"] = x.get_input_nodes();
        j["loss_function"] = x.get_loss_function();
        j["optimizer"] = x.get_optimizer();
        j["output_nodes"] = x.get_output_nodes();
        j["source_uri_param"] = x.get_source_uri_param();
    }

    inline void from_json(const json & j, Uniform& x) {
        x.set_source(get_stack_optional<std::string>(j, "source"));
        x.set_type(get_stack_optional<DataType>(j, "type"));
        x.set_value(get_untyped(j, "value"));
    }

    inline void to_json(json & j, const Uniform & x) {
        j = json::object();
        j["source"] = x.get_source();
        j["type"] = x.get_type();
        j["value"] = x.get_value();
    }

    inline void from_json(const json & j, ShaderConfig& x) {
        x.set_entry_point(get_stack_optional<std::string>(j, "entry_point"));
        x.set_source(j.at("source").get<std::string>());
        x.set_type(get_stack_optional<ShaderType>(j, "type"));
        x.set_uniforms(get_stack_optional<std::map<std::string, Uniform>>(j, "uniforms"));
    }

    inline void to_json(json & j, const ShaderConfig & x) {
        j = json::object();
        j["entry_point"] = x.get_entry_point();
        j["source"] = x.get_source();
        j["type"] = x.get_type();
        j["uniforms"] = x.get_uniforms();
    }

    inline void from_json(const json & j, Pass& x) {
        x.set_data_transforms(get_stack_optional<std::vector<DataTransform>>(j, "data_transforms"));
        x.set_description(get_stack_optional<std::string>(j, "description"));
        x.set_enabled(get_stack_optional<bool>(j, "enabled"));
        x.set_inputs(get_stack_optional<std::vector<PassIoBinding>>(j, "inputs"));
        x.set_model(get_stack_optional<ModelConfig>(j, "model"));
        x.set_name(j.at("name").get<std::string>());
        x.set_outputs(get_stack_optional<std::vector<PassIoBinding>>(j, "outputs"));
        x.set_shader(get_stack_optional<ShaderConfig>(j, "shader"));
        x.set_type(j.at("type").get<PassType>());
    }

    inline void to_json(json & j, const Pass & x) {
        j = json::object();
        j["data_transforms"] = x.get_data_transforms();
        j["description"] = x.get_description();
        j["enabled"] = x.get_enabled();
        j["inputs"] = x.get_inputs();
        j["model"] = x.get_model();
        j["name"] = x.get_name();
        j["outputs"] = x.get_outputs();
        j["shader"] = x.get_shader();
        j["type"] = x.get_type();
    }

    inline void from_json(const json & j, SgnsProcessing& x) {
        x.set_author(get_stack_optional<std::string>(j, "author"));
        x.set_description(get_stack_optional<std::string>(j, "description"));
        x.set_gnus_spec_version(j.at("gnus_spec_version").get<double>());
        x.set_inputs(j.at("inputs").get<std::vector<IoDeclaration>>());
        x.set_metadata(get_stack_optional<std::map<std::string, nlohmann::json>>(j, "metadata"));
        x.set_name(j.at("name").get<std::string>());
        x.set_outputs(j.at("outputs").get<std::vector<IoDeclaration>>());
        x.set_parameters(get_stack_optional<std::vector<Parameter>>(j, "parameters"));
        x.set_passes(j.at("passes").get<std::vector<Pass>>());
        x.set_tags(get_stack_optional<std::vector<std::string>>(j, "tags"));
        x.set_version(j.at("version").get<std::string>());
    }

    inline void to_json(json & j, const SgnsProcessing & x) {
        j = json::object();
        j["author"] = x.get_author();
        j["description"] = x.get_description();
        j["gnus_spec_version"] = x.get_gnus_spec_version();
        j["inputs"] = x.get_inputs();
        j["metadata"] = x.get_metadata();
        j["name"] = x.get_name();
        j["outputs"] = x.get_outputs();
        j["parameters"] = x.get_parameters();
        j["passes"] = x.get_passes();
        j["tags"] = x.get_tags();
        j["version"] = x.get_version();
    }

    inline void from_json(const json & j, InputFormat & x) {
        if (j == "FLOAT16") x = InputFormat::FLOAT16;
        else if (j == "FLOAT32") x = InputFormat::FLOAT32;
        else if (j == "INT16") x = InputFormat::INT16;
        else if (j == "INT32") x = InputFormat::INT32;
        else if (j == "INT8") x = InputFormat::INT8;
        else if (j == "RGB8") x = InputFormat::RGB8;
        else if (j == "RGBA8") x = InputFormat::RGBA8;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const InputFormat & x) {
        switch (x) {
            case InputFormat::FLOAT16: j = "FLOAT16"; break;
            case InputFormat::FLOAT32: j = "FLOAT32"; break;
            case InputFormat::INT16: j = "INT16"; break;
            case InputFormat::INT32: j = "INT32"; break;
            case InputFormat::INT8: j = "INT8"; break;
            case InputFormat::RGB8: j = "RGB8"; break;
            case InputFormat::RGBA8: j = "RGBA8"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"InputFormat\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, DataType & x) {
        static std::unordered_map<std::string, DataType> enumValues {
            {"bool", DataType::BOOL},
            {"buffer", DataType::BUFFER},
            {"float", DataType::FLOAT},
            {"int", DataType::INT},
            {"mat2", DataType::MAT2},
            {"mat3", DataType::MAT3},
            {"mat4", DataType::MAT4},
            {"string", DataType::STRING},
            {"tensor", DataType::TENSOR},
            {"texture1D", DataType::TEXTURE1_D},
            {"texture1D_array", DataType::TEXTURE1_D_ARRAY},
            {"texture2D", DataType::TEXTURE2_D},
            {"texture2D_array", DataType::TEXTURE2_D_ARRAY},
            {"texture3D", DataType::TEXTURE3_D},
            {"texture3D_array", DataType::TEXTURE3_D_ARRAY},
            {"textureCube", DataType::TEXTURE_CUBE},
            {"vec2", DataType::VEC2},
            {"vec3", DataType::VEC3},
            {"vec4", DataType::VEC4},
        };
        auto iter = enumValues.find(j.get<std::string>());
        if (iter != enumValues.end()) {
            x = iter->second;
        }
    }

    inline void to_json(json & j, const DataType & x) {
        switch (x) {
            case DataType::BOOL: j = "bool"; break;
            case DataType::BUFFER: j = "buffer"; break;
            case DataType::FLOAT: j = "float"; break;
            case DataType::INT: j = "int"; break;
            case DataType::MAT2: j = "mat2"; break;
            case DataType::MAT3: j = "mat3"; break;
            case DataType::MAT4: j = "mat4"; break;
            case DataType::STRING: j = "string"; break;
            case DataType::TENSOR: j = "tensor"; break;
            case DataType::TEXTURE1_D: j = "texture1D"; break;
            case DataType::TEXTURE1_D_ARRAY: j = "texture1D_array"; break;
            case DataType::TEXTURE2_D: j = "texture2D"; break;
            case DataType::TEXTURE2_D_ARRAY: j = "texture2D_array"; break;
            case DataType::TEXTURE3_D: j = "texture3D"; break;
            case DataType::TEXTURE3_D_ARRAY: j = "texture3D_array"; break;
            case DataType::TEXTURE_CUBE: j = "textureCube"; break;
            case DataType::VEC2: j = "vec2"; break;
            case DataType::VEC3: j = "vec3"; break;
            case DataType::VEC4: j = "vec4"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"DataType\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, ParameterType & x) {
        if (j == "array") x = ParameterType::ARRAY;
        else if (j == "bool") x = ParameterType::BOOL;
        else if (j == "float") x = ParameterType::FLOAT;
        else if (j == "int") x = ParameterType::INT;
        else if (j == "object") x = ParameterType::OBJECT;
        else if (j == "string") x = ParameterType::STRING;
        else if (j == "uri") x = ParameterType::URI;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const ParameterType & x) {
        switch (x) {
            case ParameterType::ARRAY: j = "array"; break;
            case ParameterType::BOOL: j = "bool"; break;
            case ParameterType::FLOAT: j = "float"; break;
            case ParameterType::INT: j = "int"; break;
            case ParameterType::OBJECT: j = "object"; break;
            case ParameterType::STRING: j = "string"; break;
            case ParameterType::URI: j = "uri"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"ParameterType\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, DataTransformType & x) {
        if (j == "color_convert") x = DataTransformType::COLOR_CONVERT;
        else if (j == "crop") x = DataTransformType::CROP;
        else if (j == "custom") x = DataTransformType::CUSTOM;
        else if (j == "denormalize") x = DataTransformType::DENORMALIZE;
        else if (j == "dequantize") x = DataTransformType::DEQUANTIZE;
        else if (j == "flip") x = DataTransformType::FLIP;
        else if (j == "normalize") x = DataTransformType::NORMALIZE;
        else if (j == "pad") x = DataTransformType::PAD;
        else if (j == "quantize") x = DataTransformType::QUANTIZE;
        else if (j == "resize") x = DataTransformType::RESIZE;
        else if (j == "rotate") x = DataTransformType::ROTATE;
        else if (j == "transpose") x = DataTransformType::TRANSPOSE;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const DataTransformType & x) {
        switch (x) {
            case DataTransformType::COLOR_CONVERT: j = "color_convert"; break;
            case DataTransformType::CROP: j = "crop"; break;
            case DataTransformType::CUSTOM: j = "custom"; break;
            case DataTransformType::DENORMALIZE: j = "denormalize"; break;
            case DataTransformType::DEQUANTIZE: j = "dequantize"; break;
            case DataTransformType::FLIP: j = "flip"; break;
            case DataTransformType::NORMALIZE: j = "normalize"; break;
            case DataTransformType::PAD: j = "pad"; break;
            case DataTransformType::QUANTIZE: j = "quantize"; break;
            case DataTransformType::RESIZE: j = "resize"; break;
            case DataTransformType::ROTATE: j = "rotate"; break;
            case DataTransformType::TRANSPOSE: j = "transpose"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"DataTransformType\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, ModelFormat & x) {
        if (j == "MNN") x = ModelFormat::MNN;
        else if (j == "ONNX") x = ModelFormat::ONNX;
        else if (j == "PyTorch") x = ModelFormat::PY_TORCH;
        else if (j == "TensorFlow") x = ModelFormat::TENSOR_FLOW;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const ModelFormat & x) {
        switch (x) {
            case ModelFormat::MNN: j = "MNN"; break;
            case ModelFormat::ONNX: j = "ONNX"; break;
            case ModelFormat::PY_TORCH: j = "PyTorch"; break;
            case ModelFormat::TENSOR_FLOW: j = "TensorFlow"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"ModelFormat\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, LossFunction & x) {
        if (j == "binary_cross_entropy") x = LossFunction::BINARY_CROSS_ENTROPY;
        else if (j == "cross_entropy") x = LossFunction::CROSS_ENTROPY;
        else if (j == "custom") x = LossFunction::CUSTOM;
        else if (j == "huber_loss") x = LossFunction::HUBER_LOSS;
        else if (j == "l1_loss") x = LossFunction::L1_LOSS;
        else if (j == "mean_squared_error") x = LossFunction::MEAN_SQUARED_ERROR;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const LossFunction & x) {
        switch (x) {
            case LossFunction::BINARY_CROSS_ENTROPY: j = "binary_cross_entropy"; break;
            case LossFunction::CROSS_ENTROPY: j = "cross_entropy"; break;
            case LossFunction::CUSTOM: j = "custom"; break;
            case LossFunction::HUBER_LOSS: j = "huber_loss"; break;
            case LossFunction::L1_LOSS: j = "l1_loss"; break;
            case LossFunction::MEAN_SQUARED_ERROR: j = "mean_squared_error"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"LossFunction\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, OptimizerType & x) {
        if (j == "adadelta") x = OptimizerType::ADADELTA;
        else if (j == "adagrad") x = OptimizerType::ADAGRAD;
        else if (j == "adam") x = OptimizerType::ADAM;
        else if (j == "adamw") x = OptimizerType::ADAMW;
        else if (j == "rmsprop") x = OptimizerType::RMSPROP;
        else if (j == "sgd") x = OptimizerType::SGD;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const OptimizerType & x) {
        switch (x) {
            case OptimizerType::ADADELTA: j = "adadelta"; break;
            case OptimizerType::ADAGRAD: j = "adagrad"; break;
            case OptimizerType::ADAM: j = "adam"; break;
            case OptimizerType::ADAMW: j = "adamw"; break;
            case OptimizerType::RMSPROP: j = "rmsprop"; break;
            case OptimizerType::SGD: j = "sgd"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"OptimizerType\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, ShaderType & x) {
        if (j == "glsl") x = ShaderType::GLSL;
        else if (j == "hlsl") x = ShaderType::HLSL;
        else if (j == "metal") x = ShaderType::METAL;
        else if (j == "spirv") x = ShaderType::SPIRV;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const ShaderType & x) {
        switch (x) {
            case ShaderType::GLSL: j = "glsl"; break;
            case ShaderType::HLSL: j = "hlsl"; break;
            case ShaderType::METAL: j = "metal"; break;
            case ShaderType::SPIRV: j = "spirv"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"ShaderType\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, PassType & x) {
        if (j == "compute") x = PassType::COMPUTE;
        else if (j == "data_transform") x = PassType::DATA_TRANSFORM;
        else if (j == "inference") x = PassType::INFERENCE;
        else if (j == "render") x = PassType::RENDER;
        else if (j == "retrain") x = PassType::RETRAIN;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const PassType & x) {
        switch (x) {
            case PassType::COMPUTE: j = "compute"; break;
            case PassType::DATA_TRANSFORM: j = "data_transform"; break;
            case PassType::INFERENCE: j = "inference"; break;
            case PassType::RENDER: j = "render"; break;
            case PassType::RETRAIN: j = "retrain"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"PassType\": " + std::to_string(static_cast<int>(x)));
        }
    }
}
