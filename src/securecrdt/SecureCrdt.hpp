/**
 * @file       SecureCrdt.hpp
 * @brief      Mandatory wrapper (D-03) for writing to a registered CRDT key and
 *             the reader-side logic (D-04) that always re-derives trust from
 *             `base_key` + `sig/<addr>` children rather than trusting any "final"
 *             marker. This is the ONLY sanctioned write entry point for keys
 *             registered via SecureCrdtRegistry.
 * @date       2026-07-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_SECURECRDT_SECURECRDT_HPP
#define SGNS_SECURECRDT_SECURECRDT_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/hierarchical_key.hpp"
#include "crdt/proto/delta.pb.h"
#include "outcome/outcome.hpp"
#include "securecrdt/SecureCrdtRegistry.hpp"

namespace sgns::securecrdt
{
    /**
     * @brief Mandatory wrapper for reading/writing registered SecureCrdt keys.
     *
     *        `ProposeValue`/`AddSignature` are the only sanctioned callers of
     *        `GlobalDB::Put` for a registered key (D-03). `ReadIfQuorum` never
     *        writes and always re-derives trust from the current base_key value
     *        plus all `sig/<addr>` children (D-04) -- no "final" marker key is
     *        ever written or read by this class.
     */
    class SecureCrdt : public std::enable_shared_from_this<SecureCrdt>
    {
    public:
        /**
         * @enum  Error
         * @brief Error codes returned by SecureCrdt's write/read operations.
         */
        enum class Error : uint8_t
        {
            UNREGISTERED_KEY = 0, ///< base_key has no SecureCrdtRegistry entry
            NO_VALUE_PROPOSED,    ///< AddSignature/ReadIfQuorum called before any ProposeValue
            INVALID_SIGNATURE,    ///< signature failed VerifyPayloadSignature against the current value
            MALFORMED_VALUE,      ///< payload failed DeserializeFromBytes/Verify (codec/semantic check)
            QUORUM_THRESHOLD_BELOW_FLOOR, ///< configured quorum_threshold below ceil(0.51*signer_set_size)
        };

        /**
         * @brief Constructs a SecureCrdt wrapper over an existing GlobalDB instance.
         * @param[in] db GlobalDB instance to Put/Get/Query against.
         * @param[in] topic CRDT broadcast/listen topic to use for all Put calls
         *            (no new networking -- reuses whatever topic the caller's
         *            GlobalDB is already wired to).
         */
        SecureCrdt( std::shared_ptr<sgns::crdt::GlobalDB> db, std::string topic );

        /**
         * @brief Proposes a value for a registered base_key. Runs the SAME
         *        codec/semantic check the remote filter callback runs on a
         *        base_key element (DeserializeFromBytes + Verify) BEFORE ever
         *        calling Put -- closes the local/remote asymmetry gap (T-09-10).
         *        Proposing a value has no signature requirement by itself; it
         *        only becomes trusted once quorum-worth of sig-entries exist
         *        (D-04), so this is not a bypass of D-03.
         * @param[in] base_key Registered CRDT key to propose a value for.
         * @param[in] payload Raw payload bytes to persist.
         * @return outcome::success on success, Error::UNREGISTERED_KEY if
         *         base_key has no registry entry, Error::MALFORMED_VALUE if the
         *         codec/semantic check fails (Put is never called in that case).
         */
        outcome::result<void> ProposeValue( const sgns::crdt::HierarchicalKey &base_key,
                                            const std::vector<uint8_t>        &payload );

        /**
         * @brief Adds a signature over the CURRENT value at base_key. Fetches the
         *        value fresh via GlobalDB::Get each call (never a cached/stale
         *        value, closing the replay threat T-09-07) and verifies it via
         *        multisig::VerifyPayloadSignature before ever calling Put -- an
         *        invalid signature is never persisted (D-03 local-write gate).
         * @param[in] base_key Registered CRDT key the signature is claimed over.
         * @param[in] signer_address Address claimed to have produced `signature`.
         * @param[in] signature Raw signature bytes.
         * @return outcome::success on success, Error::UNREGISTERED_KEY if
         *         base_key has no registry entry, Error::NO_VALUE_PROPOSED if no
         *         value exists yet at base_key, Error::INVALID_SIGNATURE if
         *         verification fails (Put is never called in that case).
         */
        outcome::result<void> AddSignature( const sgns::crdt::HierarchicalKey &base_key,
                                            const std::string                 &signer_address,
                                            const std::vector<uint8_t>        &signature );

        /**
         * @brief Returns the current value at base_key only once the required number of
         *        valid unique signatures from the registered signer set are
         *        present (D-04 quorum re-derivation); returns std::nullopt if
         *        quorum is not yet met.
         * @note  This method deliberately does NOT deserialize, semantically-
         *        verify, or Apply() the returned bytes. Once quorum is
         *        confirmed, the CALLER is responsible for instantiating its own
         *        ISignedCRDTData implementer via DeserializeFromBytes(*result)
         *        and calling Verify()+Apply() on it. SecureCrdt stays generic
         *        across all registered types and never assumes which concrete
         *        ISignedCRDTData subclass or Apply() side effect applies to a
         *        given base_key -- that knowledge lives only with the
         *        registered type's own owner (e.g. Phase 10 TrustedPeerRegistry,
         *        Phase 11 BurnConfig, Phase 12 ValidatorRegistry migration).
         * @param[in] base_key Registered CRDT key to read.
         * @return outcome::success(bytes) if quorum is met, outcome::success(nullopt)
         *         if the key does not exist yet or quorum is not yet met, or
         *         Error::UNREGISTERED_KEY if base_key has no registry entry.
         */
        outcome::result<std::optional<sgns::base::Buffer>> ReadIfQuorum(
            const sgns::crdt::HierarchicalKey &base_key );

        /**
         * @brief Self-registration entry point: registers the element filter
         *        (D-03 second, independent enforcement layer for remote-
         *        originated deltas) for every currently-registered
         *        SecureCrdtRegistry entry. Must be called once after
         *        construction (e.g. from a New(...)-style factory), mirroring
         *        ValidatorRegistry::RegisterFilter's call-from-factory
         *        convention.
         * @return true if all filter registrations succeeded.
         */
        bool RegisterFilters();

    private:
        /**
         * @brief Filter callback re-running the identical enforcement logic as
         *        AddSignature's/ProposeValue's local gate, for remote-originated
         *        deltas only (crdt_datastore.cpp, !created_by_self). Rejects
         *        (returns an empty vector) on parse failure or invalid
         *        signature/value, accepts (returns std::nullopt) otherwise.
         *        Derives the concrete base key from `element.key()` so registry
         *        patterns containing regular expressions are never used as
         *        datastore keys.
         * @param[in] entry Resolved SecureCrdtRegistry entry for the element.
         * @param[in] element Incoming CRDT element (`base_key` value or `sig/<addr>` child).
         * @return std::nullopt to accept, or an (empty) vector to reject.
         */
        std::optional<std::vector<sgns::crdt::pb::Element>> FilterSecureCrdtUpdate(
            const SecureCrdtRegistryEntry &entry,
            const sgns::crdt::pb::Element &element );

        std::shared_ptr<sgns::crdt::GlobalDB> db_;
        std::string                           topic_;
        sgns::base::Logger                    logger_ = sgns::base::createLogger( "SecureCrdt" );
    };
} // namespace sgns::securecrdt

OUTCOME_HPP_DECLARE_ERROR_2( sgns::securecrdt, SecureCrdt::Error );

#endif // SGNS_SECURECRDT_SECURECRDT_HPP
