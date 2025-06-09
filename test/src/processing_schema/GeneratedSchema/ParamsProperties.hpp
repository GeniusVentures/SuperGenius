//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     ParamsProperties.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "Axes.hpp"
#include "DescriptionClass.hpp"

namespace sgns {
    using nlohmann::json;

    class ParamsProperties {
        public:
        ParamsProperties() = default;
        virtual ~ParamsProperties() = default;

        private:
        Axes mean;
        Axes std;
        DescriptionClass width;
        DescriptionClass height;
        DescriptionClass method;
        DescriptionClass color_space;
        Axes axes;
        DescriptionClass angle;
        DescriptionClass custom_function;

        public:
        const Axes & get_mean() const { return mean; }
        Axes & get_mutable_mean() { return mean; }
        void set_mean(const Axes & value) { this->mean = value; }

        const Axes & get_std() const { return std; }
        Axes & get_mutable_std() { return std; }
        void set_std(const Axes & value) { this->std = value; }

        const DescriptionClass & get_width() const { return width; }
        DescriptionClass & get_mutable_width() { return width; }
        void set_width(const DescriptionClass & value) { this->width = value; }

        const DescriptionClass & get_height() const { return height; }
        DescriptionClass & get_mutable_height() { return height; }
        void set_height(const DescriptionClass & value) { this->height = value; }

        const DescriptionClass & get_method() const { return method; }
        DescriptionClass & get_mutable_method() { return method; }
        void set_method(const DescriptionClass & value) { this->method = value; }

        const DescriptionClass & get_color_space() const { return color_space; }
        DescriptionClass & get_mutable_color_space() { return color_space; }
        void set_color_space(const DescriptionClass & value) { this->color_space = value; }

        const Axes & get_axes() const { return axes; }
        Axes & get_mutable_axes() { return axes; }
        void set_axes(const Axes & value) { this->axes = value; }

        const DescriptionClass & get_angle() const { return angle; }
        DescriptionClass & get_mutable_angle() { return angle; }
        void set_angle(const DescriptionClass & value) { this->angle = value; }

        const DescriptionClass & get_custom_function() const { return custom_function; }
        DescriptionClass & get_mutable_custom_function() { return custom_function; }
        void set_custom_function(const DescriptionClass & value) { this->custom_function = value; }
    };
}
