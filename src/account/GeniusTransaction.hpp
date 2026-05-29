/**
 * @file       GeniusTransaction.hpp
 * @brief      Transaction interface class
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once

#include <utility>
#include <vector>
#include <string>
#include <optional>

#include <boost/format.hpp>

#include "outcome/outcome.hpp"
#include "account/proto/SGTransaction.pb.h"
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
        static constexpr std::string_view GENIUS_CHAIN_ID = "supergenius_chain";

        /**
         * @brief   Alias for the de-serializer method type to be implemented in derived classes
         */
        using TransactionDeserializeFn =
            std::function<std::shared_ptr<GeniusTransaction>( const std::vector<uint8_t> & )>;

        GeniusTransaction( std::string type, SGTransaction::DAGStruct dag ) :
            dag_st( std::move( dag ) ), transaction_type( std::move( type ) )
        {
        }

        virtual ~GeniusTransaction() = default;

        [[nodiscard]] std::string GetType() const
        {
            return transaction_type;
        }

        static outcome::result<SGTransaction::DAGStruct> DeSerializeDAGStruct( const std::vector<uint8_t> &data );
        static outcome::result<SGTransaction::DAGStruct> DeSerializeDAGStruct( const std::string &data );

        static SGTransaction::DAGStruct SetDAGWithType( SGTransaction::DAGStruct dag, const std::string &type )
        {
            dag.set_type( type );
            return dag;
        }

        virtual std::vector<uint8_t> SerializeByteVector( const SGTransaction::DAGStruct &dag ) const = 0;

        std::vector<uint8_t> SerializeByteVector() const
        {
            return SerializeByteVector( dag_st );
        }

        /**
         * @brief       Returns if transaction supports UTXOs
         * @return      True if supported, false otherwise
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

        virtual std::string GetTransactionSpecificPath() const = 0;

        static std::string GetTransactionFullPath( const std::string &tx_hash )
        {
            return "tx/" + tx_hash;
        }

        std::string GetTransactionFullPath() const
        {
            return "tx/" + GetHash();
        }

        std::string GetProofFullPath() const
        {
            return "proof/" + GetHash();
        }

        std::string GetSrcAddress() const
        {
            return dag_st.source_addr();
        }

        [[nodiscard]] std::string GetHash() const;
        [[nodiscard]] std::string GetPreviousHash() const;
        [[nodiscard]] std::string GetUncleHash() const;

        uint64_t GetTimestamp() const
        {
            return dag_st.timestamp();
        }

        uint64_t GetNonce() const
        {
            return dag_st.nonce();
        }

        virtual std::unordered_set<std::string> GetTopics() const;

        void FillHash();
        bool CheckHash() const;

        std::vector<uint8_t> MakeSignature( GeniusAccount &account );
        bool                 CheckSignature() const;
        bool                 CheckDAGSignatureLegacy() const;

        virtual std::string GetSlotID() const
        {
            return GetSrcAddress() + ":" + std::to_string( GetNonce() );
        }

        SGTransaction::DAGStruct dag_st;

    private:
        static inline std::unordered_map<std::string, TransactionDeserializeFn> deserializers_map;
        const std::string                                                       transaction_type;

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

        static std::unordered_map<std::string, TransactionDeserializeFn> &GetDeSerializers()
        {
            return deserializers_map;
        }
    };
}
