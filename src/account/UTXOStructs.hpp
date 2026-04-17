#pragma once

#include "base/blob.hpp"
#include "TokenID.hpp"

namespace sgns
{
    namespace utxo_address
    {
        // Escrow locks are encoded as 0x + 32-byte hash (66 chars total).
        bool IsEscrowLockAddress( std::string_view address );

        // Genius account addresses are full pubkeys encoded as 64 bytes hex (128 chars).
        bool IsAccountPublicKeyAddress( std::string_view address );
    } // namespace utxo_address

    /**
     * @brief   Raw UTXO input data for a transaction
     */
    struct InputUTXOInfo
    {
        std::vector<uint8_t> SerializeForSigning() const;

        base::Hash256        txid_hash_;  //< Hash of the related transaction
        uint32_t             output_idx_; //< Index of the related output in the output vector
        std::vector<uint8_t> signature_;  //< Signature of the hash and index
    };

    /**
     * @brief   Single output entry
     */
    struct OutputDestInfo
    {
        uint64_t    encrypted_amount; ///< El Gamal encrypted amount
        std::string dest_address;     ///< Destination node address
        TokenID     token_id;         ///< Token identifier
    };

    using UTXOTxParameters = std::pair<std::vector<InputUTXOInfo>, std::vector<OutputDestInfo>>;
}
