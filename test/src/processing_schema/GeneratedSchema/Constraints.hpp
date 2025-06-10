//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Constraints.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    using nlohmann::json;

    class Constraints {
        public:
        Constraints() = default;
        virtual ~Constraints() = default;

        private:
        boost::optional<std::vector<nlohmann::json>> constraints_enum;
        boost::optional<double> max;
        boost::optional<double> min;
        boost::optional<std::string> pattern;

        public:
        boost::optional<std::vector<nlohmann::json>> get_constraints_enum() const { return constraints_enum; }
        void set_constraints_enum(boost::optional<std::vector<nlohmann::json>> value) { this->constraints_enum = value; }

        boost::optional<double> get_max() const { return max; }
        void set_max(boost::optional<double> value) { this->max = value; }

        boost::optional<double> get_min() const { return min; }
        void set_min(boost::optional<double> value) { this->min = value; }

        boost::optional<std::string> get_pattern() const { return pattern; }
        void set_pattern(boost::optional<std::string> value) { this->pattern = value; }
    };
}
