/**
 * @file       crdt_data_filter.cpp
 * @brief      Source file of the CRDT Filter class
 * @date       2025-05-12
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "crdt/crdt_data_filter.hpp"
#include "crdt/globaldb/crdt_work_journal.hpp"
#include <set>

namespace sgns::crdt
{
    CRDTDataFilter::CRDTDataFilter( std::shared_ptr<CRDTWorkJournal> work_journal, bool accept_by_default ) :
        work_journal_( std::move( work_journal ) ),          //
        accept_by_default_( std::move( accept_by_default ) ) //
    {
    }

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

    void CRDTDataFilter::FilterElementsOnDelta( pb::Delta &delta ) const
    {
        std::vector<std::string>         additional_elements_to_delete;
        std::set<int, std::greater<int>> elements_to_delete_indices; // Set with reverse order

        std::unordered_map<std::string, ElementFilterCallback> registry_copy;
        {
            std::shared_lock lock( element_registry_mutex_ );
            registry_copy = element_registry_;
        }

        for ( int i = 0; i < delta.elements_size(); ++i )
        {
            const auto &element        = delta.elements( i );
            bool        filter_matched = false;

            for ( const auto &[pattern, filter] : registry_copy )
            {
                if ( std::regex regex( pattern ); std::regex_match( element.key(), regex ) )
                {
                    auto result = filter( element );

                    if ( result.has_value() )
                    {
                        // Always delete the matching element when result has value
                        elements_to_delete_indices.insert( i );

                        if ( !result->empty() )
                        {
                            // Also delete additional elements from the vector
                            for ( const auto &additional_element : *result )
                            {
                                additional_elements_to_delete.push_back( additional_element.key() );
                            }
                        }
                    }
                    else
                    {
                        work_journal_->MarkSeen( element.key() );
                    }
                    filter_matched = true;
                    break;
                }
            }

            if ( !filter_matched && !accept_by_default_ )
            {
                //at least delete the current element
                elements_to_delete_indices.insert( i );
            }
        }

        // Second pass: find additional elements to delete
        for ( int i = 0; i < delta.elements_size(); ++i )
        {
            const auto &element = delta.elements( i );
            for ( const auto &key_to_delete : additional_elements_to_delete )
            {
                if ( element.key() == key_to_delete )
                {
                    elements_to_delete_indices.insert( i );
                    break;
                }
            }
        }

        for ( int index : elements_to_delete_indices )
        {
            delta.mutable_elements()->DeleteSubrange( index, 1 );
        }
    }

    void CRDTDataFilter::FilterTombstonesOnDelta( pb::Delta &delta )
    {
        //TODO - Figure out how to remove tombstones even recorded ones
        throw std::runtime_error( "Not supported" );
    }
}
