#include "trustedpeer/TrustStateStore.hpp"

#include <mutex>

#include "storage/rocksdb/rocksdb.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns::trustedpeer, TrustStateStore::Error, e )
{
    return "trust state store behavior not implemented";
}

namespace sgns::trustedpeer
{
    std::optional<std::vector<uint8_t>> ConfirmedBurnState::CanonicalBytes() const { return std::nullopt; }
    std::optional<std::string> ConfirmedBurnState::Hash() const { return std::nullopt; }
    std::optional<ConfirmedBurnState> ConfirmedBurnState::DecodeCanonical( const std::vector<uint8_t> & )
    {
        return std::nullopt;
    }
    bool ConfirmedBurnState::operator==( const ConfirmedBurnState &other ) const
    {
        return encoding_version == other.encoding_version && network_id == other.network_id &&
               version == other.version && expected_previous_hash == other.expected_previous_hash &&
               authorizing_policy_hash == other.authorizing_policy_hash && basis_points == other.basis_points;
    }
    bool ConfirmedTrustSnapshot::operator==( const ConfirmedTrustSnapshot &other ) const
    {
        return genesis == other.genesis && genesis_fingerprint == other.genesis_fingerprint &&
               bootstrap_signature == other.bootstrap_signature && policy == other.policy &&
               policy_proof == other.policy_proof && burn == other.burn && burn_proof == other.burn_proof;
    }
    TrustStateStore::TrustStateStore( std::shared_ptr<storage::rocksdb> database,
                                      uint16_t network_id,
                                      BatchCommitter committer ) :
        database_( std::move( database ) ), network_id_( network_id ), committer_( std::move( committer ) ) {}
    outcome::result<std::shared_ptr<TrustStateStore>> TrustStateStore::Open(
        const std::string &path, uint16_t network_id, BatchCommitter committer )
    {
        auto database = storage::rocksdb::create( path );
        if ( database.has_error() ) return outcome::failure( Error::COMMIT_FAILED );
        return std::shared_ptr<TrustStateStore>(
            new TrustStateStore( database.value(), network_id, std::move( committer ) ) );
    }
    outcome::result<ConfirmedTrustSnapshot> TrustStateStore::LoadAndVerify() const
    {
        return outcome::failure( Error::NOT_FOUND );
    }
    outcome::result<ConfirmedTrustSnapshot> TrustStateStore::CommitGenesis(
        const GenesisManifest &, const std::vector<uint8_t> & )
    {
        return outcome::failure( Error::COMMIT_FAILED );
    }
    outcome::result<ConfirmedTrustSnapshot> TrustStateStore::CommitPolicySuccessor(
        const QuorumPolicyState &, const multisig::CollectedSignatures & )
    {
        return outcome::failure( Error::COMMIT_FAILED );
    }
    outcome::result<ConfirmedTrustSnapshot> TrustStateStore::CommitBurnSuccessor(
        const ConfirmedBurnState &, const multisig::CollectedSignatures & )
    {
        return outcome::failure( Error::COMMIT_FAILED );
    }
    outcome::result<void> TrustStateStore::CommitWrites( const std::vector<Write> & )
    {
        return outcome::failure( Error::COMMIT_FAILED );
    }
} // namespace sgns::trustedpeer
