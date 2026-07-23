/**
 * @file       crdt_data_filter.cpp
 * @brief      Source file of the CRDT Filter class
 * @date       2025-05-12
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "crdt/crdt_data_filter.hpp"
#include "crdt/globaldb/crdt_work_journal.hpp"
#include <algorithm>
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
        try
        {
            auto entry = std::make_shared<FilterCallbackEntry>(
                FilterCallbackEntry{ pattern, std::regex( pattern ), std::move( filter ) } );
            std::lock_guard lock( element_registry_mutex_ );
            auto it = std::find_if( element_registry_.begin(),
                                    element_registry_.end(),
                                    [&]( const auto &registered ) { return registered->pattern == pattern; } );
            if ( it == element_registry_.end() )
            {
                element_registry_.push_back( std::move( entry ) );
            }
            else
            {
                *it = std::move( entry );
            }
            return true;
        }
        catch ( const std::regex_error & )
        {
            return false;
        }
    }

    bool CRDTDataFilter::RegisterDeltaFilter( const std::string &pattern, DeltaFilterCallback filter )
    {
        try
        {
            auto entry = std::make_shared<DeltaFilterCallbackEntry>(
                DeltaFilterCallbackEntry{ pattern, std::regex( pattern ), std::move( filter ) } );
            std::lock_guard lock( delta_registry_mutex_ );
            auto it = std::find_if( delta_registry_.begin(),
                                    delta_registry_.end(),
                                    [&]( const auto &registered ) { return registered->pattern == pattern; } );
            if ( it == delta_registry_.end() )
            {
                delta_registry_.push_back( std::move( entry ) );
            }
            else
            {
                *it = std::move( entry );
            }
            return true;
        }
        catch ( const std::regex_error & )
        {
            return false;
        }
    }

    bool CRDTDataFilter::RegisterTombstoneFilter( const std::string &pattern, ElementFilterCallback filter )
    {
        try
        {
            auto entry = std::make_shared<FilterCallbackEntry>(
                FilterCallbackEntry{ pattern, std::regex( pattern ), std::move( filter ) } );
            std::lock_guard lock( tombstone_registry_mutex_ );
            auto it = std::find_if( tombstone_registry_.begin(),
                                    tombstone_registry_.end(),
                                    [&]( const auto &registered ) { return registered->pattern == pattern; } );
            if ( it == tombstone_registry_.end() )
            {
                tombstone_registry_.push_back( std::move( entry ) );
            }
            else
            {
                *it = std::move( entry );
            }
            return true;
        }
        catch ( const std::regex_error & )
        {
            return false;
        }
    }

    void CRDTDataFilter::UnregisterElementFilter( const std::string &pattern )
    {
        std::lock_guard lock( element_registry_mutex_ );
        element_registry_.erase( std::remove_if( element_registry_.begin(),
                                                 element_registry_.end(),
                                                 [&]( const auto &entry ) { return entry->pattern == pattern; } ),
                                 element_registry_.end() );
    }

    void CRDTDataFilter::UnregisterDeltaFilter( const std::string &pattern )
    {
        std::lock_guard lock( delta_registry_mutex_ );
        delta_registry_.erase( std::remove_if( delta_registry_.begin(),
                                               delta_registry_.end(),
                                               [&]( const auto &entry ) { return entry->pattern == pattern; } ),
                               delta_registry_.end() );
    }

    void CRDTDataFilter::UnregisterTombstoneFilter( const std::string &pattern )
    {
        std::lock_guard lock( tombstone_registry_mutex_ );
        tombstone_registry_.erase( std::remove_if( tombstone_registry_.begin(),
                                                   tombstone_registry_.end(),
                                                   [&]( const auto &entry ) { return entry->pattern == pattern; } ),
                                   tombstone_registry_.end() );
    }

    FilteredDeltaResult CRDTDataFilter::FilterDelta( pb::Delta delta ) const
    {
        DeltaFilterCallbackRegistry delta_registry_snapshot;
        {
            std::shared_lock lock( delta_registry_mutex_ );
            delta_registry_snapshot = delta_registry_;
        }

        DeltaFilterDecision aggregate = DeltaFilterDecision::Approve;
        std::optional<std::string> dependency;
        DeltaFilterCallbackRegistry rejected_entries;
        for ( const auto &entry : delta_registry_snapshot )
        {
            bool matched = false;
            for ( const auto &element : delta.elements() )
            {
                if ( std::regex_match( element.key(), entry->regex ) )
                {
                    matched = true;
                    break;
                }
            }
            if ( !matched )
            {
                for ( const auto &tombstone : delta.tombstones() )
                {
                    if ( std::regex_match( tombstone.key(), entry->regex ) )
                    {
                        matched = true;
                        break;
                    }
                }
            }
            if ( !matched )
            {
                continue;
            }

            auto result = entry->filter( delta );
            if ( result.decision == DeltaFilterDecision::RetryDependency && !result.dependency_cid )
            {
                result = DeltaFilterResult::Reject();
            }
            if ( result.decision == DeltaFilterDecision::Reject )
            {
                aggregate = DeltaFilterDecision::Reject;
                rejected_entries.push_back( entry );
            }
            else if ( aggregate != DeltaFilterDecision::Reject &&
                      result.decision == DeltaFilterDecision::RetryDependency )
            {
                aggregate  = DeltaFilterDecision::RetryDependency;
                dependency = std::move( result.dependency_cid );
            }
        }

        if ( aggregate == DeltaFilterDecision::RetryDependency )
        {
            return { std::move( delta ), aggregate, std::move( dependency ) };
        }

        if ( aggregate == DeltaFilterDecision::Reject )
        {
            for ( const auto &entry : rejected_entries )
            {
                bool matched = false;
                for ( const auto &element : delta.elements() )
                {
                    matched |= std::regex_match( element.key(), entry->regex );
                }
                for ( const auto &tombstone : delta.tombstones() )
                {
                    matched |= std::regex_match( tombstone.key(), entry->regex );
                }
                if ( !matched )
                {
                    continue;
                }

                for ( int i = delta.elements_size() - 1; i >= 0; --i )
                {
                    if ( std::regex_match( delta.elements( i ).key(), entry->regex ) )
                    {
                        delta.mutable_elements()->DeleteSubrange( i, 1 );
                    }
                }
                for ( int i = delta.tombstones_size() - 1; i >= 0; --i )
                {
                    if ( std::regex_match( delta.tombstones( i ).key(), entry->regex ) )
                    {
                        delta.mutable_tombstones()->DeleteSubrange( i, 1 );
                    }
                }
            }
        }

        std::vector<std::string>         additional_elements_to_delete;
        std::set<int, std::greater<int>> elements_to_delete_indices;
        FilterCallbackRegistry registry_snapshot;
        {
            std::shared_lock lock( element_registry_mutex_ );
            registry_snapshot = element_registry_;
        }

        for ( int i = 0; i < delta.elements_size(); ++i )
        {
            const auto &element        = delta.elements( i );
            bool        filter_matched = false;

            for ( const auto &entry : registry_snapshot )
            {
                if ( std::regex_match( element.key(), entry->regex ) )
                {
                    auto result = entry->filter( element );

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

        return { std::move( delta ), aggregate, std::nullopt };
    }

    void CRDTDataFilter::FilterElementsOnDelta( pb::Delta &delta ) const
    {
        delta = FilterDelta( std::move( delta ) ).delta;
    }

    void CRDTDataFilter::FilterTombstonesOnDelta( pb::Delta &delta )
    {
        //TODO - Figure out how to remove tombstones even recorded ones
        throw std::runtime_error( "Not supported" );
    }
}
