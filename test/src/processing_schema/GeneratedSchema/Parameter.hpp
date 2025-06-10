//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Parameter.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "Constraints.hpp"

namespace sgns {
    enum class ParameterType : int;
}

namespace sgns {
    using nlohmann::json;

    class Parameter {
        public:
        Parameter() :
            name_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, std::string("^[a-zA-Z][a-zA-Z0-9_]*$"))
        {}
        virtual ~Parameter() = default;

        private:
        boost::optional<Constraints> constraints;
        nlohmann::json parameter_default;
        boost::optional<std::string> description;
        std::string name;
        ClassMemberConstraints name_constraint;
        ParameterType type;

        public:
        boost::optional<Constraints> get_constraints() const { return constraints; }
        void set_constraints(boost::optional<Constraints> value) { this->constraints = value; }

        /**
         * Default value for this parameter
         */
        const nlohmann::json & get_parameter_default() const { return parameter_default; }
        nlohmann::json & get_mutable_parameter_default() { return parameter_default; }
        void set_parameter_default(const nlohmann::json & value) { this->parameter_default = value; }

        boost::optional<std::string> get_description() const { return description; }
        void set_description(boost::optional<std::string> value) { this->description = value; }

        /**
         * Parameter name
         */
        const std::string & get_name() const { return name; }
        std::string & get_mutable_name() { return name; }
        void set_name(const std::string & value) { CheckConstraint("name", name_constraint, value); this->name = value; }

        const ParameterType & get_type() const { return type; }
        ParameterType & get_mutable_type() { return type; }
        void set_type(const ParameterType & value) { this->type = value; }
    };
}
