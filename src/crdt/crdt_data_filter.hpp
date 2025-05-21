/**
 * @file       crdt_data_filter.hpp
 * @brief      Header file of the CRDT Filter class
 * @date       2025-04-07
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _CRDT_DATA_FILTER_HPP_
#define _CRDT_DATA_FILTER_HPP_
#include <unordered_map>
#include <functional>
#include <string>
#include <memory>
#include <regex>
#include <shared_mutex>
#include "crdt/proto/delta.pb.h"

namespace sgns::crdt
{
    class CRDTDataFilter
    {
    public:
        /**
         * @brief      Element filtering callback definition
         */
        using ElementFilterCallback  = std::function<std::optional<std::vector<pb::Element>>( const pb::Element & )>;
        using FilterCallbackRegistry = std::unordered_map<std::string, ElementFilterCallback>;
        /**
         * @brief       Construct a new CRDTDataFilter object
         * @param[in]   accept_by_default: if true, every delta that doesn't have a filter gets accepted.
         *              if false, rejects by default.
         */
        explicit CRDTDataFilter( bool accept_by_default = true );

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
         * @brief       Removes the registration of an tombstone filter that corresponds to a pattern
         * @param[in]   pattern The regex/pattern that the key of the tombstone has to match
         */
        void UnregisterTombstoneFilter( const std::string &pattern );

        /**
         * @brief       Tries to filter the elements on delta according to stored filters
         * @param[in]   delta The delta to be filtered
         */
        void FilterElementsOnDelta( std::shared_ptr<pb::Delta> &delta );

        /**
         * @brief       Tries to filter the tombstones on delta according to stored filters
         * @param[in]   delta The delta to be filtered
         */
        void FilterTombstonesOnDelta( std::shared_ptr<pb::Delta> &delta );

    private:
        const bool             accept_by_default_;        ///< The default behavior for values not matching any filter
        std::shared_mutex      element_registry_mutex_;   ///< Mutex for the element registry
        std::shared_mutex      tombstone_registry_mutex_; ///< Mutex for the tombstone registry
        FilterCallbackRegistry element_registry_;         ///< Element filter callback registry
        FilterCallbackRegistry tombstone_registry_;       ///< Tombstone filter callback registry
    };

}

#endif
