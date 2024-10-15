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
            return "Transfer";
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

        outcome::result<std::vector<uint8_t>>             SerializePublicParameters() override;
        std::pair<boost::json::array, boost::json::array> GenerateJsonParameters() override;
        static outcome::result<std::pair<boost::json::array, boost::json::array>> DeSerializePublicParams(
            const std::vector<uint8_t> &full_proof_data );

        // Static function to register the class
        static inline bool Register()
        {
            RegisterDeserializer( "Transfer", &TransferProof::DeSerializePublicParams );
            RegisterBytecode( "Transfer", std::string( TransactionCircuit ) );
            return true;
        }

        // Static constructor for the derived class to insert into the map
        static inline bool registered =  Register();
    };

    // Initialize the static variable and trigger registration
    //bool TransferProof::registered = TransferProof::Register();
}

#endif
