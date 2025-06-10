//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     ShaderConfig.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "Uniform.hpp"

namespace sgns {
    enum class ShaderType : int;
}

namespace sgns {
    /**
     * Shader configuration for compute/render passes
     */

    using nlohmann::json;

    /**
     * Shader configuration for compute/render passes
     */
    class ShaderConfig {
        public:
        ShaderConfig() = default;
        virtual ~ShaderConfig() = default;

        private:
        boost::optional<std::string> entry_point;
        std::string source;
        boost::optional<ShaderType> type;
        boost::optional<std::map<std::string, Uniform>> uniforms;

        public:
        boost::optional<std::string> get_entry_point() const { return entry_point; }
        void set_entry_point(boost::optional<std::string> value) { this->entry_point = value; }

        /**
         * Shader source path or URI parameter
         */
        const std::string & get_source() const { return source; }
        std::string & get_mutable_source() { return source; }
        void set_source(const std::string & value) { this->source = value; }

        boost::optional<ShaderType> get_type() const { return type; }
        void set_type(boost::optional<ShaderType> value) { this->type = value; }

        /**
         * Uniform variable declarations
         */
        boost::optional<std::map<std::string, Uniform>> get_uniforms() const { return uniforms; }
        void set_uniforms(boost::optional<std::map<std::string, Uniform>> value) { this->uniforms = value; }
    };
}
