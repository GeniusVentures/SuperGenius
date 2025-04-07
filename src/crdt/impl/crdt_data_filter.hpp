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
        using ElementFilterCallback = std::function<bool( const pb::Element & )>;

        static void RegisterElementFilter( const std::string &pattern, ElementFilterCallback filter )
        {
            GetElementFilterRegistry()[pattern] = std::move( filter );
        }

        static void RegisterTombstoneFilter( const std::string &pattern, ElementFilterCallback filter )
        {
            GetTombstoneFilterRegistry()[pattern] = std::move( filter );
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
            std::vector<int> indices_to_keep;
            indices_to_keep.reserve( delta->elements_size() );

            for ( int i = 0; i < delta->elements_size(); ++i )
            {
                const auto &element      = delta->elements( i );
                bool        keep_element = true;
                for ( const auto &[pattern, filter] : GetElementFilterRegistry() )
                {
                    std::regex regex( pattern );
                    if ( std::regex_match( element.key(), regex ) )
                    {
                        // If a matching filter rejects this element, mark it for removal
                        if ( !filter( element ) )
                        {
                            keep_element = false;
                            break;
                        }
                    }
                }
                if ( keep_element )
                {
                    indices_to_keep.push_back( i );
                }
            }

            // If some elements need to be removed, rebuild the delta
            if ( static_cast<int>( indices_to_keep.size() ) < delta->elements_size() )
            {
                std::vector<pb::Element> kept_elements;
                kept_elements.reserve( indices_to_keep.size() );

                // Collect all kept elements
                for ( int idx : indices_to_keep )
                {
                    kept_elements.push_back( delta->elements( idx ) );
                }

                // Rebuild the delta with only the kept elements (might be empty)
                delta->clear_elements();
                for ( const auto &element : kept_elements )
                {
                    auto *new_element = delta->add_elements();
                    *new_element      = element;
                }
            }
        }

        static void FilterTombstonesOnDelta( std::shared_ptr<pb::Delta> &delta )
        {
            std::vector<int> indices_to_keep;
            indices_to_keep.reserve( delta->tombstones_size() );

            for ( int i = 0; i < delta->tombstones_size(); ++i )
            {
                const auto &tombstone      = delta->tombstones( i );
                bool        keep_tombstone = true;
                for ( const auto &[pattern, filter] : GetTombstoneFilterRegistry() )
                {
                    std::regex regex( pattern );
                    if ( std::regex_match( tombstone.key(), regex ) )
                    {
                        // If a matching filter rejects this tombstone, mark it for removal
                        if ( !filter( tombstone ) )
                        {
                            keep_tombstone = false;
                            break;
                        }
                    }
                }
                if ( keep_tombstone )
                {
                    indices_to_keep.push_back( i );
                }
            }

            // If some tombstones need to be removed, rebuild the delta
            if ( static_cast<int>( indices_to_keep.size() ) < delta->tombstones_size() )
            {
                std::vector<pb::Element> keep_tombstones;
                keep_tombstones.reserve( indices_to_keep.size() );

                // Collect all kept tombstones
                for ( int idx : indices_to_keep )
                {
                    keep_tombstones.push_back( delta->tombstones( idx ) );
                }

                // Rebuild the delta with only the kept tombstones (might be empty)
                delta->clear_tombstones();
                for ( const auto &tombstone : keep_tombstones )
                {
                    auto *new_tombstone = delta->add_tombstones();
                    *new_tombstone      = tombstone;
                }
            }
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

        //Logger                       logger_      = base::createLogger( "CrdtDatastore" );
    };

}

#endif
