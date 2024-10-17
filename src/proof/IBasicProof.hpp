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
        using PublicParamDeserializeFn =
            std::function<outcome::result<std::pair<boost::json::array, boost::json::array>>(
                const std::vector<uint8_t> & )>;

        explicit IBasicProof( std::string bytecode_payload );

        virtual ~IBasicProof() = default;

        enum class Error
        {
            INSUFFICIENT_FUNDS = 0,
            INVALID_PROOF,
            BYTECODE_NOT_FOUND,
            INVALID_CIRCUIT,
            INVALID_PROTO_PROOF,
            INVALID_PROOF_TYPE,
            UNEXPECTED_PROOF_TYPE,
            INVALID_PUBLIC_PARAMETERS
        };

        virtual std::string GetProofType() const = 0;

        outcome::result<std::vector<uint8_t>> GenerateFullProof();

        static outcome::result<bool> VerifyFullProof( const std::vector<uint8_t> &full_proof_data );

    protected:
        static inline std::map<std::string, PublicParamDeserializeFn> PublicParamDeSerializers;
        static inline std::map<std::string, std::string>              ByteCodeMap;

        static void RegisterDeserializer( const std::string &proof_type, PublicParamDeserializeFn fn )
        {
            PublicParamDeSerializers[proof_type] = fn;
        }

        static void RegisterBytecode( const std::string &proof_type, std::string bytecode )
        {
            ByteCodeMap[proof_type] = std::move( bytecode );
        }

    private:
        const std::string bytecode_payload_;

        static outcome::result<SGProof::BaseProofProto> DeSerializeBaseProof( const std::vector<uint8_t> &proof_data );

        outcome::result<SGProof::BaseProofData>         GenerateProof();
        virtual outcome::result<std::vector<uint8_t>>   SerializeFullProof(
              const SGProof::BaseProofData &base_proof_data ) = 0;

        virtual std::pair<boost::json::array, boost::json::array> GenerateJsonParameters() = 0;
    };

}

OUTCOME_HPP_DECLARE_ERROR_2( sgns, IBasicProof::Error );

#endif
