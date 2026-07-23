/**
 * @file       SecureCrdtRegistry.hpp
 * @brief      Static registry mapping a base_key pattern to {signer-set source,
 *             quorum threshold, ISignedCRDTData factory}, resolvable at
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
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "outcome/outcome.hpp"
#include "securecrdt/ISignedCRDTData.hpp"

namespace sgns::securecrdt
{
    /**
     * @brief Snapshot of an authorized signer set and the quorum threshold
     *        required over it, as produced by an injected SignerSetSource.
     */
    struct SignerSetSnapshot
    {
        std::vector<std::string> signer_set;
        uint64_t                 threshold = 0;
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
    };

    /**
     * @brief Static registry resolving a CRDT key to its SecureCrdtRegistryEntry.
     */
    class SecureCrdtRegistry
    {
    public:
        /**
         * @brief Registers (or replaces) the policy entry for `key_pattern`.
         *        Compiles `compiled_pattern` as "/?" + key_pattern + "(/sig/.*)?"
         *        so both the base key and any `sig/<addr>` child resolve to the
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
            entry.compiled_pattern = std::regex( "/?" + key_pattern + "(/sig/.*)?" );
            registry()[key_pattern] = std::move( entry );
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
            auto it = registry().find( key_pattern );
            if ( it != registry().end() && it->second.owner_token == expected_token )
            {
                registry().erase( it );
            }
        }

        /**
         * @brief Resolves `key` against all registered patterns, returning the
         *        first matching entry (base_key or any `sig/<addr>` child).
         * @param[in] key CRDT key to resolve.
         * @return Pointer to the matching entry, or nullptr if unregistered.
         */
        static const SecureCrdtRegistryEntry *Resolve( const std::string &key )
        {
            for ( const auto &[pattern, entry] : registry() )
            {
                if ( std::regex_match( key, entry.compiled_pattern ) )
                {
                    return &entry;
                }
            }
            return nullptr;
        }

    private:
        static std::unordered_map<std::string, SecureCrdtRegistryEntry> &registry()
        {
            static std::unordered_map<std::string, SecureCrdtRegistryEntry> map;
            return map;
        }
    };
} // namespace sgns::securecrdt

#endif // SGNS_SECURECRDT_SECURECRDTREGISTRY_HPP
