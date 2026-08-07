/**
 * @file       TrustedPeerRegistry.cpp
 * @brief      Implementation of the genesis-seeded, quorum-updatable
 *             trusted-peer set (TPR-01, TPR-02, TPR-03).
 * @date       2026-07-24
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "trustedpeer/TrustedPeerRegistry.hpp"

#include <system_error>
#include <unordered_set>

#include "base/hexutil.hpp"
#include "securecrdt/QuorumThresholdValidation.hpp"

namespace sgns::trustedpeer
{
    TrustedPeerListPayload::TrustedPeerListPayload( std::vector<std::string> peers ) : peers_( std::move( peers ) )
    {
    }

    std::vector<uint8_t> TrustedPeerListPayload::SerializeToBytes() const
    {
        std::string joined;
        for ( size_t i = 0; i < peers_.size(); ++i )
        {
            if ( i > 0 )
            {
                joined.push_back( '\n' );
            }
            joined += peers_[i];
        }
        return std::vector<uint8_t>( joined.begin(), joined.end() );
    }

    std::optional<TrustedPeerListPayload> TrustedPeerListPayload::FromBytes( const std::vector<uint8_t> &bytes )
    {
        if ( bytes.empty() )
        {
            return std::nullopt;
        }

        std::string              raw( bytes.begin(), bytes.end() );
        std::vector<std::string> parsed;
        size_t                   start = 0;
        while ( start <= raw.size() )
        {
            const auto pos = raw.find( '\n', start );
            if ( pos == std::string::npos )
            {
                parsed.push_back( raw.substr( start ) );
                break;
            }
            parsed.push_back( raw.substr( start, pos - start ) );
            start = pos + 1;
        }

        return TrustedPeerListPayload( std::move( parsed ) );
    }

    bool TrustedPeerListPayload::DeserializeFromBytes( const std::vector<uint8_t> &bytes )
    {
        auto payload = FromBytes( bytes );
        if ( !payload )
        {
            return false;
        }

        peers_ = std::move( payload->peers_ );
        return true;
    }

    bool TrustedPeerListPayload::Verify( const std::vector<uint8_t> &payload ) const
    {
        if ( payload.empty() )
        {
            return false;
        }

        std::string              raw( payload.begin(), payload.end() );
        std::vector<std::string> entries;
        size_t                   start = 0;
        while ( start <= raw.size() )
        {
            const auto pos = raw.find( '\n', start );
            if ( pos == std::string::npos )
            {
                entries.push_back( raw.substr( start ) );
                break;
            }
            entries.push_back( raw.substr( start, pos - start ) );
            start = pos + 1;
        }

        if ( entries.empty() )
        {
            return false;
        }

        std::unordered_set<std::string> unique_entries;
        for ( const auto &entry : entries )
        {
            if ( !sgns::base::IsHexAddress( entry ) )
            {
                return false;
            }
            if ( !unique_entries.insert( entry ).second )
            {
                return false; // duplicate entry
            }
        }

        return true;
    }

    void TrustedPeerListPayload::Apply()
    {
    }

    TrustedPeerRegistry::TrustedPeerRegistry( std::shared_ptr<sgns::securecrdt::SecureCrdt> secure_crdt,
                                              std::vector<std::string>                      genesis_peers,
                                              std::string                                   bootstrapper_address,
                                              uint64_t                                      quorum_threshold,
                                              sgns::crdt::HierarchicalKey                   base_key ) :
        secure_crdt_( std::move( secure_crdt ) ),
        base_key_( std::move( base_key ) ),
        bootstrapper_address_( std::move( bootstrapper_address ) ),
        quorum_threshold_( quorum_threshold ),
        cached_peers_( std::move( genesis_peers ) )
    {
    }

    TrustedPeerRegistry::~TrustedPeerRegistry()
    {
        Unregister();
    }

    outcome::result<std::shared_ptr<TrustedPeerRegistry>> TrustedPeerRegistry::New(
        std::shared_ptr<sgns::securecrdt::SecureCrdt> secure_crdt,
        std::vector<std::string>                      genesis_peers,
        std::string                                   bootstrapper_address,
        uint64_t                                      quorum_threshold,
        sgns::crdt::HierarchicalKey                   base_key )
    {
        auto validation_result = sgns::securecrdt::ValidateQuorumThreshold( quorum_threshold, genesis_peers.size() );
        if ( validation_result.has_error() )
        {
            return validation_result.error();
        }

        auto instance = std::make_shared<TrustedPeerRegistry>( std::move( secure_crdt ),
                                                               std::move( genesis_peers ),
                                                               std::move( bootstrapper_address ),
                                                               quorum_threshold,
                                                               std::move( base_key ) );
        instance->RegisterSignerSetSource();
        return instance;
    }

    void TrustedPeerRegistry::RegisterSignerSetSource()
    {
        sgns::securecrdt::SecureCrdtRegistryEntry entry;
        entry.signer_set_source = [weak_self = weak_from_this()](
                                      const std::string & ) -> outcome::result<sgns::securecrdt::SignerSetSnapshot>
        {
            auto self = weak_self.lock();
            if ( !self )
            {
                return sgns::securecrdt::SignerSetSnapshot{};
            }
            return self->ResolveSignerSet();
        };
        entry.make_instance = []() -> std::shared_ptr<sgns::securecrdt::ISignedCRDTData>
        { return std::make_shared<TrustedPeerListPayload>(); };
        entry.owner_token = &registry_token_;

        sgns::securecrdt::SecureCrdtRegistry::Register( base_key_.GetKey(), entry );
    }

    outcome::result<sgns::securecrdt::SignerSetSnapshot> TrustedPeerRegistry::ResolveSignerSet() const
    {
        std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
        if ( !genesis_confirmed_ )
        {
            return sgns::securecrdt::SignerSetSnapshot{ { bootstrapper_address_ }, 1 };
        }
        return sgns::securecrdt::SignerSetSnapshot{ cached_peers_, quorum_threshold_ };
    }

    outcome::result<void> TrustedPeerRegistry::SeedGenesis( const std::vector<std::string> &genesis_peers,
                                                            const std::vector<uint8_t>     &ephemeral_signature )
    {
        logger_->info( "{}: seeding genesis trusted-peer list ({} peers)", __func__, genesis_peers.size() );

        TrustedPeerListPayload payload( genesis_peers );
        auto                   propose_result = secure_crdt_->ProposeValue( base_key_, payload.SerializeToBytes() );
        if ( propose_result.has_error() )
        {
            logger_->error( "{}: ProposeValue failed", __func__ );
            return propose_result.error();
        }

        auto sign_result = secure_crdt_->AddSignature( base_key_, bootstrapper_address_, ephemeral_signature );
        if ( sign_result.has_error() )
        {
            logger_->error( "{}: AddSignature failed", __func__ );
            return sign_result.error();
        }

        return outcome::success();
    }

    outcome::result<void> TrustedPeerRegistry::ProposeMembershipChange( const std::vector<std::string> &new_peers )
    {
        logger_->info( "{}: proposing membership change ({} peers)", __func__, new_peers.size() );
        TrustedPeerListPayload payload( new_peers );
        return secure_crdt_->ProposeValue( base_key_, payload.SerializeToBytes() );
    }

    outcome::result<void> TrustedPeerRegistry::SignMembershipChange( const std::string          &signer_address,
                                                                     const std::vector<uint8_t> &signature )
    {
        logger_->info( "{}: signing membership change (signer={})", __func__, signer_address );
        return secure_crdt_->AddSignature( base_key_, signer_address, signature );
    }

    outcome::result<bool> TrustedPeerRegistry::TryConfirm()
    {
        auto read_result = secure_crdt_->ReadIfQuorum( base_key_ );
        if ( read_result.has_error() )
        {
            return read_result.error();
        }

        if ( !read_result.value().has_value() )
        {
            return outcome::success( false );
        }

        const auto bytes   = read_result.value()->toVector();
        auto       payload = TrustedPeerListPayload::FromBytes( bytes );
        if ( !payload )
        {
            logger_->error( "{}: confirmed value failed to deserialize", __func__ );
            return outcome::failure( std::errc::bad_message );
        }
        if ( !payload->Verify( bytes ) )
        {
            logger_->error( "{}: confirmed value failed structural verification", __func__ );
            return outcome::failure( std::errc::bad_message );
        }
        payload->Apply();

        {
            std::unique_lock<std::shared_mutex> lock( cache_mutex_ );
            cached_peers_      = payload->GetPeers();
            genesis_confirmed_ = true;
        }

        logger_->info( "{}: confirmed trusted-peer set ({} peers)", __func__, payload->GetPeers().size() );
        return outcome::success( true );
    }

    std::vector<std::string> TrustedPeerRegistry::GetCurrentPeers() const
    {
        std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
        return cached_peers_;
    }

    bool TrustedPeerRegistry::IsGenesisConfirmed() const
    {
        std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
        return genesis_confirmed_;
    }

    void TrustedPeerRegistry::Unregister()
    {
        sgns::securecrdt::SecureCrdtRegistry::UnregisterIf( base_key_.GetKey(), &registry_token_ );
    }
} // namespace sgns::trustedpeer
