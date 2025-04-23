/**
 * @file       crdt_data_filter.hpp
 * @brief      Class to handle filtering of CRDT incoming data
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
    using ElementFilterCallback = std::function<std::optional<std::vector<pb::Element>>(const pb::Element&)>;

        static bool RegisterElementFilter( const std::string &pattern, ElementFilterCallback filter )
        {
            GetElementFilterRegistry()[pattern] = std::move( filter );
            return true;
        }

        static bool RegisterTombstoneFilter( const std::string &pattern, ElementFilterCallback filter )
        {
            GetTombstoneFilterRegistry()[pattern] = std::move( filter );
            return true;
        }

        static void UnregisterElementFilter( const std::string &pattern )
        {
            GetElementFilterRegistry().erase( pattern );
        }

        static void UnregisterTombstoneFilter( const std::string &pattern )
        {
            GetTombstoneFilterRegistry().erase( pattern );
        }

        static void FilterElementsOnDelta( std::shared_ptr<pb::Delta> &delta )
        {
            std::vector<pb::Element> new_tombstones;

            for ( int i = 0; i < delta->elements_size(); ++i )
            {
                const auto &element = delta->elements( i );

                for ( const auto &[pattern, filter] : GetElementFilterRegistry() )
                {
                    std::regex regex( pattern );
                    if ( std::regex_match( element.key(), regex ) )
                    {
                        auto tombstones = filter( element );
                        if ( tombstones )
                        {
                            new_tombstones.insert(
                                new_tombstones.end(),
                                tombstones->begin(),
                                tombstones->end()
                            );
                        }
                    }
                }
        
            }

            for (const auto &tombstone : new_tombstones)
            {
                auto *new_tombstone = delta->add_tombstones();
                *new_tombstone = tombstone;
            }
        }

        static void FilterTombstonesOnDelta( std::shared_ptr<pb::Delta> &delta )
        {
            //TODO - Figure out how to remove tombstones even recorded ones
        }

    private:
        CRDTDataFilter()  = default;
        ~CRDTDataFilter() = default;

        static std::unordered_map<std::string, ElementFilterCallback> &GetElementFilterRegistry()
        {
            static std::unordered_map<std::string, ElementFilterCallback> element_registry;
            return element_registry;
        }

        static std::unordered_map<std::string, ElementFilterCallback> &GetTombstoneFilterRegistry()
        {
            static std::unordered_map<std::string, ElementFilterCallback> tombstone_registry;
            return tombstone_registry;
        }
    };

}

#endif
