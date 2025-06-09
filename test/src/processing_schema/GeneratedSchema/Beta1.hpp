//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Beta1.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    enum class TypeEnum : int;
}

namespace sgns {
    using nlohmann::json;

    class Beta1 {
        public:
        Beta1() = default;
        virtual ~Beta1() = default;

        private:
        TypeEnum type;
        int64_t minimum;
        int64_t maximum;

        public:
        const TypeEnum & get_type() const { return type; }
        TypeEnum & get_mutable_type() { return type; }
        void set_type(const TypeEnum & value) { this->type = value; }

        const int64_t & get_minimum() const { return minimum; }
        int64_t & get_mutable_minimum() { return minimum; }
        void set_minimum(const int64_t & value) { this->minimum = value; }

        const int64_t & get_maximum() const { return maximum; }
        int64_t & get_mutable_maximum() { return maximum; }
        void set_maximum(const int64_t & value) { this->maximum = value; }
    };
}
