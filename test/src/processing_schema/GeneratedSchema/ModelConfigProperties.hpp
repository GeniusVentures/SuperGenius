//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     ModelConfigProperties.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "Author.hpp"
#include "Format.hpp"
#include "Inputs.hpp"
#include "Optimizer.hpp"
#include "BatchSize.hpp"

namespace sgns {
    using nlohmann::json;

    class ModelConfigProperties {
        public:
        ModelConfigProperties() = default;
        virtual ~ModelConfigProperties() = default;

        private:
        Author source_uri_param;
        Format format;
        Inputs input_nodes;
        Inputs output_nodes;
        Optimizer optimizer;
        Format loss_function;
        BatchSize batch_size;

        public:
        const Author & get_source_uri_param() const { return source_uri_param; }
        Author & get_mutable_source_uri_param() { return source_uri_param; }
        void set_source_uri_param(const Author & value) { this->source_uri_param = value; }

        const Format & get_format() const { return format; }
        Format & get_mutable_format() { return format; }
        void set_format(const Format & value) { this->format = value; }

        const Inputs & get_input_nodes() const { return input_nodes; }
        Inputs & get_mutable_input_nodes() { return input_nodes; }
        void set_input_nodes(const Inputs & value) { this->input_nodes = value; }

        const Inputs & get_output_nodes() const { return output_nodes; }
        Inputs & get_mutable_output_nodes() { return output_nodes; }
        void set_output_nodes(const Inputs & value) { this->output_nodes = value; }

        const Optimizer & get_optimizer() const { return optimizer; }
        Optimizer & get_mutable_optimizer() { return optimizer; }
        void set_optimizer(const Optimizer & value) { this->optimizer = value; }

        const Format & get_loss_function() const { return loss_function; }
        Format & get_mutable_loss_function() { return loss_function; }
        void set_loss_function(const Format & value) { this->loss_function = value; }

        const BatchSize & get_batch_size() const { return batch_size; }
        BatchSize & get_mutable_batch_size() { return batch_size; }
        void set_batch_size(const BatchSize & value) { this->batch_size = value; }
    };
}
