#pragma once

#include "GeniusUTXO.hpp"
#include "UTXOStructs.hpp"
#include "base/logger.hpp"

#include <shared_mutex>

namespace sgns
{
    class UTXOManager
    {
    public:
        UTXOManager( const bool is_full_node, std::string address ) :
            is_full_node_( is_full_node ), address_( std::move( address ) )
        {
        }

        /**
         * @brief       Get the account's balance
         * @return      The total balance of the account
         */
        [[nodiscard]] uint64_t GetBalance() const;

        [[nodiscard]] uint64_t GetBalance( const std::string &address ) const;

        /**
         * @brief       Get the accounts balance for a specific token
         * @param[in]   token_id Token ID to get the balance
         * @return      The balance of the account for the specific token
         */
        uint64_t GetBalance( const TokenID &token_id ) const;

        uint64_t GetBalance( const TokenID &token_id, const std::string &address ) const;

        /**
         * @brief       Add a new UTXO to the account
         * @param[in]   new_utxo The new UTXO to be added
         * @param       address Address to add the UTXO to
         * @return      true if the UTXO was added, false otherwise
         */
        bool PutUTXO( const GeniusUTXO &new_utxo, const std::string &address );

        bool PutUTXO( const GeniusUTXO &new_utxo )
        {
            return PutUTXO( new_utxo, address_ );
        }

        /**
         * @brief       Delete a UTXO from the account
         * @param[in]   utxo_id The ID of the UTXO to be deleted
         * @param       address Address to remove the UTXO from
         */
        void DeleteUTXO( const base::Hash256 &utxo_id, const std::string &address );

        /**
         * @brief       Consume UTXOs from the account
         * @param[in]   infos Vector of UTXO information to be consumed
         * @return      true if all UTXOs were consumed, false otherwise
         */
        bool ConsumeUTXOs( const std::vector<InputUTXOInfo> &infos );

        /**
         * @brief       Get UTXOs for a specific address
         * @param[in]   address The address to get UTXOs for
         * @return      Vector of UTXOs for the address
         */
        std::vector<GeniusUTXO> GetUTXOs( const std::string &address ) const;

        std::vector<GeniusUTXO> GetUTXOs() const
        {
            return GetUTXOs( address_ );
        }

        /**
         * @brief       Set UTXOs for a specific address (replaces existing UTXOs)
         * @param[in]   utxos Vector of UTXOs to set for the address
         * @param[in]   address The address to set UTXOs for
         */
        void SetUTXOs( const std::vector<GeniusUTXO> &utxos, const std::string &address );

        void SetUTXOs( const std::vector<GeniusUTXO> &utxos )
        {
            SetUTXOs( utxos, address_ );
        }

        outcome::result<UTXOTxParameters> CreateTxParameter( uint64_t           amount,
                                                             const std::string &dest_address,
                                                             const TokenID     &token_id );

        outcome::result<UTXOTxParameters> CreateTxParameter( const std::vector<OutputDestInfo> &destinations,
                                                             const TokenID                     &token_id );

        void ReserveUTXOs( const std::vector<InputUTXOInfo> &inputs );

        void RollbackUTXOs( const std::vector<InputUTXOInfo> &inputs );

    private:
        base::Logger logger_ = base::createLogger( "UTXOManager" );

        bool        is_full_node_;
        std::string address_;

        mutable std::shared_mutex                                utxos_mutex_; ///< Mutex for the UTXOs map
        std::unordered_map<std::string, std::vector<GeniusUTXO>> utxos_;       ///< Map of UTXOs by address
    };

}
