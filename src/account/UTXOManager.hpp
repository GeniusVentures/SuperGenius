#pragma once

#include "GeniusUTXO.hpp"
#include "UTXOStructs.hpp"

#include "base/logger.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "storage/rocksdb/rocksdb.hpp"

#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

namespace sgns
{
    struct OutPointHash
    {
        size_t operator()( const OutPoint &outpoint ) const
        {
            size_t seed = std::hash<base::Hash256>{}( outpoint.txid_hash_ );
            boost::hash_combine( seed, outpoint.output_idx_ );
            return seed;
        }
    };

    class UTXOManager
    {
    public:
        enum class UTXOState : uint8_t
        {
            UTXO_READY,
            UTXO_CONSUMED
        };

        using UTXOData            = std::pair<UTXOState, GeniusUTXO>;
        struct UTXOEntry
        {
            UTXOState                     state{ UTXOState::UTXO_READY };
            GeniusUTXO                    utxo;
            uint64_t                      created_epoch{ 0 };
            std::optional<uint64_t>       spent_epoch;
            std::optional<base::Hash256>  spent_by_txid;
        };
        struct UTXOCheckpoint
        {
            std::string  owner_address;
            uint64_t     epoch{ 0 };
            base::Hash256 last_finalized_tx{};
            base::Hash256 registry_hash{};
            base::Hash256 utxo_merkle_root{};
            uint64_t     utxo_count{ 0 };
            uint64_t     created_at_ms{ 0 };
        };

        using UTXOOutPointMap     = std::unordered_map<OutPoint, UTXOEntry, OutPointHash>;
        using AddressOutPointList = std::unordered_map<std::string, std::vector<OutPoint>>;
        using SignFunc            = std::function<std::vector<uint8_t>( const std::vector<uint8_t> &data )>;
        using VerifySignatureFunc = std::function<bool( const std::string          &address,
                                                        const std::vector<uint8_t> &signature,
                                                        const std::vector<uint8_t> &data )>;

        UTXOManager( const bool          is_full_node,
                     std::string         address,
                     SignFunc            sign,
                     VerifySignatureFunc verify_signature ) :
            is_full_node_( is_full_node ),
            address_( std::move( address ) ),
            sign_( std::move( sign ) ),
            verify_signature_( std::move( verify_signature ) )
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
        outcome::result<bool> PutUTXO( GeniusUTXO new_utxo, const std::string &address );

        outcome::result<bool> PutUTXO( const GeniusUTXO &new_utxo )
        {
            return PutUTXO( new_utxo, address_ );
        }

        /**
         * @brief       Delete a UTXO from the account
         * @param[in]   utxo_id The ID of the UTXO to be deleted
         * @param[in]   output_idx The output index of the UTXO
         * @param       address Address to remove the UTXO from
         */
        outcome::result<void> DeleteUTXO( const base::Hash256 &utxo_id, uint32_t output_idx, const std::string &address );

        /**
         * @brief       Consume UTXOs from the account
         * @param[in]   infos Vector of UTXO information to be consumed
         * @param       address Address to consume UTXOs from
         * @return      true if all UTXOs were consumed, false otherwise
         */
        outcome::result<bool> ConsumeUTXOs( const std::vector<InputUTXOInfo> &infos, const std::string &address );

        outcome::result<bool> ConsumeUTXOs( const std::vector<InputUTXOInfo> &infos )
        {
            return ConsumeUTXOs( infos, address_ );
        }

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

        std::vector<GeniusUTXO> GetUTXOsForReservation( const std::string &address,
                                                        const std::string &reservation_id ) const;

        std::unordered_map<std::string, std::vector<UTXOData>> GetAllUTXOs() const;

        /**
         * @brief       Set UTXOs for a specific address (replaces existing UTXOs)
         * @param[in]   utxos Vector of UTXOs to set for the address
         * @param[in]   address The address to set UTXOs for
         */
        outcome::result<void> SetUTXOs( const std::vector<GeniusUTXO> &utxos, const std::string &address );

        outcome::result<void> SetUTXOs( const std::vector<GeniusUTXO> &utxos )
        {
            return SetUTXOs( utxos, address_ );
        }

        outcome::result<UTXOTxParameters> CreateTxParameter( uint64_t    amount,
                                                             std::string dest_address,
                                                             TokenID     token_id );

        outcome::result<UTXOTxParameters> CreateTxParameter( const std::vector<OutputDestInfo> &destinations,
                                                             const TokenID                     &token_id );

        void ReserveUTXOs( const std::vector<InputUTXOInfo> &inputs, const std::string &reservation_id );

        void RollbackUTXOs( const std::vector<InputUTXOInfo> &inputs, const std::string &reservation_id );

        bool VerifyParameters( const UTXOTxParameters &params ) const
        {
            return VerifyParameters( params, address_ );
        }

        bool VerifyParameters( const UTXOTxParameters &params, const std::string &address ) const;

        std::optional<UTXOState> GetOutPointState( const base::Hash256 &utxo_id, uint32_t output_idx ) const;

        bool IsOutPointConsumed( const base::Hash256 &utxo_id, uint32_t output_idx ) const;

        /**
         * @brief Compute a deterministic Merkle root for unspent UTXOs owned by this node address
         */
        [[nodiscard]] base::Hash256 ComputeUTXOMerkleRoot() const;

        /**
         * @brief Compute a deterministic Merkle root for unspent UTXOs from a specific address
         */
        [[nodiscard]] base::Hash256 ComputeUTXOMerkleRoot( const std::string &address ) const;

        /**
         * @brief Compute deterministic UTXO Merkle root from an explicit UTXO snapshot
         */
        [[nodiscard]] base::Hash256 ComputeUTXOMerkleRootFromSnapshot( const std::vector<GeniusUTXO> &utxos ) const;

        outcome::result<bool> LoadUTXOs( std::shared_ptr<storage::rocksdb> db );

        /**
         * @return True if loaded any UTXOs, false if loaded 0 UTXOs and error if one occurred
         */
        outcome::result<void> StoreUTXOs( const std::string &address );

        outcome::result<void> CreateCheckpoint( uint64_t            epoch,
                                                const base::Hash256 &last_finalized_tx,
                                                const base::Hash256 &registry_hash );

        outcome::result<void> CreateCheckpoint( const std::string   &address,
                                                uint64_t             epoch,
                                                const base::Hash256 &last_finalized_tx,
                                                const base::Hash256 &registry_hash );

        outcome::result<std::optional<UTXOCheckpoint>> LoadLatestCheckpoint() const
        {
            return LoadLatestCheckpoint( address_ );
        }

        outcome::result<std::optional<UTXOCheckpoint>> LoadLatestCheckpoint( const std::string &address ) const;

    private:
        static constexpr std::string_view DB_PREFIX = "/utxo";
        static constexpr std::string_view CHECKPOINT_PREFIX = "/utxo-checkpoint";

        outcome::result<std::pair<std::vector<InputUTXOInfo>, uint64_t>> SelectUTXOs( uint64_t       required_amount,
                                                                                      const TokenID &token_id );

        void SignInputs( std::vector<InputUTXOInfo> &inputs ) const;

        base::Logger logger_ = base::createLogger( "UTXOManager" );

        bool                              is_full_node_;
        std::string                       address_;
        SignFunc                          sign_;
        VerifySignatureFunc               verify_signature_;
        std::shared_ptr<storage::rocksdb> db_;

        mutable std::shared_mutex                  utxos_mutex_; ///< Mutex for UTXO state structures
        UTXOOutPointMap                                        utxo_outpoints_;
        AddressOutPointList                                    address_outpoints_;
        std::unordered_map<OutPoint, std::string, OutPointHash> reserved_outpoints_;
    };

}
