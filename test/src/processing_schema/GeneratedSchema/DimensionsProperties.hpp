//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     DimensionsProperties.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "Epsilon.hpp"

namespace sgns {
    using nlohmann::json;

    class DimensionsProperties {
        public:
        DimensionsProperties() = default;
        virtual ~DimensionsProperties() = default;

        private:
        Epsilon width;
        Epsilon height;
        Epsilon channels;
        Epsilon batch;

        public:
        const Epsilon & get_width() const { return width; }
        Epsilon & get_mutable_width() { return width; }
        void set_width(const Epsilon & value) { this->width = value; }

        const Epsilon & get_height() const { return height; }
        Epsilon & get_mutable_height() { return height; }
        void set_height(const Epsilon & value) { this->height = value; }

        const Epsilon & get_channels() const { return channels; }
        Epsilon & get_mutable_channels() { return channels; }
        void set_channels(const Epsilon & value) { this->channels = value; }

        const Epsilon & get_batch() const { return batch; }
        Epsilon & get_mutable_batch() { return batch; }
        void set_batch(const Epsilon & value) { this->batch = value; }
    };
}
