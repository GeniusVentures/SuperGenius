//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Dimensions.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    /**
     * Optional dimensions specification
     */

    using nlohmann::json;

    /**
     * Optional dimensions specification
     */
    class Dimensions {
        public:
        Dimensions() :
            batch_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, boost::none),
            channels_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, boost::none),
            height_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, boost::none),
            width_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, boost::none)
        {}
        virtual ~Dimensions() = default;

        private:
        boost::optional<int64_t> batch;
        ClassMemberConstraints batch_constraint;
        boost::optional<int64_t> channels;
        ClassMemberConstraints channels_constraint;
        boost::optional<int64_t> height;
        ClassMemberConstraints height_constraint;
        boost::optional<int64_t> width;
        ClassMemberConstraints width_constraint;

        public:
        boost::optional<int64_t> get_batch() const { return batch; }
        void set_batch(boost::optional<int64_t> value) { if (value) CheckConstraint("batch", batch_constraint, *value); this->batch = value; }

        boost::optional<int64_t> get_channels() const { return channels; }
        void set_channels(boost::optional<int64_t> value) { if (value) CheckConstraint("channels", channels_constraint, *value); this->channels = value; }

        boost::optional<int64_t> get_height() const { return height; }
        void set_height(boost::optional<int64_t> value) { if (value) CheckConstraint("height", height_constraint, *value); this->height = value; }

        boost::optional<int64_t> get_width() const { return width; }
        void set_width(boost::optional<int64_t> value) { if (value) CheckConstraint("width", width_constraint, *value); this->width = value; }
    };
}
