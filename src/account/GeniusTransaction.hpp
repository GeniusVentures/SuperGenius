/**
 * @file       GeniusTransaction.hpp
 * @brief      Header file of the base GeniusTransaction class.
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_GENIUS_TRANSACTION_HPP
#define SGNS_GENIUS_TRANSACTION_HPP

#include <utility>
#include <vector>
#include <string>
#include <optional>

#include <boost/format.hpp>

#include "outcome/outcome.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "account/UTXOStructs.hpp"
#include "GeniusAccount.hpp"

#include <gsl/span>

namespace sgns
{
    using namespace boost::multiprecision;

    /**
     * @brief      Base class of the GeniusTransaction
     */
    class GeniusTransaction
    {
    public:
        /// The chain ID used for transactions that are not associated with a specific external chain.
        static constexpr std::string_view GENIUS_CHAIN_ID = "supergenius_chain";

        /**
         * @brief   Alias for the de-serializer method type to be implemented in derived classes
         */
        using TransactionDeserializeFn =
            std::function<std::shared_ptr<GeniusTransaction>( const std::vector<uint8_t> & )>;

        /**
         * @brief       Constructs a GeniusTransaction with the specified type and DAG metadata.
         * @param[in]   type The transaction type (e.g., "transfer", "mint", "escrow-hold").
         * @param[in]   dag The DAG metadata for the transaction.
         */
        GeniusTransaction( std::string type, SGTransaction::DAGStruct dag ) :
            dag_st( std::move( dag ) ), transaction_type( std::move( type ) )
        {
        }

        /**
         * @brief      Virtual destructor to avoid leakage when deleting derived classes through a base pointer.
         */
        virtual ~GeniusTransaction() = default;

        /**
         * @brief       Returns the transaction type.
         * @return      The transaction type string.
         */
        [[nodiscard]] std::string GetType() const
        {
            return transaction_type;
        }

        /**
         * @brief       Deserializes the DAG metadata from a byte vector.
         * @param[in]   data The byte vector containing the serialized DAG metadata.
         * @return      DAGStruct wrapped in an outcome::result, containing an error if deserialization fails.
         */
        static outcome::result<SGTransaction::DAGStruct> DeSerializeDAGStruct( const std::vector<uint8_t> &data );

        /**
         * @brief       Deserializes the DAG metadata from a string.
         * @param[in]   data The string containing the serialized DAG metadata.
         * @return      DAGStruct wrapped in an outcome::result, containing an error if deserialization fails.
         */
        static outcome::result<SGTransaction::DAGStruct> DeSerializeDAGStruct( const std::string &data );

        /**
         * @brief       Sets the transaction type in the DAG metadata and returns the modified DAGStruct.
         * @param[in]   dag The original DAGStruct to be modified.
         * @param[in]   type The transaction type to set in the DAG metadata.
         * @return      The modified DAGStruct with the transaction type set.
         */
        static SGTransaction::DAGStruct SetDAGWithType( SGTransaction::DAGStruct dag, const std::string &type )
        {
            dag.set_type( type );
            return dag;
        }

        /**
         * @brief       Serializes the transaction into a byte vector, including the DAG metadata.
         * @param[in]   dag The DAG metadata to be included in the serialization.
         * @return      The serialized byte vector representing the transaction.
         * @note        This should be defined in the derived class, but the version without parameters is to used
         */
        virtual std::vector<uint8_t> SerializeByteVector( const SGTransaction::DAGStruct &dag ) const = 0;

        /**
         * @brief       Serializes the transaction into an EmbeddedTransaction proto with the
         *              appropriate oneof field set.
         * @param[in]   dag The DAG metadata to be included in the serialization.
         * @return      EmbeddedTransaction proto with the typed transaction field set.
         */
        virtual EmbeddedTransaction SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const = 0;

        /**
         * @brief       Serializes using internal DAG metadata.
         */
        EmbeddedTransaction SerializeToEmbeddedTransaction() const
        {
            return SerializeToEmbeddedTransaction( dag_st );
        }

        /**
         * @brief       Serializes the transaction using the internal DAG metadata.
         * @return      The serialized byte vector representing the transaction.
         */
        std::vector<uint8_t> SerializeByteVector() const
        {
            return SerializeByteVector( dag_st );
        }

        /**
         * @brief       Returns if transaction supports UTXOs
         * @return      true if supported, false otherwise
         */
        virtual bool HasUTXOParameters() const
        {
            return false;
        }

        /**
         * @brief       Returns the UTXOs
         * @return      If exists, returns the UTXOs of the transaction
         */
        virtual std::optional<UTXOTxParameters> GetUTXOParametersOpt() const
        {
            return std::nullopt;
        }

        /**
         * @brief       Returns the source chain id for input validation routing
         * @return      The source chain id
         */
        virtual std::string GetChainId() const
        {
            return std::string( GENIUS_CHAIN_ID );
        }

        /**
         * @brief       Returns the transaction-specific path component for storage and retrieval.
         * @return      The transaction-specific path component, typically derived from the transaction type or other unique attributes.
         */
        virtual std::string GetTransactionSpecificPath() const = 0;

        /**
         * @brief       Returns the full storage path for the transaction based on its hash.
         * @param[in]   tx_hash Hash of the transaction to be included in the path.
         * @return      The full storage path for the transaction.
         */
        static std::string GetTransactionFullPath( const std::string &tx_hash )
        {
            return "tx/" + tx_hash;
        }

        /**
         * @brief       Returns the full storage path for the transaction based on its hash.
         * @return      The full storage path for the transaction.
         */
        std::string GetTransactionFullPath() const
        {
            return "tx/" + GetHash();
        }

        /**
         * @brief       Returns the full storage path for the proof based on the transaction hash.
         * @return      The full storage path for the proof.
         */
        std::string GetProofFullPath() const
        {
            return "proof/" + GetHash();
        }

        /**
         * @brief       Returns the source address for the transaction.
         * @return      The source address.
         */
        std::string GetSrcAddress() const
        {
            return dag_st.source_addr();
        }

        /**
         * @brief       Returns the hash of the transaction.
         * @return      The hash of the transaction.
         */
        [[nodiscard]] std::string GetHash() const;

        /**
         * @brief       Returns the hash of the previous transaction.
         * @return      The hash of the previous transaction.
         */
        [[nodiscard]] std::string GetPreviousHash() const;

        /**
         * @brief       Returns the hash of the uncle transaction.
         * @return      The hash of the uncle transaction.
         */
        [[nodiscard]] std::string GetUncleHash() const;

        /**
         * @brief       Returns the timestamp of the transaction.
         * @return      The timestamp of the transaction.
         */
        uint64_t GetTimestamp() const
        {
            return dag_st.timestamp();
        }

        /**
         * @brief       Returns the nonce of the transaction.
         * @return      The nonce of the transaction.
         */
        uint64_t GetNonce() const
        {
            return dag_st.nonce();
        }

        /**
         * @brief       Returns the destination topics associated with the transaction.
         * @return      The set of destination topics associated with the transaction.
         */
        virtual std::unordered_set<std::string> GetTopics() const;

        /**
         * @brief       Fills the data hash field in the DAG metadata based on the transaction content.
          * @note       This should be called after all transaction fields are set and before signing.
         */
        void FillHash();

        /**
         * @brief       Checks the integrity of the transaction by verifying that the hash field matches the calculated hash.
         * @return      true if the hash is valid, false otherwise.
         */
        bool CheckHash() const;

        /**
         * @brief       Creates a signature for the transaction using the provided GeniusAccount.
         * @param[in]   account The GeniusAccount used to sign the transaction.
         * @return      The signature as a byte vector.
         */
        std::vector<uint8_t> MakeSignature( GeniusAccount &account );

        /**
         * @brief       Verifies the transaction signature using the source address and the serialized transaction content.
         * @return      true if the signature is valid, false otherwise.
         */
        bool CheckSignature() const;

        /**
         * @brief       Legacy method to verify the transaction signature using the DAG metadata. This method may be used for backward compatibility with older transaction formats.
         * @return      true if the signature is valid and the hash matches, false otherwise.
         */
        bool CheckDAGSignatureLegacy() const;

        /**
         * @brief       Generates a slot ID used by consensus to identify competing transactions.
         * @return      The unique slot ID, typically derived from the source address
         */
        virtual std::string GetSlotID() const
        {
            return GetSrcAddress() + ":" + std::to_string( GetNonce() );
        }

        /// The DAG metadata struct that is included in all transactions, containing common fields such as source address, hashes, timestamp, nonce, and signature.
        SGTransaction::DAGStruct dag_st;

    private:
        /// Static map that holds registered deserializer functions for different transaction types, allowing dynamic deserialization based on the type field in the DAG metadata.
        static inline std::unordered_map<std::string, TransactionDeserializeFn> deserializers_map;
        /// The transaction type string that identifies the specific type of transaction (e.g., "transfer", "mint", "escrow-hold").
        const std::string transaction_type;

    public:
        /**
         * @brief       Registers a deserializer function for a specific transaction type.
         * @param[in]   transaction_type The transaction type for which the deserializer is registered.
         * @param[in]   fn The deserializer function to be registered.
         */
        static void RegisterDeserializer( const std::string &transaction_type, TransactionDeserializeFn fn )
        {
            deserializers_map[transaction_type] = std::move( fn );
        }

        /**
         * @brief       Returns the map of registered deserializer functions for transaction types.
         * @return      The map of transaction types to their corresponding deserializer functions.
         */
        static std::unordered_map<std::string, TransactionDeserializeFn> &GetDeSerializers()
        {
            return deserializers_map;
        }
    };
}

#endif // SGNS_GENIUS_TRANSACTION_HPP
