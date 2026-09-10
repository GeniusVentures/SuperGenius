/**
 * @file       crdt_data_filter.hpp
 * @brief      Header file of the CRDT Filter class
 * @date       2025-04-07
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _CRDT_DATA_FILTER_HPP_
#define _CRDT_DATA_FILTER_HPP_

#include <functional>
#include <optional>
#include <string>
#include <memory>
#include <regex>
#include <shared_mutex>
#include <system_error>
#include <utility>
#include <vector>

#include "crdt/proto/delta.pb.h"

namespace sgns::crdt
{
    class CRDTWorkJournal;

    /// Error the datastore maps a stalled element filter to. Signals "this
    /// element's dependencies are not present locally yet" (e.g. a registry
    /// update whose member certificates have not synced): the whole delta job
    /// fails without recording its head, so the existing failed-root retry
    /// schedule (and any fresh rebroadcast of the CID) reprocesses the delta
    /// once the dependencies arrive. Distinct from rejection: a rejected
    /// element is stripped permanently, a stalled one is retried.
    inline constexpr std::errc ElementFilterDependencyStalled = std::errc::resource_unavailable_try_again;

    class CRDTDataFilter
    {
    public:
        /**
         * @brief      Result of a single element-filter evaluation.
         */
        struct ElementFilterResult
        {
            enum class Decision
            {
                kAccept, ///< Keep the element (mark seen in the work journal).
                kReject, ///< Strip the element, plus any extra elements listed below.
                kStall,  ///< Dependencies missing locally: fail the whole delta job
                         ///< so it is retried (no head recorded, nothing applied).
            };

            Decision                 decision = Decision::kAccept;
            std::vector<pb::Element> additional_elements_to_remove; ///< Extra elements stripped when kReject.

            static ElementFilterResult Accept()
            {
                return {};
            }
            static ElementFilterResult Reject( std::vector<pb::Element> extra = {} )
            {
                return ElementFilterResult{ Decision::kReject, std::move( extra ) };
            }
            static ElementFilterResult Stall()
            {
                return ElementFilterResult{ Decision::kStall, {} };
            }
            /// Adapts the legacy optional contract: nullopt accepts, a value strips.
            static ElementFilterResult FromOptional( std::optional<std::vector<pb::Element>> legacy )
            {
                if ( legacy.has_value() )
                {
                    return Reject( std::move( legacy.value() ) );
                }
                return Accept();
            }
        };

        /**
         * @brief      Element filtering callback definition
         */
        using ElementFilterCallback = std::function<ElementFilterResult( const pb::Element & )>;

        struct FilterCallbackEntry
        {
            std::string           pattern;
            std::regex            regex;
            ElementFilterCallback filter;
        };

        using FilterCallbackRegistry = std::vector<std::shared_ptr<const FilterCallbackEntry>>;

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
         * @brief       Removes the registration of a tombstone filter that corresponds to a pattern
         * @param[in]   pattern The regex/pattern that the key of the tombstone has to match
         */
        void UnregisterTombstoneFilter( const std::string &pattern );

        /**
         * @brief       Tries to filter the elements on delta according to stored filters
         * @param[in]   delta The delta to be filtered
         * @return      true when a filter stalled on a missing local dependency —
         *              the caller must fail the delta job without recording its
         *              head so the retry machinery reprocesses it; false when
         *              every element was accepted or stripped.
         */
        bool FilterElementsOnDelta( pb::Delta &delta ) const;

        /**
         * @brief       Tries to filter the tombstones on delta according to stored filters
         * @param[in]   delta The delta to be filtered
         */
        void FilterTombstonesOnDelta( pb::Delta &delta );

    private:
        std::shared_ptr<CRDTWorkJournal> work_journal_;
        const bool                accept_by_default_;      ///< The default behavior for values not matching any filter
        mutable std::shared_mutex element_registry_mutex_; ///< Mutex for the element registry
        std::shared_mutex         tombstone_registry_mutex_; ///< Mutex for the tombstone registry
        FilterCallbackRegistry    element_registry_;         ///< Element filter callback registry
        FilterCallbackRegistry    tombstone_registry_;       ///< Tombstone filter callback registry
    };

}

#endif
