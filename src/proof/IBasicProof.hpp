/**
 * @file       IBasicProof.hpp
 * @brief      
 * @date       2024-09-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _IBASIC_PROOF_HPP_
#define _IBASIC_PROOF_HPP_
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <utility>
#include <boost/json.hpp>
#include "proof/proto/SGProof.pb.h"
#include "outcome/outcome.hpp"

namespace sgns
{

    class IBasicProof
    {
    public:
        explicit IBasicProof( std::string bytecode_payload );

        virtual ~IBasicProof() = default;

        enum class Error
        {
            INSUFFICIENT_FUNDS = 0,
            INVALID_PROOF,
            BYTECODE_NOT_FOUND,
            INVALID_CIRCUIT,
        };

        virtual std::string GetProofType() const = 0;

        outcome::result<SGProof::ProofStruct> GenerateProof();

        static std::vector<uint8_t> SerializeProof( const SGProof::ProofStruct &proof );

    protected:
    private:
        const std::string     bytecode_payload_;
        std::shared_ptr<void> assigner_;
        std::shared_ptr<void> prover_;

        virtual std::pair<boost::json::array, boost::json::array> GenerateJsonParameters() = 0;
    };

}

OUTCOME_HPP_DECLARE_ERROR_2( sgns, IBasicProof::Error );

#endif
