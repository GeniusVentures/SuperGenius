//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Epsilon.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    enum class TypeEnum : int;
}

namespace sgns {
    using nlohmann::json;

    class Epsilon {
        public:
        Epsilon() = default;
        virtual ~Epsilon() = default;

        private:
        TypeEnum type;
        int64_t minimum;

        public:
        const TypeEnum & get_type() const { return type; }
        TypeEnum & get_mutable_type() { return type; }
        void set_type(const TypeEnum & value) { this->type = value; }

        const int64_t & get_minimum() const { return minimum; }
        int64_t & get_mutable_minimum() { return minimum; }
        void set_minimum(const int64_t & value) { this->minimum = value; }
    };
}
