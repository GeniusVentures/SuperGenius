/**
 * @file       RegistrationTransaction.hpp
 * @brief      Transaction for child-wallet registration
 * @date       2026-07-15
 * @author     (Phase 4)
 */
#ifndef _REGISTRATION_TRANSACTION_HPP_
#define _REGISTRATION_TRANSACTION_HPP_

#include "account/GeniusTransaction.hpp"
#include "account/proto/SGTransaction.pb.h"

namespace sgns
{
    /**
     * @brief Transaction for child-wallet registration under a main wallet.
     *
     * Child-signed-only per D-04/D-05 — the main wallet does NOT counter-sign.
     * Carries DAG metadata (child's source_addr, nonce, signature), the main
     * wallet's public key, a monotonic registration sequence, and optional
     * game/publisher/dev metadata.
     */
    class RegistrationTransaction final : public GeniusTransaction
    {
    public:
        /**
         * @brief       Factory — constructs a RegistrationTransaction and fills its hash.
         * @param[in]   main_address Main wallet public address (128-hex, stored as bytes).
         * @param[in]   sequence     Monotonic per-child registration sequence number.
         * @param[in]   metadata     Optional game/publisher/dev metadata sub-message.
         * @param[in]   dag          DAG struct describing the transaction graph.
         * @return      A constructed RegistrationTransaction with computed data_hash.
         */
        static RegistrationTransaction New( std::string                    main_address,
                                            uint64_t                      sequence,
                                            SGTransaction::RegistrationMetadata metadata,
                                            SGTransaction::DAGStruct      dag,
                                            bool                          detach_flag = false,
                                            uint64_t                      supersedes_sequence = 0 );

        /**
         * @brief      Default destructor.
         */
        ~RegistrationTransaction() override = default;

        using GeniusTransaction::SerializeByteVector;
        std::vector<uint8_t> SerializeByteVector( const SGTransaction::DAGStruct &dag ) const override;

        using GeniusTransaction::SerializeToEmbeddedTransaction;
        EmbeddedTransaction SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const override;

        /**
         * @brief      Deserializes a RegistrationTransaction from bytes.
         * @param[in]  data Serialized bytes.
         * @return     Shared pointer to the deserialized transaction, or nullptr on parse failure.
         */
        static std::shared_ptr<RegistrationTransaction> DeSerializeByteVector( const std::vector<uint8_t> &data );

        /**
         * @brief      Returns the registered main wallet public key.
         * @return     The main_address_ string (128-hex).
         */
        const std::string &GetMainAddress() const
        {
            return main_address_;
        }

        /**
         * @brief      Returns the registration sequence number.
         * @return     The sequence_ value.
         */
        uint64_t GetSequence() const
        {
            return sequence_;
        }

        /**
         * @brief      Returns the registration metadata sub-message.
         * @return     The metadata_ proto.
         */
        const SGTransaction::RegistrationMetadata &GetMetadata() const
        {
            return metadata_;
        }

        /**
         * @brief      Returns whether this registration is a Detach (child clears main_address).
         * @return     The detach_flag_ value.
         */
        bool GetDetachFlag() const
        {
            return detach_flag_;
        }

        /**
         * @brief      Returns the sequence this lifecycle-change transaction supersedes.
         * @return     The supersedes_sequence_ value.
         */
        uint64_t GetSupersedesSequence() const
        {
            return supersedes_sequence_;
        }

        /**
         * @brief      Returns the transaction-specific path for storage, derived from GetType().
         * @return     The transaction type string ("registration").
         */
        std::string GetTransactionSpecificPath() const override
        {
            return GetType();
        }

        /**
         * @brief      Returns the set of pubsub topics associated with this transaction.
         * @return     A set containing the main_address_ for discovery.
         */
        std::unordered_set<std::string> GetTopics() const override;

    private:
        /**
         * @brief       Private constructor — use New() factory.
         * @param[in]   main_address Main wallet public address.
         * @param[in]   sequence     Registration sequence number.
         * @param[in]   metadata     Registration metadata sub-message.
         * @param[in]   dag          DAG struct with type set to "registration".
         */
        RegistrationTransaction( std::string                          main_address,
                                 uint64_t                             sequence,
                                 SGTransaction::RegistrationMetadata  metadata,
                                 SGTransaction::DAGStruct             dag,
                                 bool                                 detach_flag,
                                 uint64_t                             supersedes_sequence );

        std::string                         main_address_;  ///< 128-hex main wallet pubkey (stored as bytes in proto)
        uint64_t                            sequence_;      ///< Registration sequence
        SGTransaction::RegistrationMetadata metadata_;      ///< Optional metadata
        bool                                detach_flag_;         ///< True for Detach (main_address cleared)
        uint64_t                            supersedes_sequence_; ///< Sequence this lifecycle-change supersedes

        /**
         * @brief       Registers the deserializer for the "registration" transaction type.
         * @return      true (always succeeds).
         */
        static bool Register()
        {
            RegisterDeserializer( "registration", &RegistrationTransaction::DeSerializeByteVector );
            return true;
        }

        /**
         * @brief       Static variable to ensure registration on header inclusion.
         */
        static inline bool registered = Register();
    };

}

#endif
