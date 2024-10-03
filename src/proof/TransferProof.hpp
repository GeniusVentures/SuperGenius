/**
 * @file       TransferProof.hpp
 * @brief      
 * @date       2024-09-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _TRANSFER_PROOF_HPP_
#define _TRANSFER_PROOF_HPP_

#include <cstdint>
#include <cstdio>
#include <array>
#include <memory>
#include <boost/json.hpp>
#include "IBasicProof.hpp"
#include "outcome/outcome.hpp"

namespace sgns
{

    class TransferProof : public IBasicProof
    {
    public:
        explicit TransferProof( uint64_t balance, uint64_t amount );

        ~TransferProof() = default;

        enum class TxProofError
        {
            INSUFFICIENT_FUNDS = 0,
            INVALID_PROOF,
            BYTECODE_NOT_FOUND,
            INVALID_CIRCUIT,
        };

        std::string GetProofType() const override
        {
            return "Transfer";
        }

        outcome::result<std::vector<uint8_t>> GenerateProof();

    private:
        static constexpr uint64_t                generator_X_point = 1;     // Example seed for TOTP
        static constexpr uint64_t                generator_Y_point = 2;     // Example seed for TOTP
        static constexpr uint64_t                base_seed         = 12345; // Example seed for TOTP
        static constexpr uint64_t                provided_totp     = 67890; // Example provided TOTP
        static constexpr std::array<uint64_t, 4> ranges            = { 1000, 2000, 3000, 4000 };
        uint64_t                                 balance_;
        uint64_t                                 amount_;
        std::shared_ptr<void>                    assigner_;
        std::shared_ptr<void>                    prover_;

        std::pair<boost::json::array, boost::json::array> GenerateJsonParameters();
        boost::json::object                               GenerateIntParameter( uint64_t value );
        boost::json::object                               GenerateArrayParameter( std::array<uint64_t, 4> values );
        boost::json::object                               GenerateFieldParameter( uint64_t value );
        boost::json::object                               GenerateCurveParameter( uint64_t value );
    };
}

OUTCOME_HPP_DECLARE_ERROR_2( sgns, TransferProof::TxProofError );
#endif
