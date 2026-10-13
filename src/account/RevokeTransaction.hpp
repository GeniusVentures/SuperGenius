/**
 * @file       RevokeTransaction.hpp
 * @brief      Transaction for main-initiated revocation of a registered child wallet.
 * @date       2026-07-21
 * @author     (Phase 5)
 */
#ifndef _REVOKE_TRANSACTION_HPP_
#define _REVOKE_TRANSACTION_HPP_

#include "account/GeniusTransaction.hpp"
#include "account/proto/SGTransaction.pb.h"

namespace sgns
{
    /**
     * @brief Transaction for main-initiated revocation of a registered child wallet.
     *
     * Main-signed — the main wallet is the transaction's own source/signer, targeting
     * a child's `reg/` record via `child_address_`. Validated by an extended
     * CheckParentChildAuthority gate (main signature + reg/{child}.main_address == signer
     * + Registered state), per D-36.
     */
    class RevokeTransaction final : public GeniusTransaction
    {
    public:
        /**
         * @brief       Factory — constructs a RevokeTransaction and fills its hash.
         * @param[in]   child_address        Target child wallet address being revoked.
         * @param[in]   registration_sequence Sequence of the reg/ record being revoked.
         * @param[in]   dag                  DAG struct describing the transaction graph.
         * @return      A constructed RevokeTransaction with computed data_hash.
         */
        static RevokeTransaction New( std::string               child_address,
                                      uint64_t                  registration_sequence,
                                      SGTransaction::DAGStruct  dag );

        /**
         * @brief      Default destructor.
         */
        ~RevokeTransaction() override = default;

        using GeniusTransaction::SerializeByteVector;
        std::vector<uint8_t> SerializeByteVector( const SGTransaction::DAGStruct &dag ) const override;

        using GeniusTransaction::SerializeToEmbeddedTransaction;
        EmbeddedTransaction SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const override;

        /**
         * @brief      Deserializes a RevokeTransaction from bytes.
         * @param[in]  data Serialized bytes.
         * @return     Shared pointer to the deserialized transaction, or nullptr on parse failure.
         */
        static std::shared_ptr<RevokeTransaction> DeSerializeByteVector( const std::vector<uint8_t> &data );

        /**
         * @brief      Returns the target child wallet address.
         * @return     The child_address_ string.
         */
        const std::string &GetChildAddress() const
        {
            return child_address_;
        }

        /**
         * @brief      Returns the registration sequence being revoked.
         * @return     The registration_sequence_ value.
         */
        uint64_t GetRegistrationSequence() const
        {
            return registration_sequence_;
        }

        /**
         * @brief      Returns the transaction-specific path for storage, derived from GetType().
         * @return     The transaction type string ("revoke").
         */
        std::string GetTransactionSpecificPath() const override
        {
            return GetType();
        }

        /**
         * @brief      Returns the set of pubsub topics associated with this transaction.
         * @return     A set containing the child_address_ so the revoke is discoverable/routable
         *             to the target child, mirroring RegistrationTransaction::GetTopics().
         */
        std::unordered_set<std::string> GetTopics() const override;

    private:
        /**
         * @brief       Private constructor — use New() factory.
         * @param[in]   child_address        Target child wallet address.
         * @param[in]   registration_sequence Registration sequence being revoked.
         * @param[in]   dag                  DAG struct with type set to "revoke".
         */
        RevokeTransaction( std::string               child_address,
                           uint64_t                  registration_sequence,
                           SGTransaction::DAGStruct  dag );

        std::string child_address_;         ///< Target child wallet address
        uint64_t    registration_sequence_; ///< Sequence of the reg/ record being revoked

        /**
         * @brief       Registers the deserializer for the "revoke" transaction type.
         * @return      true (always succeeds).
         */
        static bool Register()
        {
            RegisterDeserializer( "revoke", &RevokeTransaction::DeSerializeByteVector );
            return true;
        }

        /**
         * @brief       Static variable to ensure registration on header inclusion.
         */
        static inline bool registered = Register();
    };

}

#endif
