/**
 * @file       UTXOStructs.hpp
 * @brief      Shared UTXO transaction input and output data structures.
 * @date       2026-01-20
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_UTXO_STRUCTS_HPP
#define SGNS_UTXO_STRUCTS_HPP

#include "base/blob.hpp"
#include "TokenID.hpp"

namespace sgns
{
    namespace utxo_address
    {
        /**
         * @brief       Checks if the address is a valid escrow lock address (0x-prefixed 64 hex chars)
         * @param[in]   address The address to check
         * @return      true if the address is a valid escrow lock address, false otherwise
         */
        bool IsEscrowLockAddress( std::string_view address );

        /**
         * @brief       Checks if the address is a public key 
         * @param[in]   address The address to check
         * @return      true if the address is a public key address, false otherwise
         */
        bool IsAccountPublicKeyAddress( std::string_view address );
    } // namespace utxo_address

    /**
     * @brief Raw UTXO input data included in a spend request.
     */
    struct InputUTXOInfo
    {
        /**
         * @brief Serializes the input fields that must be signed by the owner.
         */
        std::vector<uint8_t> SerializeForSigning() const;

        base::Hash256        txid_hash_;  ///< Hash of the transaction that created the output.
        uint32_t             output_idx_; ///< Zero-based output index within the originating transaction.
        std::vector<uint8_t> signature_;  ///< Signature authorizing the spend of this outpoint.
    };

    /**
     * @brief Single UTXO output destination entry.
     */
    struct OutputDestInfo
    {
        uint64_t    encrypted_amount; ///< El Gamal encrypted amount
        std::string dest_address;     ///< Destination node address
        TokenID     token_id;         ///< Token identifier
    };

    /**
     * @brief Pair of signed inputs and destination outputs that make up a UTXO transaction payload.
     */
    using UTXOTxParameters = std::pair<std::vector<InputUTXOInfo>, std::vector<OutputDestInfo>>;
}

#endif // SGNS_UTXO_STRUCTS_HPP
