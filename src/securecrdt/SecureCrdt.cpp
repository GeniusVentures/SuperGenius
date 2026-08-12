/**
 * @file       SecureCrdt.cpp
 * @brief      Implementation of the SecureCrdt local-write gate (D-03) and
 *             reader-side quorum re-derivation (D-04).
 * @date       2026-07-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "securecrdt/SecureCrdt.hpp"

#include <algorithm>
#include <unordered_set>

#include "multisig/MultiSig.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns::securecrdt, SecureCrdt::Error, e )
{
    using Error = sgns::securecrdt::SecureCrdt::Error;
    switch ( e )
    {
        case Error::UNREGISTERED_KEY:
            return "base_key has no SecureCrdtRegistry entry";
        case Error::NO_VALUE_PROPOSED:
            return "no value has been proposed at base_key yet";
        case Error::INVALID_SIGNATURE:
            return "signature verification failed against the current value";
        case Error::MALFORMED_VALUE:
            return "payload failed DeserializeFromBytes/Verify (codec/semantic check)";
        case Error::QUORUM_THRESHOLD_BELOW_FLOOR:
            return "configured quorum_threshold is below the majority-safety floor (ceil(0.51*signer_set_size))";
        case Error::UNREGISTERED_CANDIDATE_DOMAIN:
            return "candidate domain has no authorization source";
        case Error::CANDIDATE_CONTEXT_MISMATCH:
            return "candidate does not match the current authorization context";
        case Error::UNAUTHORIZED_CANDIDATE_SIGNER:
            return "candidate signer is not authorized or its signature is invalid";
        case Error::CANDIDATE_LIMIT_EXCEEDED:
            return "candidate resource limit exceeded";
        case Error::DUPLICATE_CANDIDATE_APPROVAL:
            return "candidate already has an approval from this signer";
    }
    return "unknown SecureCrdt::Error";
}

namespace sgns::securecrdt
{
    namespace
    {
        /// @brief Extracts the signer address from a raw datastore key returned by
        ///        GlobalDB::QueryKeyValues(..., QUERY_VALUESUFFIX). QueryElements
        ///        returns entries keyed by the RAW datastore key (e.g.
        ///        "/crdt/s/k/<base>/sig/<address>/v"), not the logical CRDT key
        ///        ("/<base>/sig/<address>") — the value-suffix marker ("v") is
        ///        always the last path segment, so the address is the segment
        ///        immediately preceding it.
        std::string LastKeySegment( const std::string &key )
        {
            sgns::crdt::HierarchicalKey hk( key );
            auto                        list = hk.GetList();
            if ( list.size() < 2 )
            {
                return {};
            }
            return list[list.size() - 2];
        }

        std::string EscapeRegex( const std::string &value )
        {
            static const std::string metacharacters = R"(\.^$|()[]{}*+?)";
            std::string              result;
            result.reserve( value.size() * 2 );
            for ( const char byte : value )
            {
                if ( metacharacters.find( byte ) != std::string::npos )
                {
                    result.push_back( '\\' );
                }
                result.push_back( byte );
            }
            return result;
        }

        std::string CandidatePattern( const std::string &domain )
        {
            return "/?" + EscapeRegex( domain ) + "/candidate/v[1-9][0-9]*/[0-9a-f]{64}/approval/[0-9a-f]{128}";
        }

        struct StoredCandidateRecord
        {
            CandidateKey            key;
            CandidateApprovalRecord record;
            size_t                  bytes = 0;
        };

        outcome::result<std::vector<StoredCandidateRecord>> QueryCandidateRecords(
            const std::shared_ptr<sgns::crdt::GlobalDB> &db,
            const std::string                           &domain )
        {
            auto query = db->QueryKeyValues(
                sgns::crdt::HierarchicalKey( domain ).ChildString( "candidate" ).GetKey() );
            if ( query.has_error() )
            {
                return query.error();
            }

            std::vector<StoredCandidateRecord> records;
            records.reserve( query.value().size() );
            for ( const auto &[raw_key, value] : query.value() )
            {
                const auto logical_key = db->KeyToString( raw_key );
                if ( logical_key.has_error() )
                {
                    continue;
                }
                const auto key = CandidateKey::Parse( sgns::crdt::HierarchicalKey( logical_key.value() ) );
                if ( !key || key->id.domain != domain )
                {
                    continue;
                }
                auto bytes  = value.toVector();
                auto record = CandidateApprovalRecord::DecodeCanonical( bytes, *key );
                if ( record )
                {
                    records.push_back( StoredCandidateRecord{ *key, std::move( *record ), bytes.size() } );
                }
            }
            return records;
        }
    } // namespace

    SecureCrdt::SecureCrdt( std::shared_ptr<sgns::crdt::GlobalDB> db,
                            std::string                           topic,
                            std::shared_ptr<SecureCrdtRegistry>   registry ) :
        db_( std::move( db ) ),
        topic_( std::move( topic ) ),
        registry_( registry ? std::move( registry ) : std::make_shared<SecureCrdtRegistry>() )
    {
    }

    SecureCrdtRegistry &SecureCrdt::Registry()
    {
        return *registry_;
    }

    const SecureCrdtRegistry &SecureCrdt::Registry() const
    {
        return *registry_;
    }

    outcome::result<void> SecureCrdt::ProposeValue( const sgns::crdt::HierarchicalKey &base_key,
                                                    const std::vector<uint8_t>        &payload )
    {
        logger_->trace( "{}: entry key={}", __func__, base_key.GetKey() );
        const auto entry = registry_->Resolve( base_key.GetKey() );
        if ( !entry )
        {
            logger_->error( "{}: unregistered key={}", __func__, base_key.GetKey() );
            return outcome::failure( Error::UNREGISTERED_KEY );
        }

        auto instance = entry->make_instance();
        if ( !instance || !instance->DeserializeFromBytes( payload ) )
        {
            logger_->error( "{}: malformed payload rejected locally key={}", __func__, base_key.GetKey() );
            return outcome::failure( Error::MALFORMED_VALUE );
        }
        if ( !instance->Verify( payload ) )
        {
            logger_->error( "{}: semantically-invalid payload rejected locally key={}", __func__, base_key.GetKey() );
            return outcome::failure( Error::MALFORMED_VALUE );
        }

        auto put_result = db_->Put( base_key, sgns::base::Buffer( payload ), { topic_ } );
        if ( put_result.has_error() )
        {
            logger_->error( "{}: Put failed key={} error={}",
                            __func__,
                            base_key.GetKey(),
                            put_result.error().message() );
            return put_result.error();
        }

        logger_->debug( "{}: value proposed key={}", __func__, base_key.GetKey() );
        return outcome::success();
    }

    outcome::result<void> SecureCrdt::AddSignature( const sgns::crdt::HierarchicalKey &base_key,
                                                    const std::string                 &signer_address,
                                                    const std::vector<uint8_t>        &signature )
    {
        logger_->trace( "{}: entry key={} signer={}", __func__, base_key.GetKey(), signer_address );
        const auto entry = registry_->Resolve( base_key.GetKey() );
        if ( !entry )
        {
            logger_->error( "{}: unregistered key={}", __func__, base_key.GetKey() );
            return outcome::failure( Error::UNREGISTERED_KEY );
        }

        auto current_value = db_->Get( base_key );
        if ( current_value.has_error() )
        {
            logger_->error( "{}: no value proposed yet key={}", __func__, base_key.GetKey() );
            return outcome::failure( Error::NO_VALUE_PROPOSED );
        }

        const std::vector<uint8_t> payload = current_value.value().toVector();
        if ( !multisig::VerifyPayloadSignature( signer_address, signature, payload ) )
        {
            logger_->error( "{}: invalid signature rejected locally key={} signer={}",
                            __func__,
                            base_key.GetKey(),
                            signer_address );
            return outcome::failure( Error::INVALID_SIGNATURE );
        }

        auto put_result = db_->Put( base_key.ChildString( "sig" ).ChildString( signer_address ),
                                    sgns::base::Buffer( signature ),
                                    { topic_ } );
        if ( put_result.has_error() )
        {
            logger_->error( "{}: Put failed key={} error={}",
                            __func__,
                            base_key.GetKey(),
                            put_result.error().message() );
            return put_result.error();
        }

        logger_->debug( "{}: signature added key={} signer={}", __func__, base_key.GetKey(), signer_address );
        return outcome::success();
    }

    outcome::result<std::optional<sgns::base::Buffer>> SecureCrdt::ReadIfQuorum(
        const sgns::crdt::HierarchicalKey &base_key )
    {
        logger_->trace( "{}: entry key={}", __func__, base_key.GetKey() );
        const auto entry = registry_->Resolve( base_key.GetKey() );
        if ( !entry )
        {
            logger_->error( "{}: unregistered key={}", __func__, base_key.GetKey() );
            return outcome::failure( Error::UNREGISTERED_KEY );
        }

        auto current_value = db_->Get( base_key );
        if ( current_value.has_error() )
        {
            logger_->debug( "{}: no value yet key={}", __func__, base_key.GetKey() );
            return outcome::success( std::optional<sgns::base::Buffer>{} );
        }
        const std::vector<uint8_t> payload = current_value.value().toVector();

        auto                          sig_query = db_->QueryKeyValues( base_key.ChildString( "sig" ).GetKey() );
        multisig::CollectedSignatures collected_signatures;
        if ( !sig_query.has_error() )
        {
            for ( const auto &[key_buffer, value_buffer] : sig_query.value() )
            {
                const std::string key_string( key_buffer.toString() );
                const std::string address = LastKeySegment( key_string );
                if ( address.empty() )
                {
                    continue;
                }
                collected_signatures.emplace_back( address, value_buffer.toVector() );
            }
        }

        auto signer_set_result = entry->signer_set_source( base_key.GetKey() );
        if ( signer_set_result.has_error() )
        {
            logger_->error( "{}: signer_set_source failed key={}", __func__, base_key.GetKey() );
            return signer_set_result.error();
        }
        const auto &snapshot = signer_set_result.value();

        const multisig::MultiSig quorum( snapshot.signer_set, snapshot.required_signatures );
        if ( !quorum.IsValid() )
        {
            logger_->error( "{}: invalid quorum configuration key={} required={} authorized={}",
                            __func__,
                            base_key.GetKey(),
                            quorum.RequiredSignatures(),
                            quorum.AuthorizedSignerCount() );
            return outcome::success( std::optional<sgns::base::Buffer>{} );
        }

        const auto quorum_result = quorum.EvaluateQuorum( collected_signatures, payload );
        if ( !quorum_result.has_quorum )
        {
            logger_->debug( "{}: quorum not met key={} valid_unique_count={}",
                            __func__,
                            base_key.GetKey(),
                            quorum_result.valid_unique_count );
            return outcome::success( std::optional<sgns::base::Buffer>{} );
        }

        logger_->debug( "{}: quorum met key={} valid_unique_count={}",
                        __func__,
                        base_key.GetKey(),
                        quorum_result.valid_unique_count );
        return outcome::success( std::optional<sgns::base::Buffer>{ current_value.value() } );
    }

    outcome::result<CandidateId> SecureCrdt::SubmitCandidateApproval( const CandidateApprovalRecord &record )
    {
        const auto bytes = record.CanonicalBytes();
        const auto id    = CandidateId::FromCore( record.core );
        if ( !bytes || !id )
        {
            return outcome::failure( Error::MALFORMED_VALUE );
        }
        const CandidateKey key{ *id, record.signer };

        std::lock_guard<std::mutex> lock( candidate_write_mutex_ );
        auto                        validated = ValidateCandidateApproval( key.ToHierarchicalKey(), *bytes, true );
        if ( validated.has_error() )
        {
            return validated.error();
        }
        auto put = db_->Put( key.ToHierarchicalKey(), sgns::base::Buffer( *bytes ), { topic_ } );
        if ( put.has_error() )
        {
            return put.error();
        }
        return *id;
    }

    outcome::result<std::vector<CandidateApprovalRecord>> SecureCrdt::ReadCandidateApprovals( const CandidateId &id )
    {
        if ( !registry_->ResolveCandidateDomain( id.domain ) )
        {
            return outcome::failure( Error::UNREGISTERED_CANDIDATE_DOMAIN );
        }
        auto stored = QueryCandidateRecords( db_, id.domain );
        if ( stored.has_error() )
        {
            return stored.error();
        }
        std::vector<CandidateApprovalRecord> approvals;
        for ( auto &item : stored.value() )
        {
            if ( item.key.id == id )
            {
                approvals.push_back( std::move( item.record ) );
            }
        }
        std::sort( approvals.begin(),
                   approvals.end(),
                   []( const auto &left, const auto &right ) { return left.signer < right.signer; } );
        return approvals;
    }

    outcome::result<std::vector<CandidateId>> SecureCrdt::ListCandidates( const std::string &domain,
                                                                          const std::string &predecessor_hash,
                                                                          bool               current_only )
    {
        const auto domain_entry = registry_->ResolveCandidateDomain( domain );
        if ( !domain_entry )
        {
            return outcome::failure( Error::UNREGISTERED_CANDIDATE_DOMAIN );
        }
        if ( current_only )
        {
            auto authorization = domain_entry->authorization_source();
            if ( authorization.has_error() )
            {
                return authorization.error();
            }
            if ( authorization.value().expected_previous_hash != predecessor_hash )
            {
                return std::vector<CandidateId>{};
            }
        }

        auto stored = QueryCandidateRecords( db_, domain );
        if ( stored.has_error() )
        {
            return stored.error();
        }
        std::vector<CandidateId> candidates;
        for ( const auto &item : stored.value() )
        {
            if ( item.record.core.expected_previous_hash == predecessor_hash &&
                 std::find( candidates.begin(), candidates.end(), item.key.id ) == candidates.end() )
            {
                candidates.push_back( item.key.id );
            }
        }
        std::sort( candidates.begin(),
                   candidates.end(),
                   []( const auto &left, const auto &right )
                   {
                       return left.version == right.version ? left.content_hash < right.content_hash
                                                            : left.version < right.version;
                   } );
        return candidates;
    }

    bool SecureCrdt::RegisterCandidateCallback( const std::string &domain,
                                                CandidateCallback  callback,
                                                const void        *owner_token )
    {
        if ( !callback || !registry_->ResolveCandidateDomain( domain ) )
        {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock( candidate_callbacks_mutex_ );
            if ( candidate_callbacks_.count( domain ) != 0 )
            {
                return false;
            }
            candidate_callbacks_.emplace( domain, CandidateCallbackEntry{ std::move( callback ), owner_token } );
        }

        auto       weak_self  = weak_from_this();
        const bool registered = db_->RegisterNewElementCallback(
            CandidatePattern( domain ),
            [weak_self, domain]( sgns::crdt::CRDTCallbackManager::NewDataPair data, const std::string & )
            {
                if ( auto strong = weak_self.lock() )
                {
                    strong->OnCandidateApproval( domain, data );
                }
            } );
        if ( !registered )
        {
            std::lock_guard<std::mutex> lock( candidate_callbacks_mutex_ );
            candidate_callbacks_.erase( domain );
        }
        return registered;
    }

    void SecureCrdt::UnregisterCandidateCallbackIf( const std::string &domain, const void *owner_token )
    {
        bool removed = false;
        {
            std::lock_guard<std::mutex> lock( candidate_callbacks_mutex_ );
            const auto                  it = candidate_callbacks_.find( domain );
            if ( it != candidate_callbacks_.end() && it->second.owner_token == owner_token )
            {
                candidate_callbacks_.erase( it );
                removed = true;
            }
        }
        if ( removed )
        {
            db_->UnregisterNewElementCallback( CandidatePattern( domain ) );
        }
    }

    outcome::result<CandidateApprovalRecord> SecureCrdt::ValidateCandidateApproval(
        const sgns::crdt::HierarchicalKey &key_value,
        const std::vector<uint8_t>        &bytes,
        bool                               enforce_resources )
    {
        const auto key = CandidateKey::Parse( key_value );
        if ( !key )
        {
            return outcome::failure( Error::MALFORMED_VALUE );
        }
        const auto record = CandidateApprovalRecord::DecodeCanonical( bytes, *key );
        if ( !record )
        {
            return outcome::failure( Error::MALFORMED_VALUE );
        }
        const auto domain_entry = registry_->ResolveCandidateDomain( key->id.domain );
        if ( !domain_entry )
        {
            return outcome::failure( Error::UNREGISTERED_CANDIDATE_DOMAIN );
        }
        auto authorization = domain_entry->authorization_source();
        if ( authorization.has_error() )
        {
            return authorization.error();
        }
        const auto &current = authorization.value();
        if ( current.network_id != record->core.network_id || current.kind != record->core.kind ||
             domain_entry->kind != record->core.kind || current.next_version != record->core.version ||
             current.expected_previous_hash != record->core.expected_previous_hash ||
             current.authorizing_policy_hash != record->core.authorizing_policy_hash )
        {
            return outcome::failure( Error::CANDIDATE_CONTEXT_MISMATCH );
        }
        if ( std::find( current.authorized_signers.begin(), current.authorized_signers.end(), record->signer ) ==
             current.authorized_signers.end() )
        {
            return outcome::failure( Error::UNAUTHORIZED_CANDIDATE_SIGNER );
        }
        const auto core_bytes = record->core.CanonicalBytes();
        if ( !core_bytes || !multisig::VerifyPayloadSignature( record->signer, record->signature, *core_bytes ) )
        {
            return outcome::failure( Error::UNAUTHORIZED_CANDIDATE_SIGNER );
        }
        if ( !enforce_resources )
        {
            return *record;
        }

        auto stored = QueryCandidateRecords( db_, key->id.domain );
        if ( stored.has_error() )
        {
            return stored.error();
        }
        std::unordered_set<std::string> active_candidates;
        size_t                          approvals_for_candidate = 0;
        size_t                          active_approval_bytes   = 0;
        bool                            candidate_exists        = false;
        for ( const auto &item : stored.value() )
        {
            if ( item.record.core.expected_previous_hash != record->core.expected_previous_hash )
            {
                continue;
            }
            active_candidates.insert( item.key.id.content_hash );
            if ( !CandidateLimits::ApprovalBytesAllowed( active_approval_bytes, item.bytes ) )
            {
                return outcome::failure( Error::CANDIDATE_LIMIT_EXCEEDED );
            }
            active_approval_bytes += item.bytes;
            if ( item.key.id == key->id )
            {
                candidate_exists = true;
                ++approvals_for_candidate;
                if ( item.record.signer == record->signer )
                {
                    return outcome::failure( Error::DUPLICATE_CANDIDATE_APPROVAL );
                }
            }
        }
        if ( ( !candidate_exists && !CandidateLimits::CandidateCountAllowed( active_candidates.size() + 1 ) ) ||
             !CandidateLimits::ApprovalCountAllowed( approvals_for_candidate + 1 ) ||
             !CandidateLimits::ApprovalBytesAllowed( active_approval_bytes, bytes.size() ) )
        {
            return outcome::failure( Error::CANDIDATE_LIMIT_EXCEEDED );
        }
        return *record;
    }

    std::optional<std::vector<sgns::crdt::pb::Element>> SecureCrdt::FilterCandidateApproval(
        const sgns::crdt::pb::Element &element )
    {
        const std::vector<uint8_t> bytes( element.value().begin(), element.value().end() );
        auto validated = ValidateCandidateApproval( sgns::crdt::HierarchicalKey( element.key() ), bytes, true );
        if ( validated.has_error() )
        {
            return std::vector<sgns::crdt::pb::Element>{};
        }
        return std::nullopt;
    }

    void SecureCrdt::OnCandidateApproval( const std::string                                &domain,
                                          const std::pair<std::string, sgns::base::Buffer> &data )
    {
        auto validated = ValidateCandidateApproval( sgns::crdt::HierarchicalKey( data.first ),
                                                    data.second.toVector(),
                                                    false );
        if ( validated.has_error() )
        {
            return;
        }
        const auto id = CandidateId::FromCore( validated.value().core );
        if ( !id )
        {
            return;
        }
        CandidateCallback callback;
        {
            std::lock_guard<std::mutex> lock( candidate_callbacks_mutex_ );
            const auto                  it = candidate_callbacks_.find( domain );
            if ( it == candidate_callbacks_.end() )
            {
                return;
            }
            callback = it->second.callback;
        }
        callback( *id, validated.value() );
    }

    bool SecureCrdt::RegisterFilters()
    {
        logger_->trace( "{}: entry", __func__ );
        bool       all_registered = true;
        auto       weak_self      = weak_from_this();
        const auto entries        = registry_->AllEntries();
        for ( const auto &entry : entries )
        {
            const std::string pattern = "/?" + entry.key_pattern + "(/sig/[^/]+)?";
            const bool registered = db_->RegisterElementFilter(
                pattern,
                [weak_self, entry]( const sgns::crdt::pb::Element &element )
                    -> std::optional<std::vector<sgns::crdt::pb::Element>>
                {
                    if ( auto strong = weak_self.lock() )
                    {
                        return strong->FilterSecureCrdtUpdate( entry, element );
                    }
                    return std::nullopt;
                } );
            all_registered = all_registered && registered;
        }
        const auto candidate_domains = registry_->AllCandidateDomains();
        for ( const auto &entry : candidate_domains )
        {
            const bool registered = db_->RegisterElementFilter(
                CandidatePattern( entry.domain ),
                [weak_self](
                    const sgns::crdt::pb::Element &element ) -> std::optional<std::vector<sgns::crdt::pb::Element>>
                {
                    if ( auto strong = weak_self.lock() )
                    {
                        return strong->FilterCandidateApproval( element );
                    }
                    return std::vector<sgns::crdt::pb::Element>{};
                } );
            all_registered = all_registered && registered;
        }
        db_->AddListenTopic( topic_ );
        logger_->info( "{}: result={}", __func__, all_registered );
        return all_registered;
    }

    std::optional<std::vector<sgns::crdt::pb::Element>> SecureCrdt::FilterSecureCrdtUpdate(
        const SecureCrdtRegistryEntry &entry,
        const sgns::crdt::pb::Element &element )
    {
        logger_->trace( "{}: entry key={}", __func__, element.key() );
        std::vector<uint8_t>              element_bytes( element.value().begin(), element.value().end() );
        const sgns::crdt::HierarchicalKey element_key( element.key() );
        const auto                        key_segments = element_key.GetList();
        const bool is_signature = key_segments.size() >= 2 && key_segments[key_segments.size() - 2] == "sig";

        sgns::crdt::HierarchicalKey base_key = element_key;
        if ( is_signature )
        {
            const auto signature_suffix = element_key.GetKey().rfind( "/sig/" );
            base_key = sgns::crdt::HierarchicalKey( element_key.GetKey().substr( 0, signature_suffix ) );
        }

        if ( element_key == base_key )
        {
            auto instance = entry.make_instance();
            if ( !instance || !instance->DeserializeFromBytes( element_bytes ) || !instance->Verify( element_bytes ) )
            {
                logger_->error( "{}: malformed/invalid remote value rejected key={}", __func__, element.key() );
                return std::vector<sgns::crdt::pb::Element>{};
            }

            logger_->debug( "{}: remote value accepted key={}", __func__, element.key() );
            return std::nullopt;
        }

        const std::string address       = key_segments.back();
        auto              current_value = db_->Get( base_key );
        if ( current_value.has_error() )
        {
            logger_->error( "{}: no base value yet, rejecting signature key={}", __func__, element.key() );
            return std::vector<sgns::crdt::pb::Element>{};
        }
        const std::vector<uint8_t> payload = current_value.value().toVector();
        if ( !multisig::VerifyPayloadSignature( address, element_bytes, payload ) )
        {
            logger_->error( "{}: invalid remote signature rejected key={}", __func__, element.key() );
            return std::vector<sgns::crdt::pb::Element>{};
        }
        logger_->debug( "{}: remote signature accepted key={}", __func__, element.key() );
        return std::nullopt;
    }
} // namespace sgns::securecrdt
