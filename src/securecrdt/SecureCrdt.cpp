/**
 * @file       SecureCrdt.cpp
 * @brief      Implementation of the SecureCrdt local-write gate (D-03) and
 *             reader-side quorum re-derivation (D-04).
 * @date       2026-07-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "securecrdt/SecureCrdt.hpp"

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
    } // namespace

    SecureCrdt::SecureCrdt( std::shared_ptr<sgns::crdt::GlobalDB> db, std::string topic ) :
        db_( std::move( db ) ), topic_( std::move( topic ) )
    {
    }

    outcome::result<void> SecureCrdt::ProposeValue( const sgns::crdt::HierarchicalKey &base_key,
                                                    const std::vector<uint8_t>        &payload )
    {
        logger_->trace( "{}: entry key={}", __func__, base_key.GetKey() );
        const auto entry = SecureCrdtRegistry::Resolve( base_key.GetKey() );
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
            logger_->error( "{}: semantically-invalid payload rejected locally key={}", __func__,
                            base_key.GetKey() );
            return outcome::failure( Error::MALFORMED_VALUE );
        }

        auto put_result = db_->Put( base_key, sgns::base::Buffer( payload ), { topic_ } );
        if ( put_result.has_error() )
        {
            logger_->error( "{}: Put failed key={} error={}", __func__, base_key.GetKey(),
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
        const auto entry = SecureCrdtRegistry::Resolve( base_key.GetKey() );
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
            logger_->error( "{}: invalid signature rejected locally key={} signer={}", __func__,
                            base_key.GetKey(), signer_address );
            return outcome::failure( Error::INVALID_SIGNATURE );
        }

        auto put_result = db_->Put( base_key.ChildString( "sig" ).ChildString( signer_address ),
                                    sgns::base::Buffer( signature ), { topic_ } );
        if ( put_result.has_error() )
        {
            logger_->error( "{}: Put failed key={} error={}", __func__, base_key.GetKey(),
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
        const auto entry = SecureCrdtRegistry::Resolve( base_key.GetKey() );
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

        auto sig_query = db_->QueryKeyValues( base_key.ChildString( "sig" ).GetKey() );
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
            logger_->debug( "{}: quorum not met key={} valid_unique_count={}", __func__, base_key.GetKey(),
                            quorum_result.valid_unique_count );
            return outcome::success( std::optional<sgns::base::Buffer>{} );
        }

        logger_->debug( "{}: quorum met key={} valid_unique_count={}", __func__, base_key.GetKey(),
                        quorum_result.valid_unique_count );
        return outcome::success( std::optional<sgns::base::Buffer>{ current_value.value() } );
    }

    bool SecureCrdt::RegisterFilters()
    {
        logger_->trace( "{}: entry", __func__ );
        bool         all_registered = true;
        auto         weak_self      = weak_from_this();
        const auto   entries        = SecureCrdtRegistry::AllEntries();
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
