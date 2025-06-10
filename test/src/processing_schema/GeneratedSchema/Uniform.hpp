//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Uniform.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    enum class DataType : int;
}

namespace sgns {
    using nlohmann::json;

    class Uniform {
        public:
        Uniform() = default;
        virtual ~Uniform() = default;

        private:
        boost::optional<std::string> source;
        boost::optional<DataType> type;
        nlohmann::json value;

        public:
        boost::optional<std::string> get_source() const { return source; }
        void set_source(boost::optional<std::string> value) { this->source = value; }

        boost::optional<DataType> get_type() const { return type; }
        void set_type(boost::optional<DataType> value) { this->type = value; }

        const nlohmann::json & get_value() const { return value; }
        nlohmann::json & get_mutable_value() { return value; }
        void set_value(const nlohmann::json & value) { this->value = value; }
    };
}
