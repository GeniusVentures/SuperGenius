//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     BatchSize.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    enum class TypeEnum : int;
}

namespace sgns {
    using nlohmann::json;

    class BatchSize {
        public:
        BatchSize() = default;
        virtual ~BatchSize() = default;

        private:
        TypeEnum type;
        int64_t minimum;
        double batch_size_default;

        public:
        const TypeEnum & get_type() const { return type; }
        TypeEnum & get_mutable_type() { return type; }
        void set_type(const TypeEnum & value) { this->type = value; }

        const int64_t & get_minimum() const { return minimum; }
        int64_t & get_mutable_minimum() { return minimum; }
        void set_minimum(const int64_t & value) { this->minimum = value; }

        const double & get_batch_size_default() const { return batch_size_default; }
        double & get_mutable_batch_size_default() { return batch_size_default; }
        void set_batch_size_default(const double & value) { this->batch_size_default = value; }
    };
}
