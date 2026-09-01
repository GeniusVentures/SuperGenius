/**
 * @file       SecureCrdtRegistry.hpp
 * @brief      Instance registry mapping a base_key pattern to {signer-set source,
 *             required signature count, ISignedCRDTData factory}, resolvable at
 *             startup/runtime by key. Each SecureCrdt owns an independent
 *             instance so in-process nodes cannot replace one another's policy.
 * @date       2026-07-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_SECURECRDT_SECURECRDTREGISTRY_HPP
#define SGNS_SECURECRDT_SECURECRDTREGISTRY_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "outcome/outcome.hpp"
#include "securecrdt/ISignedCRDTData.hpp"
#include "securecrdt/SecureCrdtCandidate.hpp"

namespace sgns::securecrdt
{
    /**
     * @brief Snapshot of an authorized signer set and its required signature count,
     *        as produced by an injected SignerSetSource.
     */
    struct SignerSetSnapshot
    {
        std::vector<std::string> signer_set;
        uint64_t                 required_signatures = 0;
    };

    /**
     * @brief Injectable callback resolving the current authorized signer set
     *        for a given base_key. NOT hard-wired to TrustedPeerRegistry (which
     *        does not exist until Phase 10) - tests inject a fixed-list lambda.
     */
    using SignerSetSource = std::function<outcome::result<SignerSetSnapshot>( const std::string &base_key )>;

    struct CandidateAuthorizationSnapshot
    {
        uint16_t                 network_id   = 0;
        CandidateKind            kind         = CandidateKind::TrustPolicy;
        uint64_t                 next_version = 0;
        std::string              expected_previous_hash;
        std::string              authorizing_policy_hash;
        std::vector<std::string> authorized_signers;
    };

    using CandidateAuthorizationSource = std::function<outcome::result<CandidateAuthorizationSnapshot>()>;

    struct CandidateDomainEntry
    {
        std::string                  domain;
        CandidateKind                kind = CandidateKind::TrustPolicy;
        CandidateAuthorizationSource authorization_source;
        const void                  *owner_token = nullptr;
    };

    /**
     * @brief Policy entry describing how a registered key pattern is verified
     *        and instantiated.
     */
    struct SecureCrdtRegistryEntry
    {
        std::string                                       key_pattern;
        SignerSetSource                                   signer_set_source;
        std::function<std::shared_ptr<ISignedCRDTData>()> make_instance;
        std::regex                                        compiled_pattern;
        /// @brief Opaque token supplied by the caller at Register() time; must
        ///        be presented verbatim to UnregisterIf() to remove this entry.
        const void *owner_token = nullptr;
    };

    /**
     * @brief Thread-safe registry resolving a CRDT key to its policy entry.
     */
    class SecureCrdtRegistry
    {
    public:
        /**
         * @brief Registers the policy entry for `key_pattern` if absent.
         *        Compiles `compiled_pattern` as "/?" + key_pattern + "(/sig/[^/]+)?"
         *        so both the base key and a valid `sig/<addr>` child resolve to the
         *        same entry - mirrors CRDTDataFilter::RegisterElementFilter's
         *        regex shape (src/crdt/impl/crdt_data_filter.cpp).
         * @param[in] key_pattern Base key pattern (regex-escaped by the caller
         *            if it contains regex metacharacters).
         * @param[in] entry Policy entry to register (compiled_pattern is
         *            overwritten by this call).
         * @return true when inserted; false when this registry already owns
         *         the same pattern. Existing registrations are never replaced.
         */
        bool Register( const std::string &key_pattern, SecureCrdtRegistryEntry entry )
        {
            entry.key_pattern      = key_pattern;
            entry.compiled_pattern = std::regex( "/?" + key_pattern + "(/sig/[^/]+)?" );
            std::unique_lock<std::shared_mutex> lock( registry_mutex_ );
            return registry_.emplace( key_pattern, std::move( entry ) ).second;
        }

        /**
         * @brief Removes the registration for `key_pattern` only if the caller's
         *        token matches the token supplied at Register() time
         *        (compare-and-remove, prevents a second unrelated registration
         *        from clobbering removal).
         * @param[in] key_pattern Base key pattern to unregister.
         * @param[in] expected_token Opaque token that must match the registering
         *            token for the removal to take effect.
         */
        void UnregisterIf( const std::string &key_pattern, const void *expected_token )
        {
            std::unique_lock<std::shared_mutex> lock( registry_mutex_ );
            auto                                it = registry_.find( key_pattern );
            if ( it != registry_.end() && it->second.owner_token == expected_token )
            {
                registry_.erase( it );
            }
        }

        /**
         * @brief Resolves `key` against all registered patterns, returning the
         *        first matching entry (base_key or any `sig/<addr>` child).
         * @param[in] key CRDT key to resolve.
         * @return Snapshot of the matching entry, or std::nullopt if unregistered.
         */
        std::optional<SecureCrdtRegistryEntry> Resolve( const std::string &key ) const
        {
            std::shared_lock<std::shared_mutex> lock( registry_mutex_ );
            for ( const auto &[pattern, entry] : registry_ )
            {
                if ( std::regex_match( key, entry.compiled_pattern ) )
                {
                    return entry;
                }
            }
            return std::nullopt;
        }

        /**
         * @brief Returns a snapshot copy of every currently-registered entry.
         *        Used by SecureCrdt::RegisterFilters to self-register a filter
         *        callback for each registered base_key pattern at startup.
         * @return Vector of registered entries (order unspecified).
         */
        std::vector<SecureCrdtRegistryEntry> AllEntries() const
        {
            std::shared_lock<std::shared_mutex>  lock( registry_mutex_ );
            std::vector<SecureCrdtRegistryEntry> entries;
            entries.reserve( registry_.size() );
            for ( const auto &[pattern, entry] : registry_ )
            {
                entries.push_back( entry );
            }
            return entries;
        }

        bool RegisterCandidateDomain( const std::string &domain, CandidateDomainEntry entry )
        {
            entry.domain = domain;
            std::unique_lock<std::shared_mutex> lock( registry_mutex_ );
            return candidate_domains_.emplace( domain, std::move( entry ) ).second;
        }

        void UnregisterCandidateDomainIf( const std::string &domain, const void *expected_token )
        {
            std::unique_lock<std::shared_mutex> lock( registry_mutex_ );
            auto                                it = candidate_domains_.find( domain );
            if ( it != candidate_domains_.end() && it->second.owner_token == expected_token )
            {
                candidate_domains_.erase( it );
            }
        }

        std::optional<CandidateDomainEntry> ResolveCandidateDomain( const std::string &domain ) const
        {
            std::shared_lock<std::shared_mutex> lock( registry_mutex_ );
            const auto                          it = candidate_domains_.find( domain );
            return it == candidate_domains_.end() ? std::nullopt : std::optional<CandidateDomainEntry>( it->second );
        }

        std::vector<CandidateDomainEntry> AllCandidateDomains() const
        {
            std::shared_lock<std::shared_mutex> lock( registry_mutex_ );
            std::vector<CandidateDomainEntry>   entries;
            entries.reserve( candidate_domains_.size() );
            for ( const auto &[domain, entry] : candidate_domains_ )
            {
                entries.push_back( entry );
            }
            return entries;
        }

    private:
        std::unordered_map<std::string, SecureCrdtRegistryEntry> registry_;
        std::unordered_map<std::string, CandidateDomainEntry>    candidate_domains_;
        mutable std::shared_mutex                                registry_mutex_;
    };
} // namespace sgns::securecrdt

#endif // SGNS_SECURECRDT_SECURECRDTREGISTRY_HPP
