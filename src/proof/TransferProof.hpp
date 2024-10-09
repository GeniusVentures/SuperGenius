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

        //outcome::result<SGProof::ProofStruct> GenerateProof() override;

        //outcome::result<std::vector<uint8_t>> GenerateProof();

    private:
        static constexpr uint64_t                generator_X_point = 1;     // Example seed for TOTP
        static constexpr uint64_t                generator_Y_point = 2;     // Example seed for TOTP
        static constexpr uint64_t                base_seed         = 12345; // Example seed for TOTP
        static constexpr uint64_t                provided_totp     = 67890; // Example provided TOTP
        static constexpr std::array<uint64_t, 4> ranges            = { 1000, 2000, 3000, 4000 };
        uint64_t                                 balance_;
        uint64_t                                 amount_;


        std::pair<boost::json::array, boost::json::array> GenerateJsonParameters() override;
        boost::json::object                               GenerateIntParameter( uint64_t value );
        boost::json::object                               GenerateArrayParameter( std::array<uint64_t, 4> values );
        boost::json::object                               GenerateFieldParameter( uint64_t value );
        boost::json::object                               GenerateCurveParameter( uint64_t value );
    };
}


#endif
