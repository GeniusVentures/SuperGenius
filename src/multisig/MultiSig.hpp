/**
 * @file       MultiSig.hpp
 * @brief      Standalone multi-signature primitive: payload signature verification
 *             and N-of-M quorum evaluation over a signer set. No CRDT, node, or
 *             pubsub dependency (MSIG-03).
 * @date       2026-07-21
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_MULTISIG_MULTISIG_HPP
#define SGNS_MULTISIG_MULTISIG_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sgns::multisig
{
    using SignatureBytes      = std::vector<uint8_t>;
    using CollectedSignatures = std::vector<std::pair<std::string, SignatureBytes>>;

    /**
     * @brief       Verifies a signature over an arbitrary raw-byte payload.
     *              Delegates entirely to `GeniusAccount::VerifySignature`; no
     *              custom crypto or signing-bytes construction is performed here
     *              (the payload is assumed already-canonical).
     * @param[in]   address Public address (hex-encoded public key) the signature
     *              is claimed to be from.
     * @param[in]   signature Raw signature bytes to verify.
     * @param[in]   payload Raw payload bytes the signature is claimed to cover.
     * @return      true if the signature is valid for the given address and
     *              payload, false otherwise (including malformed address or
     *              incorrectly sized signature).
     */
    bool VerifyPayloadSignature( const std::string          &address,
                                 const SignatureBytes       &signature,
                                 const std::vector<uint8_t> &payload );

    /**
     * @brief       Result of an N-of-M quorum evaluation.
     */
    struct QuorumResult
    {
        bool     has_quorum         = false; ///< True when valid_unique_count >= required signatures
        uint64_t valid_unique_count = 0;      ///< Count of distinct authorized signers with a valid signature
    };

    /**
     * @brief Configured N-of-M signature evaluator.
     *
     * The constructor fixes the quorum policy for the evaluator's lifetime:
     * `required_signatures` is N and the number of unique addresses in
     * `signer_set` is M. A configuration is valid only when 1 <= N <= M.
     */
    class MultiSig
    {
    public:
        MultiSig( const std::vector<std::string> &signer_set, uint64_t required_signatures );

        [[nodiscard]] bool IsValid() const;
        [[nodiscard]] uint64_t RequiredSignatures() const;
        [[nodiscard]] size_t AuthorizedSignerCount() const;

        /**
         * @brief Evaluates collected signatures against this instance's N-of-M policy.
         *
         * For each (address, signature) pair, in order:
         * - skip an address already counted
         * - skip an address outside the configured signer set
         * - skip an invalid signature
         * - otherwise count the address as one valid unique signer
         *
         * Invalid N-of-M configurations always fail closed with `has_quorum == false`.
         */
        QuorumResult EvaluateQuorum( const CollectedSignatures  &collected_signatures,
                                     const std::vector<uint8_t> &payload ) const;

    private:
        std::unordered_set<std::string> signer_set_;
        uint64_t                        required_signatures_ = 0;
    };
} // namespace sgns::multisig

#endif // SGNS_MULTISIG_MULTISIG_HPP
