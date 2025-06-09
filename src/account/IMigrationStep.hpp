/**
 * @file       IMigrationStep.hpp
 * @brief      Versioned migration manager and migration step interface.
 * @date       2025-05-29
 * @author     Luiz Guilherme Rizzatto Zucchi
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#pragma once

#include <string>
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
         * @brief Execute the migration logic.
         * @return Outcome of the operation.
         */
        virtual outcome::result<void> Apply() = 0;

        /**
         * @brief   Check if migration is required.
         * @return  outcome::result<bool>  true if migration should run; false to skip. On error, returns failure.
         */
        virtual outcome::result<bool> IsRequired() const = 0;
    };
} // namespace sgns
