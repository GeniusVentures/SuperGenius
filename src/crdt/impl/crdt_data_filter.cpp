/**
 * @file       crdt_data_filter.cpp
 * @brief      Source file of the CRDT Filter class
 * @date       2025-05-12
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "crdt/crdt_data_filter.hpp"

namespace sgns::crdt
{
    CRDTDataFilter::CRDTDataFilter( bool accept_by_default ) : accept_by_default_( std::move( accept_by_default ) ) {}

    bool CRDTDataFilter::RegisterElementFilter( const std::string &pattern, ElementFilterCallback filter )
    {
        std::lock_guard lock( element_registry_mutex_ );
        element_registry_[pattern] = std::move( filter );
        return true;
    }

    bool CRDTDataFilter::RegisterTombstoneFilter( const std::string &pattern, ElementFilterCallback filter )
    {
        std::lock_guard lock( tombstone_registry_mutex_ );
        tombstone_registry_[pattern] = std::move( filter );
        return true;
    }

    void CRDTDataFilter::UnregisterElementFilter( const std::string &pattern )
    {
        std::lock_guard lock( element_registry_mutex_ );
        element_registry_.erase( pattern );
    }

    void CRDTDataFilter::UnregisterTombstoneFilter( const std::string &pattern )
    {
        std::lock_guard lock( tombstone_registry_mutex_ );
        tombstone_registry_.erase( pattern );
    }

    void CRDTDataFilter::FilterElementsOnDelta( std::shared_ptr<pb::Delta> &delta )
    {
        std::vector<pb::Element>                               new_tombstones;
        std::unordered_map<std::string, ElementFilterCallback> registry_copy;
        {
            std::shared_lock lock( element_registry_mutex_ );
            registry_copy = element_registry_;
        }

        for ( int i = 0; i < delta->elements_size(); ++i )
        {
            const auto &element        = delta->elements( i );
            bool        filter_matched = false;

            for ( const auto &[pattern, filter] : registry_copy )
            {
                std::regex regex( pattern );
                if ( std::regex_match( element.key(), regex ) )
                {
                    auto tombstones = filter( element );
                    if ( tombstones )
                    {
                        new_tombstones.insert( new_tombstones.end(), tombstones->begin(), tombstones->end() );
                    }
                    filter_matched = true;
                    break;
                }
            }
            if ( ( filter_matched == false ) && ( accept_by_default_ == false ) )
            {
                //at least tombstone the current element
                new_tombstones.push_back( element );
            }
        }

        for ( const auto &tombstone : new_tombstones )
        {
            auto *new_tombstone = delta->add_tombstones();
            *new_tombstone      = tombstone;
        }
    }

    void CRDTDataFilter::FilterTombstonesOnDelta( std::shared_ptr<pb::Delta> &delta )
    {
        //TODO - Figure out how to remove tombstones even recorded ones
        throw std::runtime_error("Not supported");
    }
}
