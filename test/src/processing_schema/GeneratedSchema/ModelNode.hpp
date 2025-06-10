//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     ModelNode.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    enum class DataType : int;
}

namespace sgns {
    using nlohmann::json;

    class ModelNode {
        public:
        ModelNode() :
            source_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, std::string("^(input|output|internal|parameter):[a-zA-Z][a-zA-Z0-9_]*$")),
            target_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, std::string("^(output|internal):[a-zA-Z][a-zA-Z0-9_]*$"))
        {}
        virtual ~ModelNode() = default;

        private:
        std::string name;
        boost::optional<std::vector<int64_t>> shape;
        boost::optional<std::string> source;
        ClassMemberConstraints source_constraint;
        boost::optional<std::string> target;
        ClassMemberConstraints target_constraint;
        DataType type;

        public:
        /**
         * Node name in the model graph
         */
        const std::string & get_name() const { return name; }
        std::string & get_mutable_name() { return name; }
        void set_name(const std::string & value) { this->name = value; }

        /**
         * Expected tensor shape
         */
        boost::optional<std::vector<int64_t>> get_shape() const { return shape; }
        void set_shape(boost::optional<std::vector<int64_t>> value) { this->shape = value; }

        /**
         * Data source using prefix notation (input:, output:, internal:, parameter:)
         */
        boost::optional<std::string> get_source() const { return source; }
        void set_source(boost::optional<std::string> value) { if (value) CheckConstraint("source", source_constraint, *value); this->source = value; }

        /**
         * Data target using prefix notation
         */
        boost::optional<std::string> get_target() const { return target; }
        void set_target(boost::optional<std::string> value) { if (value) CheckConstraint("target", target_constraint, *value); this->target = value; }

        const DataType & get_type() const { return type; }
        DataType & get_mutable_type() { return type; }
        void set_type(const DataType & value) { this->type = value; }
    };
}
