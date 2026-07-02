#ifndef SGNS_SGNS_VERSION_HPP
#define SGNS_SGNS_VERSION_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace sgns
{
    namespace version
    {
        static constexpr std::uint16_t    MAIN_NET_ID           = 369;
        static constexpr std::uint16_t    TEST_NET_ID           = 963;
        static constexpr std::uint16_t    DEV_NET_ID            = 144;
        static constexpr std::string_view NET_ID_APPENDIX       = ".%hu";
        static constexpr std::string_view SGNS_VERSION_APPENDIX = ".%hu.%hu";
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
         * @brief Retrieves the processing version used by processing task storage keys.
         *
         * @return uint32_t processing version.
         */
        uint32_t ProcessingVersion();

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

        uint16_t GetNetworkID();

        std::string GetNetAndVersionAppendix();
        std::string GetNetAndVersionAppendix( uint32_t version_major, uint32_t version_minor, uint16_t net_id );

        void SetNetworkId( uint16_t net_id );
    }
}

#endif // SGNS_SGNS_VERSION_HPP
