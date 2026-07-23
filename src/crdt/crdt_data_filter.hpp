/**
 * @file       crdt_data_filter.hpp
 * @brief      Header file of the CRDT Filter class
 * @date       2025-04-07
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _CRDT_DATA_FILTER_HPP_
#define _CRDT_DATA_FILTER_HPP_

#include <functional>
#include <string>
#include <memory>
#include <regex>
#include <shared_mutex>
#include <vector>

#include "crdt/proto/delta.pb.h"

namespace sgns::crdt
{
    class CRDTWorkJournal;

    class CRDTDataFilter
    {
    public:
        /**
         * @brief      Element filtering callback definition
         */
        using ElementFilterCallback = std::function<std::optional<std::vector<pb::Element>>( const pb::Element & )>;
        using DeltaFilterCallback   = std::function<bool( const pb::Delta & )>;

        struct FilterCallbackEntry
        {
            std::string           pattern;
            std::regex            regex;
            ElementFilterCallback filter;
        };

        using FilterCallbackRegistry = std::vector<std::shared_ptr<const FilterCallbackEntry>>;

        struct DeltaFilterCallbackEntry
        {
            std::string        pattern;
            std::regex         regex;
            DeltaFilterCallback filter;
        };

        using DeltaFilterCallbackRegistry = std::vector<std::shared_ptr<const DeltaFilterCallbackEntry>>;

        /**
         * @brief       Construct a new CRDTDataFilter object
         * @param[in]   accept_by_default: if true, every delta that doesn't have a filter gets accepted.
         *              if false, rejects by default.
         */
        explicit CRDTDataFilter( std::shared_ptr<CRDTWorkJournal> work_journal, bool accept_by_default = true );

        /**
         * @brief       Destroy the CRDTDataFilter object
         */
        ~CRDTDataFilter() = default;

        /**
         * @brief       Registers an element filter callback
         * @param[in]   pattern The regex/pattern that the key of the element has to match
         * @param[in]   filter The callback that is executed in case the pattern matches
         * @return      true if succeeded, false otherwise
         */
        bool RegisterElementFilter( const std::string &pattern, ElementFilterCallback filter );

        /**
         * @brief Registers a filter that validates a complete incoming delta.
         *
         * The callback runs before element filters whenever at least one element
         * matches pattern. Returning false removes every matching element from
         * the delta, allowing a multi-key namespace to fail closed atomically
         * while preserving unrelated elements.
         */
        bool RegisterDeltaFilter( const std::string &pattern, DeltaFilterCallback filter );

        /**
         * @brief       Registers a tombstone filter callback
         * @param[in]   pattern The regex/pattern that the key of the tombstone has to match
         * @param[in]   filter The callback that is executed in case the pattern matches
         * @return      true if succeeded, false otherwise
         */
        bool RegisterTombstoneFilter( const std::string &pattern, ElementFilterCallback filter );

        /**
         * @brief       Removes the registration of an element filter that corresponds to a pattern
         * @param[in]   pattern The regex/pattern that the key of the element has to match
         */
        void UnregisterElementFilter( const std::string &pattern );

        /**
         * @brief Removes the delta filter registered for pattern.
         */
        void UnregisterDeltaFilter( const std::string &pattern );

        /**
         * @brief       Removes the registration of a tombstone filter that corresponds to a pattern
         * @param[in]   pattern The regex/pattern that the key of the tombstone has to match
         */
        void UnregisterTombstoneFilter( const std::string &pattern );

        /**
         * @brief       Tries to filter the elements on delta according to stored filters
         * @param[in]   delta The delta to be filtered
         */
        void FilterElementsOnDelta( pb::Delta &delta ) const;

        /**
         * @brief       Tries to filter the tombstones on delta according to stored filters
         * @param[in]   delta The delta to be filtered
         */
        void FilterTombstonesOnDelta( pb::Delta &delta );

    private:
        std::shared_ptr<CRDTWorkJournal> work_journal_;
        const bool                accept_by_default_;      ///< The default behavior for values not matching any filter
        mutable std::shared_mutex element_registry_mutex_; ///< Mutex for the element registry
        mutable std::shared_mutex delta_registry_mutex_;   ///< Mutex for the delta registry
        std::shared_mutex         tombstone_registry_mutex_; ///< Mutex for the tombstone registry
        FilterCallbackRegistry    element_registry_;         ///< Element filter callback registry
        DeltaFilterCallbackRegistry delta_registry_;          ///< Delta-aware filter callback registry
        FilterCallbackRegistry    tombstone_registry_;       ///< Tombstone filter callback registry
    };

}

#endif
