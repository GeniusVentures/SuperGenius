/**
 * @file       IBasicProof.hpp
 * @brief      
 * @date       2024-09-29
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _IBASIC_PROOF_HPP_
#define _IBASIC_PROOF_HPP_
#include <string>

class IBasicProof
{
public:
    explicit IBasicProof( std::string bytecode_payload ) : bytecode_payload_( std::move( bytecode_payload ) )
    {
    }

    virtual ~IBasicProof() = default;

    virtual std::string GetProofType() const = 0;

protected:
    const std::string bytecode_payload_;

private:
};

#endif
