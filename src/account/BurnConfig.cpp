/**
 * @file       BurnConfig.cpp
 * @brief      Implementation of the genesis-seeded, quorum-signed,
 *             cache-refresh-via-quorum-re-derivation `BURN_BASIS_POINTS`
 *             value (BURN-01, BURN-02, BURN-03).
 * @date       2026-07-24
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/BurnConfig.hpp"

#include <algorithm>
#include <charconv>
#include <system_error>

#include "account/GeniusAccount.hpp"
#include "securecrdt/QuorumThresholdValidation.hpp"

namespace sgns::account
{
    BurnConfigPayload::BurnConfigPayload( uint64_t basis_points ) : basis_points_( basis_points )
    {
    }

    std::vector<uint8_t> BurnConfigPayload::SerializeToBytes() const
    {
        const std::string encoded = std::to_string( basis_points_ );
        return std::vector<uint8_t>( encoded.begin(), encoded.end() );
    }

    bool BurnConfigPayload::DeserializeFromBytes( const std::vector<uint8_t> &bytes )
    {
        if ( bytes.empty() )
        {
            return false;
        }

        uint64_t   parsed = 0;
        const auto begin  = reinterpret_cast<const char *>( bytes.data() );
        const auto end    = begin + bytes.size();
        auto [ptr, ec]    = std::from_chars( begin, end, parsed );
        if ( ec != std::errc() || ptr != end )
        {
            return false;
        }

        basis_points_ = parsed;
        return true;
    }

    bool BurnConfigPayload::Verify( const std::vector<uint8_t> &payload ) const
    {
        if ( payload.empty() )
        {
            return false;
        }

        uint64_t   parsed = 0;
        const auto begin  = reinterpret_cast<const char *>( payload.data() );
        const auto end    = begin + payload.size();
        auto [ptr, ec]    = std::from_chars( begin, end, parsed );
        if ( ec != std::errc() || ptr != end )
        {
            return false;
        }

        return parsed <= BASIS_POINTS_TOTAL;
    }

    void BurnConfigPayload::Apply()
    {
        applied_ = true;
    }

    BurnConfig::BurnConfig( std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt,
                            std::shared_ptr<sgns::crdt::GlobalDB>                  db,
                            std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> trusted_peer_registry,
                            uint64_t                                                quorum_threshold,
                            std::shared_ptr<sgns::GeniusAccount>          account,
                            sgns::crdt::HierarchicalKey                             base_key ) :
        secure_crdt_( std::move( secure_crdt ) ),
        db_( std::move( db ) ),
        trusted_peer_registry_( std::move( trusted_peer_registry ) ),
        quorum_threshold_( quorum_threshold ),
        account_( std::move( account ) ),
        base_key_( std::move( base_key ) )
    {
    }

    BurnConfig::~BurnConfig()
    {
        Unregister();
    }

    outcome::result<std::shared_ptr<BurnConfig>> BurnConfig::New(
        std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt,
        std::shared_ptr<sgns::crdt::GlobalDB>                   db,
        std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> trusted_peer_registry,
        uint64_t                                                 quorum_threshold,
        std::shared_ptr<sgns::GeniusAccount>           account,
        sgns::crdt::HierarchicalKey                              base_key )
    {
        auto validation_result = sgns::securecrdt::ValidateQuorumThreshold(
            quorum_threshold, trusted_peer_registry->GetCurrentPeers().size() );
        if ( validation_result.has_error() )
        {
            return validation_result.error();
        }

        auto instance = std::make_shared<BurnConfig>( std::move( secure_crdt ), std::move( db ),
                                                       std::move( trusted_peer_registry ), quorum_threshold,
                                                       std::move( account ), std::move( base_key ) );
        if ( !instance->RegisterSignerSetSource() )
        {
            return outcome::failure( std::errc::file_exists );
        }
        instance->RegisterCrdtChangeCallback();
        instance->TrySeedGenesisIfEligible();

        // Seed the cache from whatever is synchronously confirmable right
        // now (BURN-03): falls back to GENESIS_DEFAULT_BASIS_POINTS (already
        // the member-initializer default) if nothing is confirmed yet.
        instance->OnCrdtElementChanged();

        return instance;
    }

    bool BurnConfig::RegisterSignerSetSource()
    {
        sgns::securecrdt::SecureCrdtRegistryEntry entry;
        entry.signer_set_source =
            [weak_self = weak_from_this()]( const std::string & ) -> outcome::result<sgns::securecrdt::SignerSetSnapshot>
        {
            auto self = weak_self.lock();
            if ( !self )
            {
                return sgns::securecrdt::SignerSetSnapshot{};
            }
            return sgns::securecrdt::SignerSetSnapshot{ self->trusted_peer_registry_->GetCurrentPeers(),
                                                        self->quorum_threshold_ };
        };
        entry.make_instance = []() -> std::shared_ptr<sgns::securecrdt::ISignedCRDTData>
        {
            return std::make_shared<BurnConfigPayload>();
        };
        entry.owner_token = &registry_token_;

        return secure_crdt_->Registry().Register( base_key_.GetKey(), std::move( entry ) );
    }

    void BurnConfig::RegisterCrdtChangeCallback()
    {
        const std::string pattern = "/?" + base_key_.GetKey() + "(/sig/.*)?";
        auto               weak_self = weak_from_this();
        db_->RegisterNewElementCallback( pattern,
                                         [weak_self]( sgns::crdt::CRDTCallbackManager::NewDataPair, const std::string & )
                                         {
                                             if ( auto self = weak_self.lock() )
                                             {
                                                 self->OnCrdtElementChanged();
                                             }
                                         } );
    }

    void BurnConfig::OnCrdtElementChanged()
    {
        auto read_result = secure_crdt_->ReadIfQuorum( base_key_ );
        if ( read_result.has_error() || !read_result.value().has_value() )
        {
            return;
        }

        const auto      bytes = read_result.value()->toVector();
        BurnConfigPayload payload;
        if ( !payload.DeserializeFromBytes( bytes ) || !payload.Verify( bytes ) )
        {
            logger_->error( "{}: confirmed value failed deserialize/verify", __func__ );
            return;
        }

        const uint64_t new_value = payload.GetBasisPoints();
        if ( new_value == cached_basis_points_.load( std::memory_order_relaxed ) )
        {
            return;
        }

        cached_basis_points_.store( new_value, std::memory_order_relaxed );

        std::vector<RefreshCallback> callbacks_copy;
        {
            std::lock_guard<std::mutex> lock( refresh_callbacks_mutex_ );
            callbacks_copy = refresh_callbacks_;
        }
        for ( const auto &cb : callbacks_copy )
        {
            cb( new_value );
        }

        logger_->info( "{}: cached basis points refreshed to {}", __func__, new_value );
    }

    void BurnConfig::TrySeedGenesisIfEligible()
    {
        auto read_result = secure_crdt_->ReadIfQuorum( base_key_ );
        if ( read_result.has_error() || read_result.value().has_value() )
        {
            // Either an error occurred, or a confirmed value already exists --
            // never auto-seed in either case.
            return;
        }

        const auto current_peers = trusted_peer_registry_->GetCurrentPeers();
        const auto self_address  = account_->GetAddress();
        const bool is_eligible =
            std::find( current_peers.begin(), current_peers.end(), self_address ) != current_peers.end();
        if ( !is_eligible )
        {
            return;
        }

        const BurnConfigPayload genesis_payload( GENESIS_DEFAULT_BASIS_POINTS );
        const auto               serialized = genesis_payload.SerializeToBytes();

        auto propose_result = secure_crdt_->ProposeValue( base_key_, serialized );
        if ( propose_result.has_error() )
        {
            logger_->error( "{}: ProposeValue failed", __func__ );
            return;
        }

        const auto signature_bytes = account_->Sign( serialized );
        auto sign_result = secure_crdt_->AddSignature( base_key_, self_address, signature_bytes );
        if ( sign_result.has_error() )
        {
            logger_->error( "{}: AddSignature failed", __func__ );
            return;
        }

        logger_->info( "{}: genesis burn-config default seeded ({} basis points)", __func__,
                       GENESIS_DEFAULT_BASIS_POINTS );
    }

    uint64_t BurnConfig::GetCachedBasisPoints() const
    {
        return cached_basis_points_.load( std::memory_order_relaxed );
    }

    void BurnConfig::RegisterRefreshCallback( RefreshCallback cb )
    {
        std::lock_guard<std::mutex> lock( refresh_callbacks_mutex_ );
        refresh_callbacks_.push_back( std::move( cb ) );
    }

    void BurnConfig::Unregister()
    {
        secure_crdt_->Registry().UnregisterIf( base_key_.GetKey(), &registry_token_ );
    }
} // namespace sgns::account
