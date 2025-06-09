//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     AllOf.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "If.hpp"
#include "Then.hpp"

namespace sgns {
    using nlohmann::json;

    class AllOf {
        public:
        AllOf() = default;
        virtual ~AllOf() = default;

        private:
        If all_of_if;
        Then then;

        public:
        const If & get_all_of_if() const { return all_of_if; }
        If & get_mutable_all_of_if() { return all_of_if; }
        void set_all_of_if(const If & value) { this->all_of_if = value; }

        const Then & get_then() const { return then; }
        Then & get_mutable_then() { return then; }
        void set_then(const Then & value) { this->then = value; }
    };
}
