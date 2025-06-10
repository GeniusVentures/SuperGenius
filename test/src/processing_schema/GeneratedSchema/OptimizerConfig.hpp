//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     OptimizerConfig.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    enum class OptimizerType : int;
}

namespace sgns {
    /**
     * Optimizer configuration for retrain passes
     */

    using nlohmann::json;

    /**
     * Optimizer configuration for retrain passes
     */
    class OptimizerConfig {
        public:
        OptimizerConfig() :
            beta1_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, boost::none),
            beta2_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, boost::none),
            epsilon_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, boost::none),
            learning_rate_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, boost::none),
            momentum_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, boost::none),
            weight_decay_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, boost::none)
        {}
        virtual ~OptimizerConfig() = default;

        private:
        boost::optional<double> beta1;
        ClassMemberConstraints beta1_constraint;
        boost::optional<double> beta2;
        ClassMemberConstraints beta2_constraint;
        boost::optional<double> epsilon;
        ClassMemberConstraints epsilon_constraint;
        boost::optional<double> learning_rate;
        ClassMemberConstraints learning_rate_constraint;
        boost::optional<double> momentum;
        ClassMemberConstraints momentum_constraint;
        OptimizerType type;
        boost::optional<double> weight_decay;
        ClassMemberConstraints weight_decay_constraint;

        public:
        boost::optional<double> get_beta1() const { return beta1; }
        void set_beta1(boost::optional<double> value) { if (value) CheckConstraint("beta1", beta1_constraint, *value); this->beta1 = value; }

        boost::optional<double> get_beta2() const { return beta2; }
        void set_beta2(boost::optional<double> value) { if (value) CheckConstraint("beta2", beta2_constraint, *value); this->beta2 = value; }

        boost::optional<double> get_epsilon() const { return epsilon; }
        void set_epsilon(boost::optional<double> value) { if (value) CheckConstraint("epsilon", epsilon_constraint, *value); this->epsilon = value; }

        boost::optional<double> get_learning_rate() const { return learning_rate; }
        void set_learning_rate(boost::optional<double> value) { if (value) CheckConstraint("learning_rate", learning_rate_constraint, *value); this->learning_rate = value; }

        boost::optional<double> get_momentum() const { return momentum; }
        void set_momentum(boost::optional<double> value) { if (value) CheckConstraint("momentum", momentum_constraint, *value); this->momentum = value; }

        const OptimizerType & get_type() const { return type; }
        OptimizerType & get_mutable_type() { return type; }
        void set_type(const OptimizerType & value) { this->type = value; }

        boost::optional<double> get_weight_decay() const { return weight_decay; }
        void set_weight_decay(boost::optional<double> value) { if (value) CheckConstraint("weight_decay", weight_decay_constraint, *value); this->weight_decay = value; }
    };
}
