#include "trustedpeer/QuorumPolicy.hpp"

namespace sgns::trustedpeer
{
    std::optional<QuorumPolicyState> QuorumPolicyState::Canonicalized() const
    {
        return std::nullopt;
    }

    std::optional<std::vector<uint8_t>> QuorumPolicyState::CanonicalBytes() const
    {
        return std::nullopt;
    }

    std::optional<std::string> QuorumPolicyState::Hash() const
    {
        return std::nullopt;
    }

    std::optional<QuorumPolicyState> QuorumPolicyState::DecodeCanonical( const std::vector<uint8_t> & )
    {
        return std::nullopt;
    }

    bool QuorumPolicyState::operator==( const QuorumPolicyState & ) const
    {
        return false;
    }

    bool ValidateQuorumPolicy( const QuorumPolicyState & )
    {
        return false;
    }

    bool ValidatePolicySuccessor( const QuorumPolicyState &, const QuorumPolicyState & )
    {
        return false;
    }
} // namespace sgns::trustedpeer
