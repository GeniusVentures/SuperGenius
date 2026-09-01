/**
 * @file       NetworkRegistry.cpp
 * @brief      Implementation of the SecureCRDT-backed per-privateNetworkId
 *             membership registry (D-03, D-06): TPR-majority bootstrap,
 *             cached self-governance, secret-free records.
 * @date       2026-09-01
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "networkregistry/NetworkRegistry.hpp"

#include <algorithm>
#include <system_error>
#include <unordered_set>

#include "base/hexutil.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "securecrdt/QuorumThresholdValidation.hpp"

namespace sgns::networkregistry
{
    namespace
    {
        /// @brief True for libp2p PeerId base58 multihash prefixes: identity-
        ///        hash ("Qm...") and Ed25519 ("12D3KooW...") peer ids.
        bool HasPeerIdMultihashPrefix( const std::string &entry )
        {
            return entry.rfind( "Qm", 0 ) == 0 || entry.rfind( "12D3KooW", 0 ) == 0;
        }

        bool IsLowerHex( const std::string &value )
        {
            if ( value.empty() || value.size() % 2 != 0 )
            {
                return false;
            }
            return std::all_of( value.begin(), value.end(), []( unsigned char c ) {
                return ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' );
            } );
        }

        std::vector<std::string> SplitLines( const std::string &raw )
        {
            std::vector<std::string> lines;
            size_t                   start = 0;
            while ( start <= raw.size() )
            {
                const auto pos = raw.find( '\n', start );
                if ( pos == std::string::npos )
                {
                    if ( start < raw.size() )
                    {
                        lines.push_back( raw.substr( start ) );
                    }
                    break;
                }
                lines.push_back( raw.substr( start, pos - start ) );
                start = pos + 1;
            }
            return lines;
        }

        /// @brief Escapes regex metacharacters so a per-network base key
        ///        containing them still registers as a literal pattern
        ///        (SecureCrdtRegistry::Register compiles the key as a regex).
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

        bool ParseCountLine( const std::string &line, const std::string &tag, size_t &count )
        {
            if ( line.rfind( tag + " ", 0 ) != 0 )
            {
                return false;
            }
            const std::string number = line.substr( tag.size() + 1 );
            if ( number.empty() || number.size() > 9 )
            {
                return false;
            }
            if ( !std::all_of( number.begin(), number.end(), []( unsigned char c ) { return c >= '0' && c <= '9'; } ) )
            {
                return false;
            }
            count = static_cast<size_t>( std::stoull( number ) );
            return true;
        }
    } // namespace

    //
    // NetworkMembershipPayload
    //
    // Wire layout (line-based, '\n'-delimited; PeerId base58 and hex address
    // characters never contain '\n'):
    //   SGNS-NETREG-1
    //   PEERS <n>
    //   SIGNERS <m>
    //   VERSION <v>
    //   FINGERPRINT <hex-or-empty>
    //   <peer 1..n>
    //   <signer 1..m>
    //
    NetworkMembershipPayload::NetworkMembershipPayload( std::vector<std::string> peers,
                                                        std::vector<std::string> signers,
                                                        uint32_t                pnet_key_version,
                                                        std::string             pnet_key_fingerprint ) :
        network_peers( std::move( peers ) ),
        network_signers( std::move( signers ) ),
        pnet_key_version( pnet_key_version ),
        pnet_key_fingerprint( std::move( pnet_key_fingerprint ) )
    {
    }

    std::vector<uint8_t> NetworkMembershipPayload::SerializeToBytes() const
    {
        std::string out;
        out.append( MAGIC.data(), MAGIC.size() );
        out += '\n';
        out += "PEERS " + std::to_string( network_peers.size() ) + '\n';
        out += "SIGNERS " + std::to_string( network_signers.size() ) + '\n';
        out += "VERSION " + std::to_string( pnet_key_version ) + '\n';
        out += "FINGERPRINT " + pnet_key_fingerprint + '\n';
        for ( const auto &peer : network_peers )
        {
            out += peer;
            out += '\n';
        }
        for ( const auto &signer : network_signers )
        {
            out += signer;
            out += '\n';
        }
        return std::vector<uint8_t>( out.begin(), out.end() );
    }

    std::optional<NetworkMembershipPayload> NetworkMembershipPayload::FromBytes( const std::vector<uint8_t> &bytes )
    {
        if ( bytes.empty() )
        {
            return std::nullopt;
        }

        const std::string raw( bytes.begin(), bytes.end() );
        if ( raw.rfind( std::string( MAGIC ) + "\n", 0 ) != 0 )
        {
            return std::nullopt;
        }

        const auto lines = SplitLines( raw.substr( MAGIC.size() + 1 ) );
        // 4 header lines + n peers + m signers
        if ( lines.size() < 4 )
        {
            return std::nullopt;
        }

        size_t peer_count   = 0;
        size_t signer_count = 0;
        if ( !ParseCountLine( lines[0], "PEERS", peer_count ) || !ParseCountLine( lines[1], "SIGNERS", signer_count ) )
        {
            return std::nullopt;
        }

        const std::string version_line = lines[2];
        if ( version_line.rfind( "VERSION ", 0 ) != 0 )
        {
            return std::nullopt;
        }
        const std::string version_number = version_line.substr( 8 );
        if ( version_number.empty() || version_number.size() > 9 ||
             !std::all_of( version_number.begin(),
                           version_number.end(),
                           []( unsigned char c ) { return c >= '0' && c <= '9'; } ) )
        {
            return std::nullopt;
        }

        const std::string fingerprint_line = lines[3];
        if ( fingerprint_line.rfind( "FINGERPRINT ", 0 ) != 0 )
        {
            return std::nullopt;
        }
        const std::string fingerprint = fingerprint_line.substr( 12 );

        if ( lines.size() != 4 + peer_count + signer_count )
        {
            return std::nullopt;
        }

        NetworkMembershipPayload payload;
        payload.network_peers.assign( lines.begin() + 4,
                                      lines.begin() + 4 + static_cast<std::ptrdiff_t>( peer_count ) );
        payload.network_signers.assign( lines.begin() + 4 + static_cast<std::ptrdiff_t>( peer_count ), lines.end() );
        payload.pnet_key_version      = static_cast<uint32_t>( std::stoull( version_number ) );
        payload.pnet_key_fingerprint = fingerprint;
        return payload;
    }

    bool NetworkMembershipPayload::DeserializeFromBytes( const std::vector<uint8_t> &bytes )
    {
        auto payload = FromBytes( bytes );
        if ( !payload )
        {
            return false;
        }
        *this = std::move( *payload );
        return true;
    }

    bool NetworkMembershipPayload::Verify( const std::vector<uint8_t> &payload ) const
    {
        if ( payload.empty() )
        {
            return false;
        }

        // Structural-only check: re-parse independently and validate shape --
        // never diff against cached/mutable state (TrustedPeerListPayload
        // convention).
        auto decoded = FromBytes( payload );
        if ( !decoded )
        {
            return false;
        }

        if ( decoded->network_peers.empty() )
        {
            return false;
        }

        std::unordered_set<std::string> unique_entries;
        for ( const auto &peer : decoded->network_peers )
        {
            if ( peer.empty() || !HasPeerIdMultihashPrefix( peer ) || !unique_entries.insert( peer ).second )
            {
                return false; // empty, non-PeerId, or duplicate entry
            }
        }
        for ( const auto &signer : decoded->network_signers )
        {
            if ( !sgns::base::IsHexAddress( signer ) || !unique_entries.insert( signer ).second )
            {
                return false; // malformed or duplicate signer address
            }
        }
        if ( decoded->pnet_key_version < 1 )
        {
            return false;
        }
        if ( !decoded->pnet_key_fingerprint.empty() && !IsLowerHex( decoded->pnet_key_fingerprint ) )
        {
            return false;
        }
        return true;
    }

    void NetworkMembershipPayload::Apply()
    {
        // Cache overwrite happens in NetworkRegistry::TryConfirm (the owning
        // registry), mirroring TrustedPeerListPayload's no-op Apply.
    }

    //
    // NetworkRegistry
    //
    NetworkRegistry::NetworkRegistry( std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt,
                                      std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> global_trusted_peers,
                                      std::string                                            private_network_id,
                                      std::vector<std::string>                               initial_network_peers,
                                      uint64_t                                               network_quorum_threshold,
                                      std::vector<std::string>                               initial_network_signers,
                                      std::string                                            pnet_key_fingerprint,
                                      sgns::crdt::HierarchicalKey                            base_key,
                                      std::shared_ptr<sgns::crdt::GlobalDB>                   global_db ) :
        secure_crdt_( std::move( secure_crdt ) ),
        global_trusted_peers_( std::move( global_trusted_peers ) ),
        base_key_( std::move( base_key ) ),
        network_quorum_threshold_( network_quorum_threshold ),
        cached_network_peers_( std::move( initial_network_peers ) ),
        cached_network_signers_( std::move( initial_network_signers ) ),
        pnet_key_fingerprint_( std::move( pnet_key_fingerprint ) ),
        private_network_id_( std::move( private_network_id ) ),
        global_db_( std::move( global_db ) )
    {
        // Snapshot the TPR bootstrap authority NOW (cached-only resolution
        // never consults the live TPR again -- Pitfall 9).
        tpr_bootstrap_peers_     = global_trusted_peers_->GetCurrentPeers();
        tpr_majority_threshold_  = sgns::securecrdt::StrictMajorityQuorumFloor( tpr_bootstrap_peers_.size() );
    }

    NetworkRegistry::~NetworkRegistry()
    {
        Unregister();
    }

    sgns::crdt::HierarchicalKey NetworkRegistry::DefaultBaseKey( const std::string &private_network_id )
    {
        return sgns::crdt::HierarchicalKey( "network-registry" ).ChildString( private_network_id );
    }

    outcome::result<std::shared_ptr<NetworkRegistry>> NetworkRegistry::New(
        std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt,
        std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> global_trusted_peers,
        std::string                                            private_network_id,
        std::vector<std::string>                               initial_network_peers,
        uint64_t                                               network_quorum_threshold,
        std::vector<std::string>                               initial_network_signers,
        std::string                                            pnet_key_fingerprint,
        std::shared_ptr<sgns::crdt::GlobalDB>                   global_db )
    {
        if ( !secure_crdt || !global_trusted_peers || private_network_id.empty() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        const auto tpr_peers = global_trusted_peers->GetCurrentPeers();
        // Floor check #1: the bootstrap threshold is the strict majority of
        // the TPR's CURRENT peer set (D-06) -- the bootstrap record confirms
        // only with a majority of the global trusted peers.
        const auto bootstrap_threshold = sgns::securecrdt::StrictMajorityQuorumFloor( tpr_peers.size() );
        auto       floor_result        = sgns::securecrdt::ValidateQuorumThreshold( bootstrap_threshold, tpr_peers.size() );
        if ( floor_result.has_error() )
        {
            return floor_result.error();
        }
        // Floor check #2: the self-governance quorum must clear the strict
        // majority floor over the network membership.
        floor_result = sgns::securecrdt::ValidateQuorumThreshold( network_quorum_threshold, initial_network_peers.size() );
        if ( floor_result.has_error() )
        {
            return floor_result.error();
        }
        // Floor check #2b: when member signers are provisioned, the
        // self-governance quorum must also be satisfiable by them.
        if ( !initial_network_signers.empty() )
        {
            floor_result = sgns::securecrdt::ValidateQuorumThreshold( network_quorum_threshold,
                                                                     initial_network_signers.size() );
            if ( floor_result.has_error() )
            {
                return floor_result.error();
            }
        }

        auto instance = std::make_shared<NetworkRegistry>( std::move( secure_crdt ),
                                                           std::move( global_trusted_peers ),
                                                           private_network_id,
                                                           std::move( initial_network_peers ),
                                                           network_quorum_threshold,
                                                           std::move( initial_network_signers ),
                                                           std::move( pnet_key_fingerprint ),
                                                           DefaultBaseKey( private_network_id ),
                                                           std::move( global_db ) );
        if ( !instance->RegisterSignerSetSource() )
        {
            return outcome::failure( std::errc::file_exists );
        }
        if ( instance->global_db_ )
        {
            instance->RegisterCrdtChangeCallback();
        }
        return instance;
    }

    bool NetworkRegistry::RegisterSignerSetSource()
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
        { return std::make_shared<NetworkMembershipPayload>(); };
        entry.owner_token = &registry_token_;
        // D-04: record the explicit per-key authority so Resolve() callers
        // can see which PeerRegistry owns this network's branch.
        entry.peer_registry = shared_from_this();

        return secure_crdt_->Registry().Register( EscapeRegex( base_key_.GetKey() ), std::move( entry ) );
    }

    void NetworkRegistry::RegisterCrdtChangeCallback()
    {
        // BurnConfig pattern: re-run TryConfirm whenever a base_key or
        // sig/<addr> child element arrives, so later quorum-signed membership
        // changes refresh the cache without an explicit TryConfirm call.
        change_callback_pattern_ = "/?" + EscapeRegex( base_key_.GetKey() ) + "(/sig/.*)?";
        auto weak_self           = weak_from_this();
        global_db_->RegisterNewElementCallback(
            change_callback_pattern_,
            [weak_self]( sgns::crdt::CRDTCallbackManager::NewDataPair, const std::string & )
            {
                if ( auto self = weak_self.lock() )
                {
                    auto confirmed = self->TryConfirm();
                    if ( confirmed.has_error() )
                    {
                        self->logger_->warn( "OnCrdtElementChanged: TryConfirm failed: {}",
                                             confirmed.error().message() );
                    }
                }
            } );
    }

    outcome::result<sgns::securecrdt::SignerSetSnapshot> NetworkRegistry::CurrentSignerSet() const
    {
        return ResolveSignerSet();
    }

    outcome::result<sgns::securecrdt::SignerSetSnapshot> NetworkRegistry::ResolveSignerSet() const
    {
        // Cached state ONLY -- NEVER ReadIfQuorum from here (Pitfall 9: this
        // runs inside SecureCrdt's verification flow).
        std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
        if ( !bootstrap_confirmed_ )
        {
            return sgns::securecrdt::SignerSetSnapshot{ tpr_bootstrap_peers_, tpr_majority_threshold_ };
        }
        return sgns::securecrdt::SignerSetSnapshot{ cached_network_signers_, network_quorum_threshold_ };
    }

    outcome::result<void> NetworkRegistry::SeedBootstrap( const std::vector<std::string> &initial_network_peers )
    {
        logger_->info( "{}: seeding bootstrap membership record ({} peers) for private network",
                       __func__,
                       initial_network_peers.size() );

        std::vector<std::string> signers;
        {
            std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
            signers = cached_network_signers_;
        }
        const NetworkMembershipPayload payload( initial_network_peers,
                                                std::move( signers ),
                                                1,
                                                pnet_key_fingerprint_ );
        // No self-signature: TPR member nodes add signatures through the
        // standard propose/sign flow until the TPR-majority quorum is met.
        return secure_crdt_->ProposeValue( base_key_, payload.SerializeToBytes() );
    }

    outcome::result<void> NetworkRegistry::ProposeMembershipChange( const std::vector<std::string> &new_peers,
                                                                    const std::vector<std::string> &new_signers )
    {
        logger_->info( "{}: proposing membership change ({} peers)", __func__, new_peers.size() );
        std::vector<std::string> signers = new_signers;
        if ( signers.empty() )
        {
            // Empty replacement would permanently fail-close self-governance:
            // keep the currently-cached signer list instead.
            std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
            signers = cached_network_signers_;
        }
        std::string fingerprint;
        {
            std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
            fingerprint = pnet_key_fingerprint_;
        }
        const NetworkMembershipPayload payload( new_peers, std::move( signers ), 1, std::move( fingerprint ) );
        return secure_crdt_->ProposeValue( base_key_, payload.SerializeToBytes() );
    }

    outcome::result<void> NetworkRegistry::SignMembershipChange( const std::string          &signer_address,
                                                                 const std::vector<uint8_t> &signature )
    {
        logger_->info( "{}: signing membership change (signer={})", __func__, signer_address );
        return secure_crdt_->AddSignature( base_key_,
                                           signer_address,
                                           std::vector<uint8_t>( signature.begin(), signature.end() ) );
    }

    outcome::result<bool> NetworkRegistry::TryConfirm()
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
        auto       payload = NetworkMembershipPayload::FromBytes( bytes );
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
            cached_network_peers_   = payload->GetNetworkPeers();
            cached_network_signers_ = payload->GetNetworkSigners();
            bootstrap_confirmed_    = true;
        }

        logger_->info( "{}: confirmed membership record ({} peers)", __func__, payload->GetNetworkPeers().size() );
        return outcome::success( true );
    }

    std::vector<std::string> NetworkRegistry::GetCurrentPeers() const
    {
        std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
        return cached_network_peers_;
    }

    bool NetworkRegistry::IsBootstrapConfirmed() const
    {
        std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
        return bootstrap_confirmed_;
    }

    void NetworkRegistry::Unregister()
    {
        if ( secure_crdt_ )
        {
            secure_crdt_->Registry().UnregisterIf( EscapeRegex( base_key_.GetKey() ), &registry_token_ );
        }
        if ( global_db_ && !change_callback_pattern_.empty() )
        {
            global_db_->UnregisterNewElementCallback( change_callback_pattern_ );
            change_callback_pattern_.clear();
        }
    }
} // namespace sgns::networkregistry
