/**
 * @file       TransferProof.hpp
 * @brief      
 * @date       2024-09-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _TRANSFER_PROOF_HPP_
#define _TRANSFER_PROOF_HPP_

#include <string>
#include <cstdint>
#include <array>
#include <utility>
#include <boost/json.hpp>
#include <string_view>
#include "IBasicProof.hpp"
#include "circuits/TransactionVerifierCircuit.hpp"

namespace sgns
{

    class TransferProof : public IBasicProof
    {
    public:
        explicit TransferProof( uint64_t balance, uint64_t amount );

        ~TransferProof() = default;

        std::string GetProofType() const override
        {
            return std::string( TRANSFER_TYPE_NAME );
        }

        static boost::json::object GenerateIntParameter( uint64_t value );
        static boost::json::object GenerateArrayParameter( std::array<uint64_t, 4> values );
        static boost::json::object GenerateFieldParameter( uint64_t value );
        template <typename T>
        static boost::json::object GenerateCurveParameter( T value );

    private:
        static constexpr uint64_t                generator_X_point = 1;     // Example seed for TOTP
        static constexpr uint64_t                generator_Y_point = 2;     // Example seed for TOTP
        static constexpr uint64_t                base_seed         = 12345; // Example seed for TOTP
        static constexpr uint64_t                provided_totp     = 67890; // Example provided TOTP
        static constexpr std::array<uint64_t, 4> ranges            = { 1000, 2000, 3000, 4000 };
        uint64_t                                 balance_;
        uint64_t                                 amount_;

        outcome::result<std::vector<uint8_t>> SerializeFullProof(
            const SGProof::BaseProofData &base_proof_data ) override;
        std::pair<boost::json::array, boost::json::array>                         GenerateJsonParameters() override;
        static outcome::result<std::pair<boost::json::array, boost::json::array>> DeSerializePublicParams(
            const std::vector<uint8_t> &full_proof_data );

        static constexpr std::string_view TRANSFER_TYPE_NAME = "Transfer";

        static inline bool Register()
        {
            RegisterDeserializer( std::string( TRANSFER_TYPE_NAME ), &TransferProof::DeSerializePublicParams );
            RegisterBytecode( std::string( TRANSFER_TYPE_NAME ), std::string( TransactionCircuit ) );
            return true;
        }

        static inline bool registered = Register();
    };
}

#endif
