#include "trustedpeer/TrustStateStore.hpp"

#include "securecrdt/SecureCrdtCandidate.hpp"

#include <algorithm>
#include <limits>
#include <map>

#include <gsl/span>

#include "base/hexutil.hpp"
#include "multisig/MultiSig.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "storage/rocksdb/rocksdb_batch.hpp"
#include "trustedpeer/CanonicalTrustCodec.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns::trustedpeer, TrustStateStore::Error, e )
{
    using Error = sgns::trustedpeer::TrustStateStore::Error;
    switch ( e )
    {
        case Error::NOT_FOUND:
            return "confirmed trust state not found";
        case Error::ALREADY_INITIALIZED:
            return "trust state is already initialized";
        case Error::NETWORK_MISMATCH:
            return "persisted trust state belongs to another network";
        case Error::CORRUPT_GENESIS:
            return "persisted genesis record is corrupt";
        case Error::CORRUPT_FINGERPRINT:
            return "persisted genesis fingerprint is corrupt";
        case Error::INVALID_GENESIS_PROOF:
            return "persisted genesis proof is invalid";
        case Error::MISSING_POLICY_RECORD:
            return "policy head references a missing record";
        case Error::MISSING_BURN_RECORD:
            return "burn head references a missing record";
        case Error::CORRUPT_POLICY_RECORD:
            return "persisted policy record is corrupt";
        case Error::CORRUPT_BURN_RECORD:
            return "persisted burn record is corrupt";
        case Error::INVALID_POLICY_PROOF:
            return "persisted policy proof is invalid";
        case Error::INVALID_BURN_PROOF:
            return "persisted burn proof is invalid";
        case Error::VERSION_DECREASE:
            return "candidate version does not advance the durable version";
        case Error::VERSION_SKIP:
            return "candidate version skips the next durable version";
        case Error::WRONG_PREDECESSOR:
            return "candidate does not descend from the durable head";
        case Error::WRONG_AUTHORIZER:
            return "candidate is not authorized by the durable policy head";
        case Error::INITIAL_BURN_NOT_CONFIRMED:
            return "initial burn state does not yet have peer-quorum authorization";
        case Error::STALE_HEAD:
            return "candidate lost the durable-head transition race";
        case Error::COMMIT_FAILED:
            return "synchronous trust-state batch commit failed";
    }
    return "unknown TrustStateStore::Error";
}

namespace sgns::trustedpeer
{
    namespace
    {
        constexpr std::string_view BURN_DOMAIN                = "SGNS_BURN_STATE_V1";
        constexpr std::string_view BURN_GENESIS_ANCHOR_DOMAIN = "SGNS_BURN_GENESIS_ANCHOR_V1";
        constexpr std::string_view GENESIS_RECORD             = "SGNS_TRUST_GENESIS_RECORD_V1";
        constexpr std::string_view POLICY_RECORD              = "SGNS_TRUST_POLICY_RECORD_V1";
        constexpr std::string_view BURN_RECORD                = "SGNS_TRUST_BURN_RECORD_V1";
        constexpr size_t           HASH_HEX_LENGTH            = 64;
        constexpr size_t           MAX_SIGNATURE_LEN          = 256;

        bool IsHash( std::string_view value )
        {
            return value.size() == HASH_HEX_LENGTH && sgns::base::IsLowerHex( value );
        }

        base::Buffer Buffer( std::string_view value )
        {
            return base::Buffer{}.put( value );
        }

        base::Buffer Buffer( const std::vector<uint8_t> &value )
        {
            return base::Buffer( value );
        }

        std::string Prefix( uint16_t network_id )
        {
            return "trust/version-1/network/" + std::to_string( network_id ) + "/";
        }

        std::string GenesisKey( uint16_t network_id )
        {
            return Prefix( network_id ) + "genesis";
        }

        std::string PolicyHeadKey( uint16_t network_id )
        {
            return Prefix( network_id ) + "policy/head";
        }

        std::string BurnHeadKey( uint16_t network_id )
        {
            return Prefix( network_id ) + "burn/head";
        }

        std::string PolicyRecordKey( uint16_t network_id, uint64_t version, const std::string &hash )
        {
            return Prefix( network_id ) + "policy/version-" + std::to_string( version ) + "/" + hash;
        }

        std::string BurnRecordKey( uint16_t network_id, uint64_t version, const std::string &hash )
        {
            return Prefix( network_id ) + "burn/version-" + std::to_string( version ) + "/" + hash;
        }

        std::vector<uint8_t> EncodeHead( uint64_t version, std::string_view hash )
        {
            CanonicalTrustCodec::Writer writer;
            writer.WriteU64( version );
            writer.WriteBytes( hash );
            return writer.Take();
        }

        std::optional<std::pair<uint64_t, std::string>> DecodeHead( const base::Buffer &bytes )
        {
            CanonicalTrustCodec::Reader reader( bytes );
            auto                        version = reader.ReadU64();
            auto                        hash    = reader.ReadBytes( HASH_HEX_LENGTH );
            if ( !version || *version == 0 || !hash || !reader.Exhausted() )
            {
                return std::nullopt;
            }
            std::string hash_string( hash->begin(), hash->end() );
            if ( !IsHash( hash_string ) )
            {
                return std::nullopt;
            }
            return std::make_pair( *version, std::move( hash_string ) );
        }

        void EncodeProof( CanonicalTrustCodec::Writer &writer, const multisig::CollectedSignatures &proof )
        {
            writer.WriteU32( static_cast<uint32_t>( proof.size() ) );
            for ( const auto &[signer, signature] : proof )
            {
                (void) writer.WriteLengthPrefixedBytes(
                    gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( signer.data() ), signer.size() ) );
                (void) writer.WriteLengthPrefixedBytes( signature );
            }
        }

        std::optional<multisig::CollectedSignatures> DecodeProof( CanonicalTrustCodec::Reader &reader )
        {
            auto count = reader.ReadU32();
            if ( !count || *count == 0 || *count > CanonicalTrustCodec::MAX_TRUSTED_PEERS )
            {
                return std::nullopt;
            }
            multisig::CollectedSignatures proof;
            proof.reserve( *count );
            for ( uint32_t index = 0; index < *count; ++index )
            {
                auto signer    = reader.ReadLengthPrefixedBytes( CanonicalTrustCodec::PUBLIC_KEY_BYTES * 2 );
                auto signature = reader.ReadLengthPrefixedBytes( MAX_SIGNATURE_LEN );
                if ( !signer || signer->size() != CanonicalTrustCodec::PUBLIC_KEY_BYTES * 2 || !signature ||
                     signature->empty() )
                {
                    return std::nullopt;
                }
                proof.emplace_back( std::string( signer->begin(), signer->end() ), std::move( *signature ) );
            }
            return proof;
        }

        std::vector<uint8_t> EncodeGenesisRecord( const std::vector<uint8_t> &bytes,
                                                  std::string_view            fingerprint,
                                                  const std::vector<uint8_t> &signature )
        {
            CanonicalTrustCodec::Writer writer;
            writer.WriteBytes( GENESIS_RECORD );
            (void) writer.WriteLengthPrefixedBytes( bytes );
            (void) writer.WriteLengthPrefixedBytes(
                gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( fingerprint.data() ),
                                          fingerprint.size() ) );
            (void) writer.WriteLengthPrefixedBytes( signature );
            return writer.Take();
        }

        struct GenesisRecord
        {
            std::vector<uint8_t> bytes;
            std::string          fingerprint;
            std::vector<uint8_t> signature;
        };

        std::optional<GenesisRecord> DecodeGenesisRecord( const base::Buffer &value )
        {
            CanonicalTrustCodec::Reader reader( value );
            auto                        domain      = reader.ReadBytes( GENESIS_RECORD.size() );
            auto                        bytes       = reader.ReadLengthPrefixedBytes( 64 * 1024 );
            auto                        fingerprint = reader.ReadLengthPrefixedBytes( HASH_HEX_LENGTH );
            auto                        signature   = reader.ReadLengthPrefixedBytes( MAX_SIGNATURE_LEN );
            if ( !domain || !std::equal( domain->begin(), domain->end(), GENESIS_RECORD.begin() ) || !bytes ||
                 !fingerprint || fingerprint->size() != HASH_HEX_LENGTH || !signature || signature->empty() ||
                 !reader.Exhausted() )
            {
                return std::nullopt;
            }
            GenesisRecord record{ std::move( *bytes ),
                                  std::string( fingerprint->begin(), fingerprint->end() ),
                                  std::move( *signature ) };
            if ( !IsHash( record.fingerprint ) )
            {
                return std::nullopt;
            }
            return record;
        }

        std::vector<uint8_t> EncodeSignedRecord( std::string_view                     domain,
                                                 const std::vector<uint8_t>          &bytes,
                                                 const std::vector<uint8_t>          &authorization_bytes,
                                                 const multisig::CollectedSignatures &proof )
        {
            CanonicalTrustCodec::Writer writer;
            writer.WriteBytes( domain );
            (void) writer.WriteLengthPrefixedBytes( bytes );
            (void) writer.WriteLengthPrefixedBytes( authorization_bytes );
            EncodeProof( writer, proof );
            return writer.Take();
        }

        struct SignedRecord
        {
            std::vector<uint8_t>          bytes;
            std::vector<uint8_t>          authorization_bytes;
            multisig::CollectedSignatures proof;
        };

        std::optional<SignedRecord> DecodeSignedRecord( const base::Buffer &value, std::string_view domain )
        {
            CanonicalTrustCodec::Reader current_reader( value );
            auto                        current_domain      = current_reader.ReadBytes( domain.size() );
            auto                        current_bytes       = current_reader.ReadLengthPrefixedBytes( 64 * 1024 );
            auto                        authorization_bytes = current_reader.ReadLengthPrefixedBytes( 128 * 1024 );
            if ( current_domain && std::equal( current_domain->begin(), current_domain->end(), domain.begin() ) &&
                 current_bytes && authorization_bytes && !authorization_bytes->empty() )
            {
                auto proof = DecodeProof( current_reader );
                if ( proof && current_reader.Exhausted() )
                {
                    return SignedRecord{ std::move( *current_bytes ),
                                         std::move( *authorization_bytes ),
                                         std::move( *proof ) };
                }
            }

            // Compatibility with the original Phase 13-02 record layout,
            // where signatures directly followed the canonical state bytes.
            CanonicalTrustCodec::Reader legacy_reader( value );
            auto                        legacy_domain = legacy_reader.ReadBytes( domain.size() );
            auto                        legacy_bytes  = legacy_reader.ReadLengthPrefixedBytes( 64 * 1024 );
            if ( !legacy_domain || !std::equal( legacy_domain->begin(), legacy_domain->end(), domain.begin() ) ||
                 !legacy_bytes )
            {
                return std::nullopt;
            }
            auto legacy_proof = DecodeProof( legacy_reader );
            if ( !legacy_proof || !legacy_reader.Exhausted() )
            {
                return std::nullopt;
            }
            return SignedRecord{ *legacy_bytes, std::move( *legacy_bytes ), std::move( *legacy_proof ) };
        }

        bool VerifyProof( const QuorumPolicyState             &policy,
                          uint64_t                             threshold,
                          const multisig::CollectedSignatures &proof,
                          const std::vector<uint8_t>          &bytes )
        {
            multisig::MultiSig verifier( policy.peers, threshold );
            return verifier.IsValid() && verifier.EvaluateQuorum( proof, bytes ).has_quorum;
        }

        bool AuthorizationBindsPolicy( const std::vector<uint8_t> &authorization_bytes,
                                       const std::vector<uint8_t> &policy_bytes,
                                       const QuorumPolicyState    &policy )
        {
            if ( authorization_bytes == policy_bytes )
            {
                return true;
            }
            auto core = securecrdt::CandidateCore::DecodeCanonical( authorization_bytes );
            return core && core->kind == securecrdt::CandidateKind::TrustPolicy &&
                   core->network_id == policy.network_id && core->version == policy.version &&
                   core->expected_previous_hash == policy.expected_previous_hash &&
                   core->authorizing_policy_hash == policy.authorizing_policy_hash && core->payload == policy_bytes;
        }

        bool AuthorizationBindsBurn( const std::vector<uint8_t> &authorization_bytes,
                                     const std::vector<uint8_t> &burn_bytes,
                                     const ConfirmedBurnState   &burn )
        {
            if ( authorization_bytes == burn_bytes )
            {
                return true;
            }
            auto core = securecrdt::CandidateCore::DecodeCanonical( authorization_bytes );
            return core && core->kind == securecrdt::CandidateKind::BurnConfig && core->network_id == burn.network_id &&
                   core->version == burn.version && core->expected_previous_hash == burn.expected_previous_hash &&
                   core->authorizing_policy_hash == burn.authorizing_policy_hash && core->payload == burn_bytes;
        }

        bool IsCanonicalBurnCandidateAuthorization( const std::vector<uint8_t> &authorization_bytes,
                                                     const std::vector<uint8_t> &burn_bytes,
                                                     const ConfirmedBurnState   &burn )
        {
            // The canonical authorization is the candidate core, never the raw state bytes.
            return authorization_bytes != burn_bytes && AuthorizationBindsBurn( authorization_bytes, burn_bytes, burn );
        }
    } // namespace

    std::string BurnGenesisAnchorHash( const std::string &genesis_fingerprint )
    {
        CanonicalTrustCodec::Writer writer;
        writer.WriteBytes( BURN_GENESIS_ANCHOR_DOMAIN );
        writer.WriteBytes( genesis_fingerprint );
        return CanonicalTrustCodec::Sha256Hex( writer.Take() );
    }

    std::optional<std::vector<uint8_t>> ConfirmedBurnState::CanonicalBytes() const
    {
        if ( encoding_version != ENCODING_VERSION || version == 0 || !IsHash( expected_previous_hash ) ||
             !IsHash( authorizing_policy_hash ) || basis_points > 10000 )
        {
            return std::nullopt;
        }
        CanonicalTrustCodec::Writer writer;
        writer.WriteBytes( BURN_DOMAIN );
        writer.WriteU8( encoding_version );
        writer.WriteU16( network_id );
        writer.WriteU64( version );
        writer.WriteBytes( expected_previous_hash );
        writer.WriteBytes( authorizing_policy_hash );
        writer.WriteU64( basis_points );
        return writer.Take();
    }

    std::optional<std::string> ConfirmedBurnState::Hash() const
    {
        auto bytes = CanonicalBytes();
        if ( !bytes )
        {
            return std::nullopt;
        }
        return CanonicalTrustCodec::Sha256Hex( *bytes );
    }

    std::optional<ConfirmedBurnState> ConfirmedBurnState::DecodeCanonical( const std::vector<uint8_t> &bytes )
    {
        CanonicalTrustCodec::Reader reader( bytes );
        auto                        domain        = reader.ReadBytes( BURN_DOMAIN.size() );
        auto                        encoding      = reader.ReadU8();
        auto                        network       = reader.ReadU16();
        auto                        version_value = reader.ReadU64();
        auto                        previous      = reader.ReadBytes( HASH_HEX_LENGTH );
        auto                        authorizer    = reader.ReadBytes( HASH_HEX_LENGTH );
        auto                        value         = reader.ReadU64();
        if ( !domain || !std::equal( domain->begin(), domain->end(), BURN_DOMAIN.begin() ) || !encoding || !network ||
             !version_value || !previous || !authorizer || !value || !reader.Exhausted() )
        {
            return std::nullopt;
        }
        ConfirmedBurnState burn;
        burn.encoding_version = *encoding;
        burn.network_id       = *network;
        burn.version          = *version_value;
        burn.expected_previous_hash.assign( previous->begin(), previous->end() );
        burn.authorizing_policy_hash.assign( authorizer->begin(), authorizer->end() );
        burn.basis_points = *value;
        auto canonical    = burn.CanonicalBytes();
        if ( !canonical || *canonical != bytes )
        {
            return std::nullopt;
        }
        return burn;
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
               policy_proof == other.policy_proof && burn == other.burn && burn_proof == other.burn_proof &&
               burn_authorization == other.burn_authorization;
    }

    TrustStateStore::TrustStateStore( std::shared_ptr<storage::rocksdb> database,
                                      uint16_t                          network_id,
                                      BatchCommitter                    committer,
                                      LoadObserver                      load_observer ) :
        database_( std::move( database ) ),
        network_id_( network_id ),
        committer_( std::move( committer ) ),
        load_observer_( std::move( load_observer ) )
    {
    }

    outcome::result<std::shared_ptr<TrustStateStore>> TrustStateStore::Open( const std::string &path,
                                                                             uint16_t           network_id,
                                                                             BatchCommitter     committer,
                                                                             LoadObserver       load_observer )
    {
        storage::rocksdb::Options options;
        options.create_if_missing = true;
        auto database             = storage::rocksdb::create( path, options );
        if ( database.has_error() )
        {
            return outcome::failure( Error::COMMIT_FAILED );
        }
        return std::shared_ptr<TrustStateStore>(
            new TrustStateStore(
                database.value(), network_id, std::move( committer ), std::move( load_observer ) ) );
    }

    outcome::result<ConfirmedTrustSnapshot> TrustStateStore::LoadAndVerify() const
    {
        std::lock_guard<std::mutex> lock( transition_mutex_ );
        return LoadAndVerifyUnlocked();
    }

    outcome::result<ConfirmedTrustSnapshot> TrustStateStore::LoadAndVerifyUnlocked() const
    {
        auto genesis_value = database_->get( Buffer( GenesisKey( network_id_ ) ) );
        if ( genesis_value.has_error() )
        {
            auto any_network = database_->query( Buffer( "trust/version-1/network/" ) );
            if ( any_network.has_value() && !any_network.value().empty() )
            {
                return outcome::failure( Error::NETWORK_MISMATCH );
            }
            return outcome::failure( Error::NOT_FOUND );
        }
        auto genesis_record = DecodeGenesisRecord( genesis_value.value() );
        if ( !genesis_record )
        {
            return outcome::failure( Error::CORRUPT_GENESIS );
        }
        auto genesis = GenesisManifest::DecodeAndVerify( genesis_record->bytes, genesis_record->fingerprint );
        if ( !genesis )
        {
            return outcome::failure( Error::CORRUPT_FINGERPRINT );
        }
        if ( genesis->network_id != network_id_ )
        {
            return outcome::failure( Error::NETWORK_MISMATCH );
        }
        const auto genesis_core = GenesisCandidateCore( *genesis, genesis_record->bytes, genesis_record->fingerprint );
        const auto genesis_authorization = genesis_core.CanonicalBytes();
        if ( !multisig::VerifyPayloadSignature( genesis->bootstrapper_public_key,
                                                genesis_record->signature,
                                                genesis_record->bytes ) &&
             ( !genesis_authorization || !multisig::VerifyPayloadSignature( genesis->bootstrapper_public_key,
                                                                            genesis_record->signature,
                                                                            *genesis_authorization ) ) )
        {
            return outcome::failure( Error::INVALID_GENESIS_PROOF );
        }

        auto policy_head_value = database_->get( Buffer( PolicyHeadKey( network_id_ ) ) );
        if ( policy_head_value.has_error() )
        {
            return outcome::failure( Error::MISSING_POLICY_RECORD );
        }
        auto policy_head = DecodeHead( policy_head_value.value() );
        if ( !policy_head )
        {
            return outcome::failure( Error::CORRUPT_POLICY_RECORD );
        }

        std::map<std::string, QuorumPolicyState>             policies;
        std::map<std::string, multisig::CollectedSignatures> policy_proofs;
        std::map<std::string, std::vector<uint8_t>>          policy_authorizations;
        std::string                                          policy_hash = policy_head->second;
        for ( uint64_t version = policy_head->first; version > 0; --version )
        {
            auto value = database_->get( Buffer( PolicyRecordKey( network_id_, version, policy_hash ) ) );
            if ( value.has_error() )
            {
                return outcome::failure( Error::MISSING_POLICY_RECORD );
            }
            auto record = DecodeSignedRecord( value.value(), POLICY_RECORD );
            if ( !record )
            {
                return outcome::failure( Error::CORRUPT_POLICY_RECORD );
            }
            auto policy = QuorumPolicyState::DecodeCanonical( record->bytes );
            if ( !policy || policy->network_id != network_id_ || policy->version != version ||
                 policy->Hash() != std::optional<std::string>( policy_hash ) )
            {
                return outcome::failure( Error::CORRUPT_POLICY_RECORD );
            }
            policies.emplace( policy_hash, *policy );
            policy_proofs.emplace( policy_hash, record->proof );
            policy_authorizations.emplace( policy_hash, record->authorization_bytes );
            policy_hash = policy->expected_previous_hash;
        }

        const auto current_policy = policies.find( policy_head->second );
        if ( current_policy == policies.end() )
        {
            return outcome::failure( Error::CORRUPT_POLICY_RECORD );
        }
        for ( const auto &[hash, policy] : policies )
        {
            const auto &proof               = policy_proofs.at( hash );
            const auto &authorization_bytes = policy_authorizations.at( hash );
            auto        bytes               = policy.CanonicalBytes().value();
            if ( policy.version == 1 )
            {
                if ( policy.expected_previous_hash != genesis_record->fingerprint ||
                     policy.authorizing_policy_hash != genesis_record->fingerprint || policy.peers != genesis->peers ||
                     policy.membership_threshold != genesis->membership_threshold ||
                     policy.burn_threshold != genesis->burn_threshold || proof.size() != 1 ||
                     proof.front().first != genesis->bootstrapper_public_key ||
                     proof.front().second != genesis_record->signature )
                {
                    return outcome::failure( Error::INVALID_POLICY_PROOF );
                }
            }
            else
            {
                auto predecessor = policies.find( policy.expected_previous_hash );
                if ( predecessor == policies.end() || policy.authorizing_policy_hash != predecessor->first ||
                     !ValidatePolicySuccessor( predecessor->second, policy ) ||
                     !AuthorizationBindsPolicy( authorization_bytes, bytes, policy ) ||
                     !VerifyProof( predecessor->second,
                                   predecessor->second.membership_threshold,
                                   proof,
                                   authorization_bytes ) )
                {
                    return outcome::failure( Error::INVALID_POLICY_PROOF );
                }
            }
        }

        if ( load_observer_ )
        {
            load_observer_();
        }

        auto burn_head_value = database_->get( Buffer( BurnHeadKey( network_id_ ) ) );
        if ( burn_head_value.has_error() )
        {
            return outcome::failure( Error::MISSING_BURN_RECORD );
        }
        auto burn_head = DecodeHead( burn_head_value.value() );
        if ( !burn_head )
        {
            return outcome::failure( Error::CORRUPT_BURN_RECORD );
        }
        std::map<std::string, ConfirmedBurnState>            burns;
        std::map<std::string, multisig::CollectedSignatures> burn_proofs;
        std::map<std::string, std::vector<uint8_t>>          burn_authorizations;
        std::map<std::string, BurnAuthorizationKind>         burn_authorization_kinds;
        std::string                                          burn_hash = burn_head->second;
        for ( uint64_t version = burn_head->first; version > 0; --version )
        {
            auto value = database_->get( Buffer( BurnRecordKey( network_id_, version, burn_hash ) ) );
            if ( value.has_error() )
            {
                return outcome::failure( Error::MISSING_BURN_RECORD );
            }
            auto record = DecodeSignedRecord( value.value(), BURN_RECORD );
            if ( !record )
            {
                return outcome::failure( Error::CORRUPT_BURN_RECORD );
            }
            auto burn = ConfirmedBurnState::DecodeCanonical( record->bytes );
            if ( !burn || burn->network_id != network_id_ || burn->version != version ||
                 burn->Hash() != std::optional<std::string>( burn_hash ) )
            {
                return outcome::failure( Error::CORRUPT_BURN_RECORD );
            }
            burns.emplace( burn_hash, *burn );
            burn_proofs.emplace( burn_hash, record->proof );
            burn_authorizations.emplace( burn_hash, record->authorization_bytes );
            burn_hash = burn->expected_previous_hash;
        }
        const auto current_burn = burns.find( burn_head->second );
        if ( current_burn == burns.end() )
        {
            return outcome::failure( Error::CORRUPT_BURN_RECORD );
        }
        for ( const auto &[hash, burn] : burns )
        {
            const auto &proof               = burn_proofs.at( hash );
            const auto &authorization_bytes = burn_authorizations.at( hash );
            if ( burn.version == 1 )
            {
                auto       policy           = policies.find( burn.authorizing_policy_hash );
                auto       bytes            = burn.CanonicalBytes().value();
                const bool legacy_bootstrap = burn.expected_previous_hash == genesis_record->fingerprint &&
                                              authorization_bytes == bytes;
                const bool bootstrap_proof = proof.size() == 1 &&
                                             proof.front().first == genesis->bootstrapper_public_key &&
                                             proof.front().second == genesis_record->signature &&
                                             ( authorization_bytes == genesis_record->bytes ||
                                               ( genesis_authorization &&
                                                 authorization_bytes == *genesis_authorization ) ||
                                               legacy_bootstrap );
                const bool peer_proof = policy != policies.end() &&
                                        AuthorizationBindsBurn( authorization_bytes, bytes, burn ) &&
                                        VerifyProof( policy->second,
                                                     policy->second.burn_threshold,
                                                     proof,
                                                     authorization_bytes );
                if ( ( burn.expected_previous_hash != BurnGenesisAnchorHash( genesis_record->fingerprint ) &&
                       !legacy_bootstrap ) ||
                     policy == policies.end() || policy->second.version != 1 ||
                     burn.basis_points != genesis->initial_burn_basis_points || ( !bootstrap_proof && !peer_proof ) )
                {
                    return outcome::failure( Error::INVALID_BURN_PROOF );
                }
                burn_authorization_kinds.emplace(
                    hash, peer_proof ? BurnAuthorizationKind::PeerQuorum : BurnAuthorizationKind::BootstrapOnly );
            }
            else
            {
                auto predecessor = burns.find( burn.expected_previous_hash );
                auto policy      = policies.find( burn.authorizing_policy_hash );
                auto bytes       = burn.CanonicalBytes().value();
                if ( predecessor == burns.end() || predecessor->second.version + 1 != burn.version ||
                     policy == policies.end() || !AuthorizationBindsBurn( authorization_bytes, bytes, burn ) ||
                     !VerifyProof( policy->second, policy->second.burn_threshold, proof, authorization_bytes ) )
                {
                    return outcome::failure( Error::INVALID_BURN_PROOF );
                }
                burn_authorization_kinds.emplace( hash, BurnAuthorizationKind::PeerQuorum );
            }
        }

        return ConfirmedTrustSnapshot{ *genesis,
                                       genesis_record->fingerprint,
                                       genesis_record->signature,
                                       current_policy->second,
                                       policy_proofs.at( current_policy->first ),
                                       current_burn->second,
                                       burn_proofs.at( current_burn->first ),
                                       burn_authorization_kinds.at( current_burn->first ) };
    }

    outcome::result<ConfirmedTrustSnapshot> TrustStateStore::CommitGenesis(
        const GenesisManifest      &manifest,
        const std::vector<uint8_t> &bootstrap_signature,
        const std::vector<uint8_t> &authorization_bytes )
    {
        std::lock_guard<std::mutex> lock( transition_mutex_ );
        if ( database_->contains( Buffer( GenesisKey( network_id_ ) ) ) )
        {
            return outcome::failure( Error::ALREADY_INITIALIZED );
        }
        auto canonical   = manifest.Canonicalized();
        auto bytes       = manifest.CanonicalBytes();
        auto fingerprint = manifest.Fingerprint();
        if ( !canonical || !bytes || !fingerprint )
        {
            return outcome::failure( Error::CORRUPT_GENESIS );
        }
        if ( canonical->network_id != network_id_ )
        {
            return outcome::failure( Error::NETWORK_MISMATCH );
        }
        const auto genesis_core = GenesisCandidateCore( *canonical, *bytes, *fingerprint );
        const auto expected_candidate_authorization = genesis_core.CanonicalBytes();
        const auto                           &proof_bytes = authorization_bytes.empty() ? *bytes : authorization_bytes;
        if ( !multisig::VerifyPayloadSignature( canonical->bootstrapper_public_key,
                                                bootstrap_signature,
                                                proof_bytes ) ||
             ( proof_bytes != *bytes &&
               ( !expected_candidate_authorization || proof_bytes != *expected_candidate_authorization ) ) )
        {
            return outcome::failure( Error::INVALID_GENESIS_PROOF );
        }

        QuorumPolicyState policy;
        policy.network_id              = network_id_;
        policy.version                 = 1;
        policy.expected_previous_hash  = *fingerprint;
        policy.authorizing_policy_hash = *fingerprint;
        policy.peers                   = canonical->peers;
        policy.membership_threshold    = canonical->membership_threshold;
        policy.burn_threshold          = canonical->burn_threshold;
        auto policy_bytes              = policy.CanonicalBytes();
        auto policy_hash               = policy.Hash();
        if ( !policy_bytes || !policy_hash )
        {
            return outcome::failure( Error::CORRUPT_POLICY_RECORD );
        }

        ConfirmedBurnState burn;
        burn.network_id              = network_id_;
        burn.version                 = 1;
        burn.expected_previous_hash  = BurnGenesisAnchorHash( *fingerprint );
        burn.authorizing_policy_hash = *policy_hash;
        burn.basis_points            = canonical->initial_burn_basis_points;
        auto burn_bytes              = burn.CanonicalBytes();
        auto burn_hash               = burn.Hash();
        if ( !burn_bytes || !burn_hash )
        {
            return outcome::failure( Error::CORRUPT_BURN_RECORD );
        }
        multisig::CollectedSignatures proof{ { canonical->bootstrapper_public_key, bootstrap_signature } };

        std::vector<Write> writes{
            { Buffer( GenesisKey( network_id_ ) ),
              Buffer( EncodeGenesisRecord( *bytes, *fingerprint, bootstrap_signature ) ) },
            { Buffer( PolicyRecordKey( network_id_, 1, *policy_hash ) ),
              Buffer( EncodeSignedRecord( POLICY_RECORD, *policy_bytes, proof_bytes, proof ) ) },
            { Buffer( PolicyHeadKey( network_id_ ) ), Buffer( EncodeHead( 1, *policy_hash ) ) },
            { Buffer( BurnRecordKey( network_id_, 1, *burn_hash ) ),
              Buffer( EncodeSignedRecord( BURN_RECORD, *burn_bytes, proof_bytes, proof ) ) },
            { Buffer( BurnHeadKey( network_id_ ) ), Buffer( EncodeHead( 1, *burn_hash ) ) },
        };
        auto committed = CommitWrites( writes );
        if ( committed.has_error() )
        {
            return outcome::failure( Error::COMMIT_FAILED );
        }
        return LoadAndVerifyUnlocked();
    }

    outcome::result<ConfirmedTrustSnapshot> TrustStateStore::CommitPolicySuccessor(
        const QuorumPolicyState             &candidate,
        const multisig::CollectedSignatures &proof,
        const std::vector<uint8_t>          &authorization_bytes )
    {
        std::lock_guard<std::mutex> lock( transition_mutex_ );
        auto                        current_result = LoadAndVerifyUnlocked();
        if ( current_result.has_error() )
        {
            return current_result.error();
        }
        const auto &current = current_result.value();
        if ( current.burn_authorization != BurnAuthorizationKind::PeerQuorum )
        {
            return outcome::failure( Error::INITIAL_BURN_NOT_CONFIRMED );
        }
        if ( candidate.version < current.policy.version )
        {
            return outcome::failure( Error::VERSION_DECREASE );
        }
        if ( candidate.version == current.policy.version )
        {
            const auto candidate_hash = candidate.Hash();
            const auto current_hash   = current.policy.Hash();
            if ( candidate_hash && current_hash && *candidate_hash != *current_hash )
            {
                return outcome::failure( Error::STALE_HEAD );
            }
            // Re-committing the exact durable candidate (the refresh-driven vs
            // admin-driven activation race) is benign — but only with a valid quorum
            // proof; an unproven duplicate stays a downgrade attempt.
            auto bytes = candidate.CanonicalBytes();
            if ( bytes )
            {
                const auto &signed_bytes = authorization_bytes.empty() ? *bytes : authorization_bytes;
                if ( AuthorizationBindsPolicy( signed_bytes, *bytes, candidate ) &&
                     VerifyProof( current.policy, current.policy.membership_threshold, proof, signed_bytes ) )
                {
                    return current;
                }
            }
            return outcome::failure( Error::VERSION_DECREASE );
        }
        if ( current.policy.version == std::numeric_limits<uint64_t>::max() ||
             candidate.version != current.policy.version + 1 )
        {
            return outcome::failure( Error::VERSION_SKIP );
        }
        const auto current_hash = current.policy.Hash().value();
        if ( candidate.expected_previous_hash != current_hash )
        {
            return outcome::failure( Error::WRONG_PREDECESSOR );
        }
        if ( candidate.authorizing_policy_hash != current_hash )
        {
            return outcome::failure( Error::WRONG_AUTHORIZER );
        }
        auto canonical = candidate.Canonicalized();
        auto bytes     = candidate.CanonicalBytes();
        auto hash      = candidate.Hash();
        if ( !canonical || !bytes || !hash || !ValidatePolicySuccessor( current.policy, candidate ) )
        {
            return outcome::failure( Error::CORRUPT_POLICY_RECORD );
        }
        const auto &signed_bytes = authorization_bytes.empty() ? *bytes : authorization_bytes;
        if ( !AuthorizationBindsPolicy( signed_bytes, *bytes, candidate ) ||
             !VerifyProof( current.policy, current.policy.membership_threshold, proof, signed_bytes ) )
        {
            return outcome::failure( Error::INVALID_POLICY_PROOF );
        }
        return CommitRecordAndHead(
            { Buffer( PolicyRecordKey( network_id_, canonical->version, *hash ) ),
              Buffer( EncodeSignedRecord( POLICY_RECORD, *bytes, signed_bytes, proof ) ) },
            { Buffer( PolicyHeadKey( network_id_ ) ), Buffer( EncodeHead( canonical->version, *hash ) ) } );
    }

    outcome::result<ConfirmedTrustSnapshot> TrustStateStore::CommitBurnSuccessor(
        const ConfirmedBurnState            &candidate,
        const multisig::CollectedSignatures &proof,
        const std::vector<uint8_t>          &authorization_bytes )
    {
        std::lock_guard<std::mutex> lock( transition_mutex_ );
        auto                        current_result = LoadAndVerifyUnlocked();
        if ( current_result.has_error() )
        {
            return current_result.error();
        }
        const auto &current = current_result.value();
        if ( current.burn_authorization == BurnAuthorizationKind::BootstrapOnly )
        {
            const auto current_bytes       = current.burn.CanonicalBytes();
            const auto candidate_bytes     = candidate.CanonicalBytes();
            const auto current_hash        = current.burn.Hash();
            const auto candidate_hash      = candidate.Hash();
            const auto current_policy_hash = current.policy.Hash();
            if ( current.burn.version != 1 || current.burn.basis_points != GenesisManifest::INITIAL_BURN_BASIS_POINTS ||
                 !( candidate == current.burn ) || !current_bytes || !candidate_bytes ||
                 *candidate_bytes != *current_bytes ||
                 !current_hash || !candidate_hash || *candidate_hash != *current_hash || !current_policy_hash ||
                 candidate.authorizing_policy_hash != *current_policy_hash || authorization_bytes.empty() ||
                 !IsCanonicalBurnCandidateAuthorization( authorization_bytes, *candidate_bytes, candidate ) ||
                 !VerifyProof( current.policy, current.policy.burn_threshold, proof, authorization_bytes ) )
            {
                return outcome::failure( Error::INITIAL_BURN_NOT_CONFIRMED );
            }
        }
        if ( candidate.version < current.burn.version )
        {
            return outcome::failure( Error::VERSION_DECREASE );
        }
        if ( candidate.version == current.burn.version )
        {
            const auto candidate_hash = candidate.Hash();
            const auto current_hash   = current.burn.Hash();
            if ( candidate_hash && current_hash && *candidate_hash != *current_hash )
            {
                return outcome::failure( Error::STALE_HEAD );
            }
            if ( !candidate_hash || !current_hash )
            {
                return outcome::failure( Error::VERSION_DECREASE );
            }
            auto bytes = candidate.CanonicalBytes();
            if ( !bytes )
            {
                return outcome::failure( Error::CORRUPT_BURN_RECORD );
            }
            const auto &signed_bytes = authorization_bytes.empty() ? *bytes : authorization_bytes;
            if ( !AuthorizationBindsBurn( signed_bytes, *bytes, candidate ) ||
                 !VerifyProof( current.policy, current.policy.burn_threshold, proof, signed_bytes ) )
            {
                return outcome::failure( Error::INVALID_BURN_PROOF );
            }
            // Re-committing the exact durable candidate (the refresh-driven vs
            // admin/peer-driven activation race) is benign — return the durable
            // state instead of failing as a downgrade attempt.
            if ( candidate.version != 1 )
            {
                return current;
            }
            return CommitRecordAndHead(
                { Buffer( BurnRecordKey( network_id_, candidate.version, *candidate_hash ) ),
                  Buffer( EncodeSignedRecord( BURN_RECORD, *bytes, signed_bytes, proof ) ) },
                { Buffer( BurnHeadKey( network_id_ ) ), Buffer( EncodeHead( candidate.version, *candidate_hash ) ) } );
        }
        if ( current.burn.version == std::numeric_limits<uint64_t>::max() ||
             candidate.version != current.burn.version + 1 )
        {
            return outcome::failure( Error::VERSION_SKIP );
        }
        if ( candidate.expected_previous_hash != current.burn.Hash().value() )
        {
            return outcome::failure( Error::WRONG_PREDECESSOR );
        }
        if ( candidate.authorizing_policy_hash != current.policy.Hash().value() )
        {
            return outcome::failure( Error::WRONG_AUTHORIZER );
        }
        auto bytes = candidate.CanonicalBytes();
        auto hash  = candidate.Hash();
        if ( !bytes || !hash || candidate.network_id != network_id_ )
        {
            return outcome::failure( Error::CORRUPT_BURN_RECORD );
        }
        const auto &signed_bytes = authorization_bytes.empty() ? *bytes : authorization_bytes;
        if ( !AuthorizationBindsBurn( signed_bytes, *bytes, candidate ) ||
             !VerifyProof( current.policy, current.policy.burn_threshold, proof, signed_bytes ) )
        {
            return outcome::failure( Error::INVALID_BURN_PROOF );
        }
        return CommitRecordAndHead(
            { Buffer( BurnRecordKey( network_id_, candidate.version, *hash ) ),
              Buffer( EncodeSignedRecord( BURN_RECORD, *bytes, signed_bytes, proof ) ) },
            { Buffer( BurnHeadKey( network_id_ ) ), Buffer( EncodeHead( candidate.version, *hash ) ) } );
    }

    outcome::result<void> TrustStateStore::CommitWrites( const std::vector<Write> &writes )
    {
        if ( committer_ )
        {
            return committer_( *database_, writes );
        }
        auto batch = database_->batch();
        if ( !batch )
        {
            return outcome::failure( Error::COMMIT_FAILED );
        }
        for ( const auto &[key, value] : writes )
        {
            auto put = batch->put( key, value );
            if ( put.has_error() )
            {
                return outcome::failure( Error::COMMIT_FAILED );
            }
        }
        auto commit = batch->commit();
        if ( commit.has_error() )
        {
            return outcome::failure( Error::COMMIT_FAILED );
        }
        return outcome::success();
    }

    outcome::result<ConfirmedTrustSnapshot> TrustStateStore::CommitRecordAndHead( Write record_write, Write head_write )
    {
        auto committed = CommitWrites( { std::move( record_write ), std::move( head_write ) } );
        if ( committed.has_error() )
        {
            return outcome::failure( Error::COMMIT_FAILED );
        }
        return LoadAndVerifyUnlocked();
    }
} // namespace sgns::trustedpeer
