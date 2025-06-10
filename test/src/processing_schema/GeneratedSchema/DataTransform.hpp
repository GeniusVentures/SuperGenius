//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     DataTransform.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "Params.hpp"

namespace sgns {
    enum class DataTransformType : int;
}

namespace sgns {
    using nlohmann::json;

    class DataTransform {
        public:
        DataTransform() = default;
        virtual ~DataTransform() = default;

        private:
        std::string input;
        std::string output;
        boost::optional<Params> params;
        DataTransformType type;

        public:
        /**
         * Input data reference
         */
        const std::string & get_input() const { return input; }
        std::string & get_mutable_input() { return input; }
        void set_input(const std::string & value) { this->input = value; }

        /**
         * Output data reference
         */
        const std::string & get_output() const { return output; }
        std::string & get_mutable_output() { return output; }
        void set_output(const std::string & value) { this->output = value; }

        /**
         * Transform-specific parameters
         */
        boost::optional<Params> get_params() const { return params; }
        void set_params(boost::optional<Params> value) { this->params = value; }

        const DataTransformType & get_type() const { return type; }
        DataTransformType & get_mutable_type() { return type; }
        void set_type(const DataTransformType & value) { this->type = value; }
    };
}
