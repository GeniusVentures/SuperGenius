/**
 * @file       SecureCrdtCandidate.hpp
 * @brief      Canonical, bounded candidate approval records for SecureCrdt.
 */
#ifndef SGNS_SECURECRDT_SECURECRDTCANDIDATE_HPP
#define SGNS_SECURECRDT_SECURECRDTCANDIDATE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "crdt/hierarchical_key.hpp"

namespace sgns::securecrdt
{
    enum class CandidateKind : uint8_t
    {
        TrustPolicy       = 1,
        BurnConfig        = 2,
        TrustedPeerGenesis = 3,
    };

    struct CandidateLimits
    {
        static constexpr size_t MAX_CANDIDATE_BYTES                         = 65536;
        static constexpr size_t MAX_ACTIVE_CANDIDATES_PER_PREDECESSOR       = 32;
        static constexpr size_t MAX_APPROVALS_PER_CANDIDATE                 = 256;
        static constexpr size_t MAX_ACTIVE_APPROVAL_BYTES_PER_PREDECESSOR   = 64 * 1024 * 1024;

        [[nodiscard]] static bool CandidateCountAllowed( size_t count );
        [[nodiscard]] static bool ApprovalCountAllowed( size_t count );
        [[nodiscard]] static bool ApprovalBytesAllowed( size_t current_bytes, size_t additional_bytes );
    };

    struct CandidateCore
    {
        static constexpr uint8_t          ENCODING_VERSION = 1;
        static constexpr std::string_view RECORD_DOMAIN    = "SGNS_SECURECRDT_CANDIDATE_V1";

        uint8_t              encoding_version = ENCODING_VERSION;
        std::string          domain;
        uint16_t             network_id = 0;
        CandidateKind        kind       = CandidateKind::TrustPolicy;
        uint64_t             version    = 0;
        std::string          expected_previous_hash;
        std::string          authorizing_policy_hash;
        std::vector<uint8_t> payload;

        [[nodiscard]] std::optional<std::vector<uint8_t>> CanonicalBytes() const;
        [[nodiscard]] std::optional<std::string>          Hash() const;
        [[nodiscard]] static std::optional<CandidateCore> DecodeCanonical( const std::vector<uint8_t> &bytes );

        bool operator==( const CandidateCore &other ) const;
    };

    struct CandidateId
    {
        std::string domain;
        uint64_t    version = 0;
        std::string content_hash;
        std::string expected_previous_hash;

        [[nodiscard]] static std::optional<CandidateId> FromCore( const CandidateCore &core );
        bool operator==( const CandidateId &other ) const;
    };

    struct CandidateKey
    {
        CandidateId id;
        std::string signer;

        [[nodiscard]] sgns::crdt::HierarchicalKey ToHierarchicalKey() const;
        [[nodiscard]] static std::optional<CandidateKey> Parse( const sgns::crdt::HierarchicalKey &key );
    };

    struct CandidateApprovalRecord
    {
        static constexpr uint8_t          ENCODING_VERSION = 1;
        static constexpr std::string_view RECORD_DOMAIN    = "SGNS_SECURECRDT_APPROVAL_V1";

        uint8_t              encoding_version = ENCODING_VERSION;
        CandidateCore        core;
        std::string          signer;
        std::vector<uint8_t> signature;

        [[nodiscard]] std::optional<std::vector<uint8_t>> CanonicalBytes() const;
        [[nodiscard]] static std::optional<CandidateApprovalRecord> DecodeCanonical(
            const std::vector<uint8_t> &bytes,
            const CandidateKey         &key );
    };
} // namespace sgns::securecrdt

#endif // SGNS_SECURECRDT_SECURECRDTCANDIDATE_HPP
