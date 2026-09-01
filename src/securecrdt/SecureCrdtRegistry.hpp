/**
 * @file       SecureCrdtRegistry.hpp
 * @brief      Static registry mapping a base_key pattern to {signer-set source,
 *             required signature count, ISignedCRDTData factory}, resolvable at
 *             startup/runtime by key. Header-only, mirrors IInputValidator's
 *             static Register/UnregisterIf/Get idiom.
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

namespace sgns::peerregistry
{
    class PeerRegistry; // complete type not needed here - association only (D-04);
                        // the adaptation helper lives in peerregistry/PeerRegistry.hpp
                        // to avoid an include cycle.
} // namespace sgns::peerregistry

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

    /**
     * @brief Policy entry describing how a registered key pattern is verified
     *        and instantiated.
     */
    struct SecureCrdtRegistryEntry
    {
        std::string                                          key_pattern;
        SignerSetSource                                      signer_set_source;
        std::function<std::shared_ptr<ISignedCRDTData>()>    make_instance;
        std::regex                                           compiled_pattern;
        /// @brief Opaque token supplied by the caller at Register() time; must
        ///        be presented verbatim to UnregisterIf() to remove this entry.
        const void                                          *owner_token = nullptr;
        /// @brief Explicit association to the PeerRegistry instance that owns
        ///        this key pattern's authorization (D-04) - defaults to null
        ///        for entries whose signer_set_source was built without a
        ///        registry. Shared ownership per the BurnConfig shared_ptr
        ///        registry precedent. Register() stores it verbatim and never
        ///        replaces an explicitly provided signer_set_source; entries
        ///        wanting a registry-derived source build it with
        ///        peerregistry::MakeRegistrySignerSetSource. Declared LAST so
        ///        existing positional aggregate initializers (which supply
        ///        owner_token as the final element) stay source-compatible.
        std::shared_ptr<sgns::peerregistry::PeerRegistry>    peer_registry;
    };

    /**
     * @brief Static registry resolving a CRDT key to its SecureCrdtRegistryEntry.
     */
    class SecureCrdtRegistry
    {
    public:
        /**
         * @brief Registers (or replaces) the policy entry for `key_pattern`.
         *        Compiles `compiled_pattern` as "/?" + key_pattern + "(/sig/[^/]+)?"
         *        so both the base key and a valid `sig/<addr>` child resolve to the
         *        same entry - mirrors CRDTDataFilter::RegisterElementFilter's
         *        regex shape (src/crdt/impl/crdt_data_filter.cpp).
         * @param[in] key_pattern Base key pattern (regex-escaped by the caller
         *            if it contains regex metacharacters).
         * @param[in] entry Policy entry to register (compiled_pattern is
         *            overwritten by this call).
         */
        static void Register( const std::string &key_pattern, SecureCrdtRegistryEntry entry )
        {
            entry.key_pattern     = key_pattern;
            entry.compiled_pattern = std::regex( "/?" + key_pattern + "(/sig/[^/]+)?" );
            {
                // Unlink any replaced entry WITHOUT destroying it while the
                // registry mutex is held: a replaced entry's peer_registry may
                // own the last reference to a PeerRegistry whose destructor
                // re-enters Unregister() -> UnregisterIf() (destruction
                // re-entrancy; std::shared_mutex is not recursive).
                std::unique_lock<std::shared_mutex> lock( registryMutex() );
                auto replaced = registry().extract( key_pattern );
                lock.unlock();
            } // replaced node (if any) destroyed here, mutex released
            std::unique_lock<std::shared_mutex> lock( registryMutex() );
            registry().insert_or_assign( key_pattern, std::move( entry ) );
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
        static void UnregisterIf( const std::string &key_pattern, const void *expected_token )
        {
            std::unique_lock<std::shared_mutex> lock( registryMutex() );
            auto it = registry().find( key_pattern );
            if ( it != registry().end() && it->second.owner_token == expected_token )
            {
                // Unlink without destroying under the lock: the entry's
                // peer_registry may own the last PeerRegistry reference, whose
                // destructor re-enters Unregister() -> UnregisterIf()
                // (destruction re-entrancy; std::shared_mutex is not recursive).
                auto node = registry().extract( it );
                lock.unlock();
            } // node destroyed here, mutex released
        }

        /**
         * @brief Resolves `key` against all registered patterns, returning the
         *        first matching entry (base_key or any `sig/<addr>` child).
         * @param[in] key CRDT key to resolve.
         * @return Snapshot of the matching entry, or std::nullopt if unregistered.
         */
        static std::optional<SecureCrdtRegistryEntry> Resolve( const std::string &key )
        {
            std::shared_lock<std::shared_mutex> lock( registryMutex() );
            for ( const auto &[pattern, entry] : registry() )
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
        static std::vector<SecureCrdtRegistryEntry> AllEntries()
        {
            std::shared_lock<std::shared_mutex> lock( registryMutex() );
            std::vector<SecureCrdtRegistryEntry> entries;
            entries.reserve( registry().size() );
            for ( const auto &[pattern, entry] : registry() )
            {
                entries.push_back( entry );
            }
            return entries;
        }

    private:
        static std::unordered_map<std::string, SecureCrdtRegistryEntry> &registry()
        {
            static std::unordered_map<std::string, SecureCrdtRegistryEntry> map;
            return map;
        }

        static std::shared_mutex &registryMutex()
        {
            static std::shared_mutex mutex;
            return mutex;
        }
    };
} // namespace sgns::securecrdt

#endif // SGNS_SECURECRDT_SECURECRDTREGISTRY_HPP
