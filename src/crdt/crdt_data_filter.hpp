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
#include "crdt/proto/delta.pb.h"

namespace sgns::crdt
{
    class CRDTDataFilter
    {
    public:
        using ElementFilterCallback = std::function<std::optional<std::vector<pb::Element>>( const pb::Element & )>;

        explicit CRDTDataFilter( bool accept_by_default = true );
        ~CRDTDataFilter() = default;

        bool RegisterElementFilter( const std::string &pattern, ElementFilterCallback filter );

        bool RegisterTombstoneFilter( const std::string &pattern, ElementFilterCallback filter );

        void UnregisterElementFilter( const std::string &pattern );

        void UnregisterTombstoneFilter( const std::string &pattern );

        void FilterElementsOnDelta( std::shared_ptr<pb::Delta> &delta );

        void FilterTombstonesOnDelta( std::shared_ptr<pb::Delta> &delta );

    private:
        const bool                                             accept_by_default_;
        std::mutex                                             element_registry_mutex_;
        std::mutex                                             tombstone_registry_mutex_;
        std::unordered_map<std::string, ElementFilterCallback> element_registry_;
        std::unordered_map<std::string, ElementFilterCallback> tombstone_registry_;
    };

}

#endif
