//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     OptimizerConfigProperties.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "DataTypeClass.hpp"
#include "BatchSize.hpp"
#include "Beta1.hpp"
#include "Epsilon.hpp"

namespace sgns {
    using nlohmann::json;

    class OptimizerConfigProperties {
        public:
        OptimizerConfigProperties() = default;
        virtual ~OptimizerConfigProperties() = default;

        private:
        DataTypeClass type;
        BatchSize learning_rate;
        Beta1 momentum;
        Epsilon weight_decay;
        Beta1 beta1;
        Beta1 beta2;
        Epsilon epsilon;

        public:
        const DataTypeClass & get_type() const { return type; }
        DataTypeClass & get_mutable_type() { return type; }
        void set_type(const DataTypeClass & value) { this->type = value; }

        const BatchSize & get_learning_rate() const { return learning_rate; }
        BatchSize & get_mutable_learning_rate() { return learning_rate; }
        void set_learning_rate(const BatchSize & value) { this->learning_rate = value; }

        const Beta1 & get_momentum() const { return momentum; }
        Beta1 & get_mutable_momentum() { return momentum; }
        void set_momentum(const Beta1 & value) { this->momentum = value; }

        const Epsilon & get_weight_decay() const { return weight_decay; }
        Epsilon & get_mutable_weight_decay() { return weight_decay; }
        void set_weight_decay(const Epsilon & value) { this->weight_decay = value; }

        const Beta1 & get_beta1() const { return beta1; }
        Beta1 & get_mutable_beta1() { return beta1; }
        void set_beta1(const Beta1 & value) { this->beta1 = value; }

        const Beta1 & get_beta2() const { return beta2; }
        Beta1 & get_mutable_beta2() { return beta2; }
        void set_beta2(const Beta1 & value) { this->beta2 = value; }

        const Epsilon & get_epsilon() const { return epsilon; }
        Epsilon & get_mutable_epsilon() { return epsilon; }
        void set_epsilon(const Epsilon & value) { this->epsilon = value; }
    };
}
