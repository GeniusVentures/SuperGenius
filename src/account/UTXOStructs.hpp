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
        base::Hash256 txid_hash_;
        uint32_t      output_idx_;
        std::string   signature_;
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
