//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     Params.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

namespace sgns {
    /**
     * Transform-specific parameters
     */

    using nlohmann::json;

    /**
     * Transform-specific parameters
     */
    class Params {
        public:
        Params() = default;
        virtual ~Params() = default;

        private:
        boost::optional<double> angle;
        boost::optional<std::vector<int64_t>> axes;
        boost::optional<std::string> color_space;
        boost::optional<std::string> custom_function;
        boost::optional<int64_t> height;
        boost::optional<std::vector<double>> mean;
        boost::optional<std::string> method;
        boost::optional<std::vector<double>> std;
        boost::optional<int64_t> width;

        public:
        boost::optional<double> get_angle() const { return angle; }
        void set_angle(boost::optional<double> value) { this->angle = value; }

        boost::optional<std::vector<int64_t>> get_axes() const { return axes; }
        void set_axes(boost::optional<std::vector<int64_t>> value) { this->axes = value; }

        boost::optional<std::string> get_color_space() const { return color_space; }
        void set_color_space(boost::optional<std::string> value) { this->color_space = value; }

        boost::optional<std::string> get_custom_function() const { return custom_function; }
        void set_custom_function(boost::optional<std::string> value) { this->custom_function = value; }

        boost::optional<int64_t> get_height() const { return height; }
        void set_height(boost::optional<int64_t> value) { this->height = value; }

        boost::optional<std::vector<double>> get_mean() const { return mean; }
        void set_mean(boost::optional<std::vector<double>> value) { this->mean = value; }

        boost::optional<std::string> get_method() const { return method; }
        void set_method(boost::optional<std::string> value) { this->method = value; }

        boost::optional<std::vector<double>> get_std() const { return std; }
        void set_std(boost::optional<std::vector<double>> value) { this->std = value; }

        boost::optional<int64_t> get_width() const { return width; }
        void set_width(boost::optional<int64_t> value) { this->width = value; }
    };
}
