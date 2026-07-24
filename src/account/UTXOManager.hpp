/**
 * @file       UTXOManager.hpp
 * @brief      In-memory and persisted UTXO state manager with reservation and checkpoint helpers.
 * @date       2026-01-20
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_UTXO_MANAGER_HPP
#define SGNS_UTXO_MANAGER_HPP

#include "GeniusUTXO.hpp"
#include "UTXOStructs.hpp"

#include "base/logger.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "storage/rocksdb/rocksdb.hpp"

#include <optional>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

namespace sgns
{
    class TransactionManager;
    class TransactionManagerPendingLifecycleTestAccess;
    class UTXOManagerTestAccess;
    /**
     * @brief Hash functor for using OutPoint keys in unordered containers.
     */
    struct OutPointHash
    {
        /**
         * @brief       Computes a combined hash for a transaction id and output index pair.
         * @param[in]   outpoint The OutPoint to hash, containing a transaction id and output index.
         * @return      Size of the combined hash
         */
        size_t operator()( const OutPoint &outpoint ) const
        {
            size_t seed = std::hash<base::Hash256>{}( outpoint.txid_hash_ );
            boost::hash_combine( seed, outpoint.output_idx_ );
            return seed;
        }
    };

    /**
     * @brief Owns the local UTXO set, supports coin selection, validation, persistence,
     *        reservations, and deterministic snapshot hashing.
     */
    class UTXOManager
    {
    public:
        /**
         * @brief Lifecycle state stored for each tracked UTXO.
         */
        enum class UTXOState : uint8_t
        {
            UTXO_READY    = 0, ///< UTXO is unspent and available for use
            UTXO_CONSUMED = 1, ///< UTXO has been consumed by a transaction and is no longer available
            UTXO_RESERVED = 2  ///< Burn UTXO with mint in consensus — blocks local reuse but allows voting
        };

        /**
         * @brief Type classification for UTXOs to distinguish standard UTXOs from cross-chain bridge burns.
         */
        enum class UTXOType : uint8_t
        {
            UTXO_NORMAL = 0, ///< Standard UTXO from local transfers or mints
            UTXO_BRIDGE = 1  ///< UTXO from cross-chain bridge burn event
        };

        /**
         * @brief      UTXO state paired with the actual UTXO
         */
        using UTXOData = std::pair<UTXOState, GeniusUTXO>;

        /**
         * @brief Metadata tracked for each outpoint in the local registry.
         */
        struct UTXOEntry
        {
            UTXOState                    state{ UTXOState::UTXO_READY }; ///< Current lifecycle state of the UTXO
            GeniusUTXO                   utxo;                           ///< The actual UTXO data
            uint64_t                     created_epoch{ 0 };             ///< Epoch when the UTXO was created
            std::optional<uint64_t>      spent_epoch;   ///< Epoch when the UTXO was consumed, if applicable
            std::optional<base::Hash256> spent_by_txid; ///< Transaction ID that consumed this UTXO, if applicable
            UTXOType                     type{ UTXOType::UTXO_NORMAL }; ///< Type classification for the UTXO
        };

        /**
         * @brief Persisted checkpoint snapshot used to audit finalized UTXO state at a given epoch.
         */
        struct UTXOCheckpoint
        {
            std::string owner_address; ///< Owner address associated with this checkpoint
            uint64_t    epoch{ 0 };    ///< Epoch number when the checkpoint was created
            base::Hash256
                last_finalized_tx{}; ///< Transaction ID of the last finalized transaction at the time of checkpointing
            base::Hash256 registry_hash{};    ///< Hash of the full UTXO registry state at the time of checkpointing.
            base::Hash256 utxo_merkle_root{}; ///< Merkle root of the unspent UTXOs at the time of checkpointing.
            uint64_t      utxo_count{ 0 };    ///< Total number of UTXOs included in the checkpoint
            uint64_t      created_at_ms{ 0 }; ///< Timestamp in milliseconds when the checkpoint was created
        };

        /// @brief Maps an outpoint to its UTXO Entry
        using UTXOOutPointMap = std::unordered_map<OutPoint, UTXOEntry, OutPointHash>;
        /// @brief Maps an owner address to a list of outpoints they own
        using AddressOutPointList = std::unordered_map<std::string, std::vector<OutPoint>>;
        /// @brief Method to sign a vector of bytes, returning the signature bytes
        using SignFunc = std::function<std::vector<uint8_t>( const std::vector<uint8_t> &data )>;
        /// @brief Method to verify a signature given an address, signature bytes, and original data
        using VerifySignatureFunc = std::function<bool( const std::string          &address,
                                                        const std::vector<uint8_t> &signature,
                                                        const std::vector<uint8_t> &data )>;

        /**
         * @brief       Construct a new UTXOManager object
         * @param[in]   address The address of the node
         * @param[in]   sign The signer method
         * @param[in]   verify_signature The verifier method
         */
        UTXOManager( std::string address, SignFunc sign, VerifySignatureFunc verify_signature ) :
            address_( std::move( address ) ),
            sign_( std::move( sign ) ),
            verify_signature_( std::move( verify_signature ) )
        {
            ResetFaultCallback();
            ResetBridgeApplicationReader();
        }

        /**
         * @brief       Get the account's balance
         * @return      The total balance of the account
         */
        [[nodiscard]] uint64_t GetBalance() const;

        /**
         * @brief       Get the informed address balance
         * @param[in]   address The address to get the balance for
         * @return      The total balance of the account
         */
        [[nodiscard]] uint64_t GetBalance( const std::string &address ) const;

        /**
         * @brief       Get the accounts balance for a specific token
         * @param[in]   token_id Token ID to get the balance
         * @return      The balance of the account for the specific token
         */
        uint64_t GetBalance( const TokenID &token_id ) const;

        /**
         * @brief       Get the balance of the informed address for a specific token
         * @param[in]   token_id The token ID to get the balance for
         * @param[in]   address The address to get the balance for
         * @return      The balance of the account for the specific token and address
         */
        uint64_t GetBalance( const TokenID &token_id, const std::string &address ) const;

        /**
         * @brief       Add a new UTXO to the account
         * @param[in]   new_utxo The new UTXO to be added
         * @param       address Address to add the UTXO to
         * @param[in]   type UTXO type classification (default UTXO_NORMAL for standard UTXOs)
         * @return      true if the UTXO was added, false otherwise
         */
        outcome::result<bool> PutUTXO( GeniusUTXO         new_utxo,
                                       const std::string &address,
                                       UTXOType           type = UTXOType::UTXO_NORMAL );

        /**
         * @brief       Adds a new UTXO to the account using the manager's default address.
         * @param[in]   new_utxo The UTXO to be added
         * @return      true if added successfully, false otherwise
         */
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
        outcome::result<void> DeleteUTXO( const base::Hash256 &utxo_id,
                                          uint32_t             output_idx,
                                          const std::string   &address );

        /**
         * @brief       Consume UTXOs from the account
         * @param[in]   infos Vector of UTXO information to be consumed
         * @param       address Address to consume UTXOs from
         * @param[in]   type Only consume UTXOs of this type (default NORMAL)
         * @return      true if all UTXOs were consumed, false otherwise
         */
        outcome::result<bool> ConsumeUTXOs( const std::vector<InputUTXOInfo> &infos,
                                            const std::string                &address,
                                            UTXOType                          type = UTXOType::UTXO_NORMAL );

        /**
         * @brief Restores previously consumed inputs when their consuming transaction is reverted.
         * @param[in] infos Inputs whose outpoints should become spendable again.
         * @param[in] address Expected owner of the consumed inputs.
         * @param[in] type Expected UTXO type.
         * @return Success when every input was consumed by this owner and was restored.
         */
        outcome::result<void> RestoreConsumedUTXOs( const std::vector<InputUTXOInfo> &infos,
                                                    const std::string                &address,
                                                    UTXOType                          type = UTXOType::UTXO_NORMAL );

        /**
         * @brief       Consume UTXOs from the default owner address tracked by this manager.
         * @param[in]   infos Vector of UTXO information to be consumed
         * @param[in]   type Only consume UTXOs of this type (default NORMAL)
         * @return      true if all UTXOs were consumed, false otherwise
         */
        outcome::result<bool> ConsumeUTXOs( const std::vector<InputUTXOInfo> &infos,
                                            UTXOType                          type = UTXOType::UTXO_NORMAL )
        {
            return ConsumeUTXOs( infos, address_, type );
        }

        /**
         * @brief       Get UTXOs for a specific address
         * @param[in]   address The address to get UTXOs for
         * @return      Vector of UTXOs for the address
         */
        std::vector<GeniusUTXO> GetUTXOs( const std::string &address ) const;

        /**
         * @brief       Returns spendable UTXOs owned by the manager's default address.
         * @return      The vector of UTXOs for the manager's default address
         */
        std::vector<GeniusUTXO> GetUTXOs() const
        {
            return GetUTXOs( address_ );
        }

        /**
         * @brief       Get all unconsumed UTXOs for a specific address.
         * @param[in]   address The address to get UTXOs for
         * @return      Ready and locally reserved UTXOs for the address
         */
        std::vector<GeniusUTXO> GetUnconsumedUTXOs( const std::string &address ) const;

        /**
         * @brief Returns an unconsumed UTXO by its exact outpoint.
         * @param[in] txid Transaction hash that created the UTXO
         * @param[in] output_idx Output index within the transaction
         * @return The UTXO when present and not consumed, otherwise std::nullopt
         */
        std::optional<GeniusUTXO> GetUnconsumedUTXO( const base::Hash256 &txid, uint32_t output_idx ) const;

        /**
         * @brief       Get all UTXOs tracked by the manager, grouped by owner address
         * @return      A map of owner addresses to their corresponding vectors of UTXOs
         */
        std::unordered_map<std::string, std::vector<UTXOData>> GetAllUTXOs() const;

        /**
         * @brief       Set UTXOs for a specific address (replaces existing UTXOs)
         * @param[in]   utxos Vector of UTXOs to set for the address
         * @param[in]   address The address to set UTXOs for
         */
        outcome::result<void> SetUTXOs( const std::vector<GeniusUTXO> &utxos, const std::string &address );

        /**
         * @brief       Set UTXOs for the manager's default address (replaces existing UTXOs)
         * @param[in]   utxos Vector of UTXOs to set for the manager's default address
         */
        outcome::result<void> SetUTXOs( const std::vector<GeniusUTXO> &utxos )
        {
            return SetUTXOs( utxos, address_ );
        }

        /**
         * @brief       Create the input and output parameters for a single-output transfer, selecting from available UTXOs.
         * @param[in]   amount The amount to transfer
         * @param[in]   dest_address The destination address for the transfer
         * @param[in]   token_id The token ID to transfer
         * @return      The combined input and output parameters for the transaction if successful, or an error if selection or signing failed
         */
        outcome::result<UTXOTxParameters> CreateTxParameter( uint64_t    amount,
                                                             std::string dest_address,
                                                             TokenID     token_id );

        /**
         * @brief       Selects and signs inputs for a multi-output transfer.
         * @param[in]   destinations The list of destination addresses and amounts for the transfer
         * @param[in]   token_id The token ID to transfer
         * @return      The combined input and output parameters for the transaction if successful, or an error if selection or signing failed
         */
        outcome::result<UTXOTxParameters> CreateTxParameter( const std::vector<OutputDestInfo> &destinations,
                                                             const TokenID                     &token_id );

        /**
         * @brief       Marks inputs as reserved so they are not reused by concurrent transaction assembly.
         * @param[in]   inputs The list of UTXOs to reserve
         * @param[in]   reservation_id The ID for the reservation
         * @param[in]   type Only reserve UTXOs of this type (default NORMAL)
         */
        void ReserveUTXOs( const std::vector<InputUTXOInfo> &inputs,
                           const std::string                &reservation_id,
                           UTXOType                          type = UTXOType::UTXO_NORMAL );

        /**
         * @brief       Releases a previous reservation without consuming the inputs.
         * @param[in]   inputs The list of UTXOs to release
         * @param[in]   reservation_id The ID for the reservation
         * @param[in]   type Only rollback UTXOs of this type (default NORMAL)
         */
        void RollbackUTXOs( const std::vector<InputUTXOInfo> &inputs,
                            const std::string                &reservation_id,
                            UTXOType                          type = UTXOType::UTXO_NORMAL );

        /**
         * @brief       Verifies ownership and signatures for UTXO transaction parameters using the default address.
         * @param[in]   params The transaction parameters to verify, including inputs and outputs
         * @return      true if all signatures are valid and correspond to the default address, false otherwise
         */
        bool VerifyParameters( const UTXOTxParameters &params ) const
        {
            return VerifyParameters( params, address_ );
        }

        /**
         * @brief       Verifies ownership and signatures for UTXO transaction parameters using an explicit address.
         * @param[in]   params The transaction parameters to verify, including inputs and outputs
         * @param[in]   address The address to verify ownership against
         * @return      true if all signatures are valid and correspond to the specified address, false otherwise
         */
        bool VerifyParameters( const UTXOTxParameters &params, const std::string &address ) const;

        /**
         * @brief       Returns the tracked state of a specific outpoint when present.
         * @param[in]   utxo_id The transaction hash that created the UTXO
         * @param[in]   output_idx The output index of the UTXO within the transaction
         * @return      If the outpoint is tracked, returns its current state (e.g. ready or consumed); if not tracked, returns std::nullopt
         */
        std::optional<UTXOState> GetOutPointState( const base::Hash256 &utxo_id, uint32_t output_idx ) const;

        /**
         * @brief       Indicates whether a specific outpoint has already been consumed.
         * @param[in]   utxo_id The transaction hash that created the UTXO
         * @param[in]   output_idx The output index of the UTXO within the transaction
         * @return      true if the outpoint is consumed, false otherwise
         */
        bool IsOutPointConsumed( const base::Hash256 &utxo_id, uint32_t output_idx ) const;

        /**
         * @brief       Indicates whether a specific outpoint is in the RESERVED state (burn UTXO awaiting consensus).
         * @param[in]   utxo_id The transaction hash that created the UTXO
         * @param[in]   output_idx The output index of the UTXO within the transaction
         * @return      true if the outpoint exists and is in UTXO_RESERVED state
         */
        bool IsOutPointReserved( const base::Hash256 &utxo_id, uint32_t output_idx ) const;

        /**
         * @brief       Compute a deterministic Merkle root for unspent UTXOs owned by this node address
         * @return      The computed UTXO Merkle root for this node address
         */
        [[nodiscard]] base::Hash256 ComputeUTXOMerkleRoot() const;

        /**
         * @brief       Compute a deterministic Merkle root for unspent UTXOs from a specific address
         * @param[in]   address The address to compute the UTXO Merkle root for
         * @return      The computed UTXO Merkle root for the specified address
         */
        [[nodiscard]] base::Hash256 ComputeUTXOMerkleRoot( const std::string &address ) const;

        /**
         * @brief       Compute deterministic UTXO Merkle root from an explicit UTXO snapshot
         * @param[in]   utxos The list of UTXOs to include in the Merkle root computation
         * @return      The computed UTXO Merkle root
         */
        [[nodiscard]] base::Hash256 ComputeUTXOMerkleRootFromSnapshot( const std::vector<GeniusUTXO> &utxos ) const;

        /**
         * @brief       Loads the UTXO state for the manager's default address from persistent storage.
         * @param[in]   db The RocksDB instance to load from
         * @return      true if loaded successfully, false if no UTXOs were found, or an error if loading failed
         */
        outcome::result<bool> LoadUTXOs( std::shared_ptr<storage::rocksdb> db );

        /**
         * @brief       Releases the current RocksDB handle used for persistence.
         */
        void ReleaseStorage();

        /**
         * @brief       Stores the current UTXO state for the manager's default address to persistent storage.
         * @param[in]   address The address to store UTXOs for
         */
        outcome::result<void> StoreUTXOs( const std::string &address );

        /// Builds the sole durable key for one canonical bridge burn application.
        static std::string MakeBridgeApplicationKey( const std::string &chain_id,
                                                     const base::Hash256 &burn_hash,
                                                     uint32_t receipt_log_index );

        /**
         * @brief       Creates a checkpoint for the manager's default address.
         * @param[in]   epoch The epoch number associated with the checkpoint
         * @param[in]   last_finalized_tx The transaction ID of the last finalized transaction at the time of checkpointing
         * @param[in]   registry_hash The hash of the full registry state at the time of checkpointing

         */
        outcome::result<void> CreateCheckpoint( uint64_t             epoch,
                                                const base::Hash256 &last_finalized_tx,
                                                const base::Hash256 &registry_hash );

        /**
         * @brief       Creates a checkpoint for an explicit owner address.
         * @param[in]   address The address for which to create a checkpoint
         * @param[in]   epoch The epoch number associated with the checkpoint
         * @param[in]   last_finalized_tx The transaction ID of the last finalized transaction at the time of checkpointing
         * @param[in]   registry_hash The hash of the full registry state at the time of checkpointing
         */
        outcome::result<void> CreateCheckpoint( const std::string   &address,
                                                uint64_t             epoch,
                                                const base::Hash256 &last_finalized_tx,
                                                const base::Hash256 &registry_hash );

        /**
         * @brief       Loads the latest checkpoint for the default owner address.
         * @return      If successful, returns the latest checkpoint for the manager's default address; if no checkpoint is found, returns std::nullopt; if an error occurs during loading, returns the error
         */
        outcome::result<std::optional<UTXOCheckpoint>> LoadLatestCheckpoint() const
        {
            return LoadLatestCheckpoint( address_ );
        }

        /**
         * @brief       Loads the latest checkpoint for the provided owner address.
         * @param[in]   address The owner address to load the checkpoint for
         * @return      If successful, returns the latest checkpoint for the manager's default address; if no checkpoint is found, returns std::nullopt; if an error occurs during loading, returns the error
         */
        outcome::result<std::optional<UTXOCheckpoint>> LoadLatestCheckpoint( const std::string &address ) const;

    private:
        friend class TransactionManager;
        friend class TransactionManagerPendingLifecycleTestAccess;
        friend class UTXOManagerTestAccess;

        enum class AtomicMintEffectResult : uint8_t
        {
            Applied,
            AlreadyApplied
        };

        struct AtomicMintEffectRequest
        {
            base::Hash256               winning_transaction_hash;
            std::string                 chain_id;
            base::Hash256               burn_hash;
            uint32_t                    receipt_log_index{ 0 };
            std::vector<GeniusUTXO>     produced_outputs;
            InputUTXOInfo               bridge_input;
            std::string                 bridge_input_owner;
            UTXOType                    bridge_input_type{ UTXOType::UTXO_BRIDGE };
        };

        struct BridgeApplication
        {
            base::Hash256           winning_transaction_hash;
            std::string             chain_id;
            base::Hash256           burn_hash;
            uint32_t                receipt_log_index{ 0 };
            std::string             bridge_input_owner;
            UTXOType                bridge_input_type{ UTXOType::UTXO_BRIDGE };
            std::vector<GeniusUTXO> produced_outputs;
            std::vector<uint8_t>    canonical_bytes;
        };

        enum class FaultStage : uint8_t
        {
            ProducedOutputStage,
            BridgeInputStage,
            AtomicMintWaitingForPersistenceGate,
            AtomicMintPersistenceGateAcquired,
            AtomicMintBeforeBatchCommit,
            OrdinaryStoreWaitingForPersistenceGate,
            OrdinaryStorePersistenceGateAcquired,
            OrdinaryStoreSnapshotReadyBeforeCommit
        };

        using FaultCallback = std::function<outcome::result<void>( FaultStage )>;
        using BridgeApplicationReader =
            std::function<outcome::result<base::Buffer>(
                const std::shared_ptr<storage::rocksdb> &, const base::Buffer & )>;

        /// Prefix for UTXO-related keys in RocksDB
        static constexpr std::string_view DB_PREFIX = "/utxo";
        ///< Prefix for UTXO checkpoint keys in RocksDB
        static constexpr std::string_view CHECKPOINT_PREFIX = "/utxo-checkpoint";
        static constexpr std::string_view BRIDGE_APPLICATION_PREFIX = "/bridge/application/v1/";

        outcome::result<AtomicMintEffectResult> ApplyMintEffectsAtomically(
            const AtomicMintEffectRequest &request );
        outcome::result<std::optional<BridgeApplication>> GetBridgeApplication(
            const std::string &chain_id,
            const base::Hash256 &burn_hash,
            uint32_t receipt_log_index ) const;
        outcome::result<void> InvokeFault( FaultStage stage ) const;
        void ResetFaultCallback();
        void ResetBridgeApplicationReader();

        /**
         * @brief       Grabs the current storage as a shared pointer copy
         * @return      The database handle as a shared pointer
         */
        std::shared_ptr<storage::rocksdb> AcquireStorage() const;

        /**
         * @brief       Selects UTXOs to cover a required amount for a specific token, excluding reserved outpoints, and returns the selected inputs along with the total selected amount.
         * @param[in]   required_amount The total amount that needs to be covered by the selected UTXOs
         * @param[in]   token_id The token ID that the selected UTXOs must match
         * @return      If successful, returns a pair containing the vector of selected UTXO input information and the total amount covered by those inputs; if selection fails (e.g., insufficient funds), returns an error
         */
        outcome::result<std::pair<std::vector<InputUTXOInfo>, uint64_t>> SelectUTXOs( uint64_t       required_amount,
                                                                                      const TokenID &token_id );

        /**
         * @brief       Signs the provided UTXO inputs using the configured signing function.
         * @param[in]   inputs The vector of UTXO input information to sign
         */
        void SignInputs( std::vector<InputUTXOInfo> &inputs ) const;

        /// Logger instance for UTXOManager
        base::Logger logger_ = base::createLogger( "UTXOManager" );

        std::string         address_;          ///< Address of the account this manager is responsible for
        SignFunc            sign_;             ///< Signer method for authorizing UTXO spends
        VerifySignatureFunc verify_signature_; ///< Verifier method for validating signatures on UTXO spends
        std::shared_ptr<storage::rocksdb> db_; ///< Database handle for persisting UTXO state and checkpoints

        /// Serializes every persistent snapshot. Lock order is persistence_mutex_ then utxos_mutex_.
        mutable std::mutex persistence_mutex_;
        mutable std::shared_mutex utxos_mutex_;       ///< Mutex for UTXO state structures
        UTXOOutPointMap           utxo_outpoints_;    ///< Maps outpoints to their UTXO entries for efficient lookup
        AddressOutPointList       address_outpoints_; ///< Maps owner addresses to their outpoints for efficient lookup
        /// Transient local ownership for reservations; never persisted or used for consensus validity.
        std::unordered_map<OutPoint, std::string, OutPointHash> local_reservations_;
        FaultCallback fault_callback_;
        BridgeApplicationReader bridge_application_reader_;
    };

}

#endif // SGNS_UTXO_MANAGER_HPP
