/**
 * @file       ISignedCRDTData.hpp
 * @brief      Per-type interface for CRDT-backed values that require quorum-signed
 *             updates. Every future registered type (TrustedPeerRegistry, BurnConfig,
 *             migrated ValidatorRegistry) implements this. No template - mirrors
 *             IInputValidator's per-type virtual-interface style exactly.
 * @date       2026-07-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_SECURECRDT_ISIGNEDCRDTDATA_HPP
#define SGNS_SECURECRDT_ISIGNEDCRDTDATA_HPP

#include <cstdint>
#include <vector>

namespace sgns::securecrdt
{
    /**
     * @brief Interface implemented by every CRDT-backed value that requires a
     *        quorum of authorized signers to create/update.
     */
    class ISignedCRDTData
    {
    public:
        virtual ~ISignedCRDTData() = default;

        /**
         * @brief Serializes this instance's payload to raw bytes (codec, encode side).
         * @return Raw payload bytes representing this instance's current state.
         */
        virtual std::vector<uint8_t> SerializeToBytes() const = 0;

        /**
         * @brief Deserializes raw bytes into this instance's payload (codec, decode side).
         *
         *        Must return false on malformed/truncated input rather than throwing
         *        or asserting (V5 Input Validation) - mirrors
         *        ValidatorRegistry::DeserializeRegistryUpdate's parse-failure-returns-error
         *        pattern.
         * @param[in] bytes Raw payload bytes to decode.
         * @return true if `bytes` decoded successfully, false if malformed.
         */
        virtual bool DeserializeFromBytes( const std::vector<uint8_t> &bytes ) = 0;

        /**
         * @brief Performs type-specific semantic validation of `payload` beyond
         *        signature/quorum checks (e.g. field-range checks).
         * @param[in] payload Raw payload bytes to validate.
         * @return true if `payload` is semantically valid for this type.
         */
        virtual bool Verify( const std::vector<uint8_t> &payload ) const = 0;

        /**
         * @brief Applies the side effect of this value once the caller has
         *        independently confirmed quorum (e.g. via SecureCrdt::ReadIfQuorum).
         *        Apply() itself does NOT check quorum.
         */
        virtual void Apply() = 0;
    };
} // namespace sgns::securecrdt

#endif // SGNS_SECURECRDT_ISIGNEDCRDTDATA_HPP
