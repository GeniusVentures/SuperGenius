#pragma once

#include <cstdint>
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
         * @brief Retrieves the pre-release identifier of SuperGenius.
         *
         * @return const char* pointing to the pre-release string.
         */
        const char *SuperGeniusVersionPreRelease();

        /**
         * @brief Retrieves the build metadata of SuperGenius.
         *
         * @return const char* pointing to the build metadata string.
         */
        const char *SuperGeniusVersionBuildMetadata();

        
        const char *SuperGeniusVersionText();
    }
}
