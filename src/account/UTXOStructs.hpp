#pragma once

#include "base/blob.hpp"
#include "TokenID.hpp"

namespace sgns
{
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
