#include "securecrdt/SecureCrdtCandidate.hpp"

namespace sgns::securecrdt
{
    bool CandidateLimits::CandidateCountAllowed( size_t )
    {
        return false;
    }

    bool CandidateLimits::ApprovalCountAllowed( size_t )
    {
        return false;
    }

    bool CandidateLimits::ApprovalBytesAllowed( size_t, size_t )
    {
        return false;
    }

    std::optional<std::vector<uint8_t>> CandidateCore::CanonicalBytes() const
    {
        return std::nullopt;
    }

    std::optional<std::string> CandidateCore::Hash() const
    {
        return std::nullopt;
    }

    std::optional<CandidateCore> CandidateCore::DecodeCanonical( const std::vector<uint8_t> & )
    {
        return std::nullopt;
    }

    bool CandidateCore::operator==( const CandidateCore &other ) const
    {
        return encoding_version == other.encoding_version && domain == other.domain && network_id == other.network_id &&
               kind == other.kind && version == other.version &&
               expected_previous_hash == other.expected_previous_hash &&
               authorizing_policy_hash == other.authorizing_policy_hash && payload == other.payload;
    }

    std::optional<CandidateId> CandidateId::FromCore( const CandidateCore & )
    {
        return std::nullopt;
    }

    bool CandidateId::operator==( const CandidateId &other ) const
    {
        return domain == other.domain && version == other.version && content_hash == other.content_hash &&
               expected_previous_hash == other.expected_previous_hash;
    }

    sgns::crdt::HierarchicalKey CandidateKey::ToHierarchicalKey() const
    {
        return {};
    }

    std::optional<CandidateKey> CandidateKey::Parse( const sgns::crdt::HierarchicalKey & )
    {
        return std::nullopt;
    }

    std::optional<std::vector<uint8_t>> CandidateApprovalRecord::CanonicalBytes() const
    {
        return std::nullopt;
    }

    std::optional<CandidateApprovalRecord> CandidateApprovalRecord::DecodeCanonical(
        const std::vector<uint8_t> &,
        const CandidateKey & )
    {
        return std::nullopt;
    }
} // namespace sgns::securecrdt
