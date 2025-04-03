#pragma once

#include <cstdint>
#include <string>

namespace sgns
{
    namespace version
    {
        /**
         * @brief Retrieves the complete version number of SuperGenius.
         *
         * @return uint64_t representing the version number.
         */
        uint64_t SuperGeniusVersionNum();

        /**
         * @brief Retrieves the major version of SuperGenius.
         *
         * @return uint32_t representing the major version.
         */
        uint32_t SuperGeniusVersionMajor();

        /**
         * @brief Retrieves the minor version of SuperGenius.
         *
         * @return uint32_t representing the minor version.
         */
        uint32_t SuperGeniusVersionMinor();

        /**
         * @brief Retrieves the patch version of SuperGenius.
         *
         * @return uint32_t representing the patch version.
         */
        uint32_t SuperGeniusVersionPatch();

        /**
         * @brief Retrieves the short version string of SuperGenius.
         *
         * @return std::string representing the short version.
         */
        std::string SuperGeniusVersionString();

        /**
         * @brief Retrieves the full version string of SuperGenius.
         *
         * @return std::string representing the full version.
         */
        std::string SuperGeniusVersionFullString();

        /**
         * @brief Retrieves the display version text of SuperGenius.
         *
         * @return std::string representing the version text.
         */
        std::string SuperGeniusVersionText();
    }
}
