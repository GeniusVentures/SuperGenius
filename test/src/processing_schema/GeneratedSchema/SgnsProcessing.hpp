//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     SgnsProcessing.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "IoDeclaration.hpp"
#include "Parameter.hpp"
#include "Pass.hpp"

namespace sgns {
    /**
     * Schema for defining AI inference and retraining workflows with shader passes
     */

    using nlohmann::json;

    /**
     * Schema for defining AI inference and retraining workflows with shader passes
     */
    class SgnsProcessing {
        public:
        SgnsProcessing() :
            gnus_spec_version_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, std::string("^\\d+\\.\\d+$")),
            name_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, std::string("^[A-Za-z0-9_-]+$")),
            version_constraint(boost::none, boost::none, boost::none, boost::none, boost::none, boost::none, std::string("^\\d+\\.\\d+(\\.\\d+)?$"))
        {}
        virtual ~SgnsProcessing() = default;

        private:
        boost::optional<std::string> author;
        boost::optional<std::string> description;
        std::string gnus_spec_version;
        ClassMemberConstraints gnus_spec_version_constraint;
        std::vector<IoDeclaration> inputs;
        boost::optional<std::map<std::string, nlohmann::json>> metadata;
        std::string name;
        ClassMemberConstraints name_constraint;
        std::vector<IoDeclaration> outputs;
        boost::optional<std::vector<Parameter>> parameters;
        std::vector<Pass> passes;
        boost::optional<std::vector<std::string>> tags;
        std::string version;
        ClassMemberConstraints version_constraint;

        public:
        /**
         * Author of this processing definition
         */
        boost::optional<std::string> get_author() const { return author; }
        void set_author(boost::optional<std::string> value) { this->author = value; }

        /**
         * Human-readable description of what this processing definition does
         */
        boost::optional<std::string> get_description() const { return description; }
        void set_description(boost::optional<std::string> value) { this->description = value; }

        /**
         * Version of the GNUS processing definition specification
         */
        const std::string & get_gnus_spec_version() const { return gnus_spec_version; }
        std::string & get_mutable_gnus_spec_version() { return gnus_spec_version; }
        void set_gnus_spec_version(const std::string & value) { CheckConstraint("gnus_spec_version", gnus_spec_version_constraint, value); this->gnus_spec_version = value; }

        /**
         * Declares the external inputs this process requires
         */
        const std::vector<IoDeclaration> & get_inputs() const { return inputs; }
        std::vector<IoDeclaration> & get_mutable_inputs() { return inputs; }
        void set_inputs(const std::vector<IoDeclaration> & value) { this->inputs = value; }

        /**
         * Additional metadata for the processing definition
         */
        boost::optional<std::map<std::string, nlohmann::json>> get_metadata() const { return metadata; }
        void set_metadata(boost::optional<std::map<std::string, nlohmann::json>> value) { this->metadata = value; }

        /**
         * Unique name for this processing definition
         */
        const std::string & get_name() const { return name; }
        std::string & get_mutable_name() { return name; }
        void set_name(const std::string & value) { CheckConstraint("name", name_constraint, value); this->name = value; }

        /**
         * Declares the final outputs this process will produce
         */
        const std::vector<IoDeclaration> & get_outputs() const { return outputs; }
        std::vector<IoDeclaration> & get_mutable_outputs() { return outputs; }
        void set_outputs(const std::vector<IoDeclaration> & value) { this->outputs = value; }

        /**
         * Overridable parameters with defaults
         */
        boost::optional<std::vector<Parameter>> get_parameters() const { return parameters; }
        void set_parameters(boost::optional<std::vector<Parameter>> value) { this->parameters = value; }

        /**
         * Array of processing passes to execute
         */
        const std::vector<Pass> & get_passes() const { return passes; }
        std::vector<Pass> & get_mutable_passes() { return passes; }
        void set_passes(const std::vector<Pass> & value) { this->passes = value; }

        /**
         * Tags for categorizing this definition
         */
        boost::optional<std::vector<std::string>> get_tags() const { return tags; }
        void set_tags(boost::optional<std::vector<std::string>> value) { this->tags = value; }

        /**
         * Version of this processing definition
         */
        const std::string & get_version() const { return version; }
        std::string & get_mutable_version() { return version; }
        void set_version(const std::string & value) { CheckConstraint("version", version_constraint, value); this->version = value; }
    };
}
