//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     ModelConfig.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "ModelNode.hpp"
#include "OptimizerConfig.hpp"

namespace sgns {
    enum class ModelFormat : int;
    enum class LossFunction : int;
}

namespace sgns {
    /**
     * Model configuration for inference/retrain passes
     */

    using nlohmann::json;

    /**
     * Model configuration for inference/retrain passes
     */
    class ModelConfig {
        public:
        ModelConfig() :
            batch_size_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, boost::none)
        {}
        virtual ~ModelConfig() = default;

        private:
        boost::optional<int64_t> batch_size;
        ClassMemberConstraints batch_size_constraint;
        boost::optional<ModelFormat> format;
        std::vector<ModelNode> input_nodes;
        boost::optional<LossFunction> loss_function;
        boost::optional<OptimizerConfig> optimizer;
        std::vector<ModelNode> output_nodes;
        std::string source_uri_param;

        public:
        boost::optional<int64_t> get_batch_size() const { return batch_size; }
        void set_batch_size(boost::optional<int64_t> value) { if (value) CheckConstraint("batch_size", batch_size_constraint, *value); this->batch_size = value; }

        /**
         * Model format
         */
        boost::optional<ModelFormat> get_format() const { return format; }
        void set_format(boost::optional<ModelFormat> value) { this->format = value; }

        const std::vector<ModelNode> & get_input_nodes() const { return input_nodes; }
        std::vector<ModelNode> & get_mutable_input_nodes() { return input_nodes; }
        void set_input_nodes(const std::vector<ModelNode> & value) { this->input_nodes = value; }

        /**
         * Loss function for training
         */
        boost::optional<LossFunction> get_loss_function() const { return loss_function; }
        void set_loss_function(boost::optional<LossFunction> value) { this->loss_function = value; }

        /**
         * Optimizer configuration for retrain passes
         */
        boost::optional<OptimizerConfig> get_optimizer() const { return optimizer; }
        void set_optimizer(boost::optional<OptimizerConfig> value) { this->optimizer = value; }

        const std::vector<ModelNode> & get_output_nodes() const { return output_nodes; }
        std::vector<ModelNode> & get_mutable_output_nodes() { return output_nodes; }
        void set_output_nodes(const std::vector<ModelNode> & value) { this->output_nodes = value; }

        /**
         * Parameter name that contains the model URI
         */
        const std::string & get_source_uri_param() const { return source_uri_param; }
        std::string & get_mutable_source_uri_param() { return source_uri_param; }
        void set_source_uri_param(const std::string & value) { this->source_uri_param = value; }
    };
}
