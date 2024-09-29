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
#include "IBasicProof.hpp"



namespace sgns
{

    class TransferProof : public IBasicProof
    {
    public:
        explicit TransferProof( std::string bytecode_path, uint64_t balance, uint64_t amount );

        ~TransferProof() = default;

        std::string GetProofType() const override
        {
            return "Transfer";
        }

        void GenerateProof();

    private:
        const std::string bytecode_path_;
        //const typename pallas::template g1_type<coordinates::affine>::value_type generator;
        //const typename pallas::scalar_field_type::value_type base_seed     = 12345; // Example seed for TOTP
        //const typename pallas::scalar_field_type::value_type provided_totp = 67890; // Example provided TOTP
        static constexpr uint64_t                generator_X_point = 1;     // Example seed for TOTP
        static constexpr uint64_t                generator_Y_point = 2;     // Example seed for TOTP
        static constexpr uint64_t                base_seed         = 12345; // Example seed for TOTP
        static constexpr uint64_t                provided_totp     = 67890; // Example provided TOTP
        static constexpr std::array<uint64_t, 4> ranges            = { 1000, 2000, 3000, 4000 };
        uint64_t                                 balance_;
        uint64_t                                 amount_;
        std::shared_ptr<void> assigner_;
        std::shared_ptr<void> prover_;
    };
}
#endif
