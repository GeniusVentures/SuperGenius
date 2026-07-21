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

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sgns::multisig
{
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
                                 std::string_view            signature,
                                 const std::vector<uint8_t> &payload );

    /**
     * @brief       Result of an N-of-M quorum evaluation.
     */
    struct QuorumResult
    {
        bool     has_quorum         = false; ///< True when valid_unique_count >= threshold
        uint64_t valid_unique_count = 0;      ///< Count of distinct authorized signers with a valid signature
    };

    /**
     * @brief       Evaluates N-of-M quorum for a signer set + threshold + collected signatures.
     *
     *              For each (address, signature) pair in `collected_signatures`, in order:
     *              - skip if `address` has already been counted (dedup-first, before verification)
     *              - skip if `address` is not present in `signer_set` (unauthorized)
     *              - skip if `VerifyPayloadSignature` fails for that (address, signature, payload)
     *              Otherwise the address is counted as a valid unique signer.
     *
     *              This dedup-before-verify ordering guarantees a signer contributes at most
     *              one unit toward `valid_unique_count` regardless of how many entries for
     *              that signer appear in `collected_signatures`, even if one of the duplicate
     *              entries carries a garbage/invalid signature.
     *
     * @param[in]   signer_set Authorized signer addresses (the "M" in N-of-M).
     * @param[in]   threshold Minimum number of distinct valid signers required for quorum (the "N").
     * @param[in]   collected_signatures (address, signature) pairs collected from callers/peers.
     * @param[in]   payload Raw payload bytes the signatures are claimed to cover.
     * @return      QuorumResult with has_quorum and valid_unique_count populated.
     */
    QuorumResult EvaluateQuorum( const std::vector<std::string>                         &signer_set,
                                 uint64_t                                                threshold,
                                 const std::vector<std::pair<std::string, std::string>> &collected_signatures,
                                 const std::vector<uint8_t>                             &payload );
} // namespace sgns::multisig

#endif // SGNS_MULTISIG_MULTISIG_HPP
