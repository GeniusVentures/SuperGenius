//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     IoDeclaration.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "Dimensions.hpp"

namespace sgns {
    enum class InputFormat : int;
    enum class DataType : int;
}

namespace sgns {
    using nlohmann::json;

    class IoDeclaration {
        public:
        IoDeclaration() :
            name_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, std::string("^[a-zA-Z][a-zA-Z0-9_]*$"))
        {}
        virtual ~IoDeclaration() = default;

        private:
        boost::optional<std::string> description;
        boost::optional<Dimensions> dimensions;
        boost::optional<InputFormat> format;
        std::string name;
        ClassMemberConstraints name_constraint;
        DataType type;

        public:
        /**
         * Human-readable description
         */
        boost::optional<std::string> get_description() const { return description; }
        void set_description(boost::optional<std::string> value) { this->description = value; }

        /**
         * Optional dimensions specification
         */
        boost::optional<Dimensions> get_dimensions() const { return dimensions; }
        void set_dimensions(boost::optional<Dimensions> value) { this->dimensions = value; }

        /**
         * Data format (e.g., RGBA8, FLOAT32)
         */
        boost::optional<InputFormat> get_format() const { return format; }
        void set_format(boost::optional<InputFormat> value) { this->format = value; }

        /**
         * Unique identifier for this input/output
         */
        const std::string & get_name() const { return name; }
        std::string & get_mutable_name() { return name; }
        void set_name(const std::string & value) { CheckConstraint("name", name_constraint, value); this->name = value; }

        const DataType & get_type() const { return type; }
        DataType & get_mutable_type() { return type; }
        void set_type(const DataType & value) { this->type = value; }
    };
}
