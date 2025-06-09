//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     DataTransformProperties.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "DataTypeClass.hpp"
#include "Author.hpp"
#include "Params.hpp"

namespace sgns {
    using nlohmann::json;

    class DataTransformProperties {
        public:
        DataTransformProperties() = default;
        virtual ~DataTransformProperties() = default;

        private:
        DataTypeClass type;
        Author input;
        Author output;
        Params params;

        public:
        const DataTypeClass & get_type() const { return type; }
        DataTypeClass & get_mutable_type() { return type; }
        void set_type(const DataTypeClass & value) { this->type = value; }

        const Author & get_input() const { return input; }
        Author & get_mutable_input() { return input; }
        void set_input(const Author & value) { this->input = value; }

        const Author & get_output() const { return output; }
        Author & get_mutable_output() { return output; }
        void set_output(const Author & value) { this->output = value; }

        const Params & get_params() const { return params; }
        Params & get_mutable_params() { return params; }
        void set_params(const Params & value) { this->params = value; }
    };
}
