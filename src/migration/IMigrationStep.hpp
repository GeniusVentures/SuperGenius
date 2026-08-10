/**
 * @file       IMigrationStep.hpp
 * @brief      Versioned migration manager and migration step interface.
 * @date       2025-05-29
 * @author     Luiz Guilherme Rizzatto Zucchi
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef SGNS_IMIGRATION_STEP_HPP
#define SGNS_IMIGRATION_STEP_HPP

#include <string>
#include <tuple>
#include <sstream>
#include "outcome/outcome.hpp"

namespace sgns
{
    /**
     * @brief Interface for a migration step between two schema versions.
     */
    class IMigrationStep
    {
    public:
        virtual ~IMigrationStep() = default;

        /**
         * @brief Get the version from which the migration starts.
         * @return The source version string.
         */
        virtual std::string FromVersion() const = 0;

        /**
         * @brief Get the version to which the migration applies.
         * @return The target version string.
         */
        virtual std::string ToVersion() const = 0;

        /**
         * @brief       Initializes internal variables after constructor
         * @return      Outcome of the operation
         */
        virtual outcome::result<void> Init() = 0;
        /**
         * @brief Execute the migration logic.
         * @return Outcome of the operation.
         */
        virtual outcome::result<void> Apply() = 0;

        /**
         * @brief       Shuts down internal variables
         * @return      Outcome of the operation
         */
        virtual outcome::result<void> ShutDown() = 0;

        /**
         * @brief   Check if migration is required.
         * @return  outcome::result<bool>  true if migration should run; false to skip. On error, returns failure.
         */
        virtual outcome::result<bool> IsRequired() const = 0;

        std::tuple<int, int, int> ParseVersion( const std::string &version ) const
        {
            int                major = 0, minor = 0, patch = 0;
            char               dot;
            std::istringstream iss( version );
            iss >> major >> dot >> minor >> dot >> patch;
            return { major, minor, patch };
        }

        bool IsVersionLessThan( const std::string &lhs, const std::string &rhs ) const
        {
            auto [lhs_major, lhs_minor, lhs_patch] = ParseVersion( lhs );
            auto [rhs_major, rhs_minor, rhs_patch] = ParseVersion( rhs );
            if ( lhs_major != rhs_major )
            {
                return lhs_major < rhs_major;
            }
            if ( lhs_minor != rhs_minor )
            {
                return lhs_minor < rhs_minor;
            }
            return lhs_patch < rhs_patch;
        }
    };
} // namespace sgns

#endif // SGNS_IMIGRATION_STEP_HPP
