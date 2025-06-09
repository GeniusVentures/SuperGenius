//  To parse this JSON data, first install
//
//      Boost     http://www.boost.org
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     WelcomeProperties.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>
#include "helper.hpp"

#include "GnusSpecVersion.hpp"
#include "Author.hpp"
#include "Tags.hpp"
#include "Inputs.hpp"
#include "Metadata.hpp"

namespace sgns {
    using nlohmann::json;

    class WelcomeProperties {
        public:
        WelcomeProperties() = default;
        virtual ~WelcomeProperties() = default;

        private:
        GnusSpecVersion name;
        GnusSpecVersion version;
        GnusSpecVersion gnus_spec_version;
        Author author;
        Author description;
        Tags tags;
        Inputs inputs;
        Inputs outputs;
        Inputs parameters;
        Inputs passes;
        Metadata metadata;

        public:
        const GnusSpecVersion & get_name() const { return name; }
        GnusSpecVersion & get_mutable_name() { return name; }
        void set_name(const GnusSpecVersion & value) { this->name = value; }

        const GnusSpecVersion & get_version() const { return version; }
        GnusSpecVersion & get_mutable_version() { return version; }
        void set_version(const GnusSpecVersion & value) { this->version = value; }

        const GnusSpecVersion & get_gnus_spec_version() const { return gnus_spec_version; }
        GnusSpecVersion & get_mutable_gnus_spec_version() { return gnus_spec_version; }
        void set_gnus_spec_version(const GnusSpecVersion & value) { this->gnus_spec_version = value; }

        const Author & get_author() const { return author; }
        Author & get_mutable_author() { return author; }
        void set_author(const Author & value) { this->author = value; }

        const Author & get_description() const { return description; }
        Author & get_mutable_description() { return description; }
        void set_description(const Author & value) { this->description = value; }

        const Tags & get_tags() const { return tags; }
        Tags & get_mutable_tags() { return tags; }
        void set_tags(const Tags & value) { this->tags = value; }

        const Inputs & get_inputs() const { return inputs; }
        Inputs & get_mutable_inputs() { return inputs; }
        void set_inputs(const Inputs & value) { this->inputs = value; }

        const Inputs & get_outputs() const { return outputs; }
        Inputs & get_mutable_outputs() { return outputs; }
        void set_outputs(const Inputs & value) { this->outputs = value; }

        const Inputs & get_parameters() const { return parameters; }
        Inputs & get_mutable_parameters() { return parameters; }
        void set_parameters(const Inputs & value) { this->parameters = value; }

        const Inputs & get_passes() const { return passes; }
        Inputs & get_mutable_passes() { return passes; }
        void set_passes(const Inputs & value) { this->passes = value; }

        const Metadata & get_metadata() const { return metadata; }
        Metadata & get_mutable_metadata() { return metadata; }
        void set_metadata(const Metadata & value) { this->metadata = value; }
    };
}
