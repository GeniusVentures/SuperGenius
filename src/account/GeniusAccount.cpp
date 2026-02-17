#include "GeniusAccount.hpp"

#include <limits>
#include <nil/crypto3/algebra/marshalling.hpp>
#include <nil/crypto3/pubkey/algorithm/sign.hpp>
#include <nil/crypto3/pubkey/algorithm/verify.hpp>
#include <openssl/rand.h>
#include <random>
#include "WalletCore/Hash.h"
#include "base/hexutil.hpp"
#include "local_secure_storage/ISecureStorage.hpp"
#include "outcome/outcome.hpp"
#include "singleton/CComponentFactory.hpp"
#include "WalletCore/PrivateKey.h"
#include "ipfs_pubsub/gossip_pubsub.hpp"
#include "account/AccountMessenger.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "local_secure_storage/SecureStorage.hpp"
#include "primitives/cid/cid.hpp"

namespace
{
    using namespace sgns;

    std::array<uint8_t, 32> get_elgamal_pubkey()
    {
        const auto              elgamal_key = KeyGenerator::ElGamal( 0x1234_cppui256 ).GetPublicKey().public_key_value;
        std::array<uint8_t, 32> exported;
        export_bits( elgamal_key, exported.begin(), 8, false );

        return exported;
    }

    base::Logger genius_account_logger()
    {
        // Always call base::createLogger to get the current logger
        // This will return existing logger or create new one as needed
        return base::createLogger( "GeniusAccount" );
    }

    boost::filesystem::path SetupStoragePath( const boost::filesystem::path &base_path )
    {
        constexpr std::string_view FILE_NAME = "secure_storage_id";
        boost::filesystem::create_directories( base_path );
        return boost::filesystem::canonical( boost::filesystem::absolute( base_path ) ) / FILE_NAME;
    }

    // Helper to create storage and key generators from key_seed
    outcome::result<
        std::pair<std::shared_ptr<ISecureStorage>, std::pair<KeyGenerator::ElGamal, ethereum::EthereumKeyGenerator>>>
    CreateStorageAndKeys( nil::crypto3::multiprecision::uint256_t key_seed )
    {
        constexpr std::string_view PREFIX = "SGNS";

        ethereum::EthereumKeyGenerator eth_key( key_seed );
        auto                           pub_key = eth_key.GetEntirePubValue();
        OUTCOME_TRY( std::vector<uint8_t> vec, base::unhex( pub_key ) );

        auto storage = std::make_shared<SecureStorageImpl>( std::string( PREFIX ) +
                                                            libp2p::multi::detail::encodeBase58( vec ) );

        return std::make_pair( std::move( storage ),
                               std::make_pair( KeyGenerator::ElGamal( std::move( key_seed ) ), eth_key ) );
    }
}

namespace sgns
{
    const std::array<uint8_t, 32> GeniusAccount::ELGAMAL_PUBKEY_PREDEFINED = get_elgamal_pubkey();

    std::shared_ptr<GeniusAccount> GeniusAccount::CreateInstanceFromResponse(
        TokenID token_id,
        std::pair<std::shared_ptr<ISecureStorage>, std::pair<KeyGenerator::ElGamal, ethereum::EthereumKeyGenerator>>
             response_value,
        bool full_node )
    {
        auto [storage, addresses]           = std::move( response_value );
        auto [elgamal_address, eth_address] = std::move( addresses );

        auto instance = std::shared_ptr<GeniusAccount>(
            new GeniusAccount( token_id, std::move( storage ), full_node ) );
        instance->eth_keypair_     = std::make_shared<ethereum::EthereumKeyGenerator>( std::move( eth_address ) );
        instance->elgamal_address_ = std::make_shared<KeyGenerator::ElGamal>( std::move( elgamal_address ) );

        return instance;
    }

    // Refactored New methods
    std::shared_ptr<GeniusAccount> GeniusAccount::New( TokenID                        token_id,
                                                       const char                    *eth_private_key,
                                                       const boost::filesystem::path &base_path,
                                                       bool                           full_node )
    {
        if ( auto response = LoadGeniusAccount( base_path ); response.has_value() )
        {
            genius_account_logger()->debug( "Loaded existing Genius address" );
            return CreateInstanceFromResponse( token_id, std::move( response.value() ), full_node );
        }

        genius_account_logger()->info(
            "Could not load Genius address from storage, attempting to generate from ethereum private key" );
        auto response = GenerateGeniusAddress( eth_private_key, base_path );
        if ( response.has_error() )
        {
            genius_account_logger()->error( "Failed to generate Genius address from private key" );
            return nullptr;
        }

        genius_account_logger()->debug( "Generated a Genius address from private key" );
        return CreateInstanceFromResponse( token_id, std::move( response.value() ), full_node );
    }

    std::shared_ptr<GeniusAccount> GeniusAccount::New( TokenID                        token_id,
                                                       const Credentials             &credentials,
                                                       const boost::filesystem::path &base_path,
                                                       bool                           full_node )
    {
        if ( auto response = LoadGeniusAccount( base_path ); response.has_value() )
        {
            genius_account_logger()->debug( "Loaded existing Genius address" );
            return CreateInstanceFromResponse( token_id, std::move( response.value() ), full_node );
        }

        genius_account_logger()->info(
            "Could not load Genius address from storage, attempting to generate from credentials" );
        auto response = GenerateGeniusAddress( credentials, base_path );
        if ( response.has_error() )
        {
            genius_account_logger()->error( "Failed to generate Genius address from credentials" );
            return nullptr;
        }

        genius_account_logger()->debug( "Generated a Genius address from credentials" );
        // Save credentials to storage
        auto &[storage, addresses] = response.value();
        storage->Save( "email", credentials.email );
        storage->Save( "password", credentials.password );

        return CreateInstanceFromResponse( token_id, std::move( response.value() ), full_node );
    }

    std::shared_ptr<GeniusAccount> GeniusAccount::New( TokenID                        token_id,
                                                       const boost::filesystem::path &base_path,
                                                       bool                           full_node )
    {
        static std::string_view SUFFIX = "@gnus.ai";
        static std::mt19937_64  eng( ( std::random_device() )() );

        if ( auto response = LoadGeniusAccount( base_path ); response.has_value() )
        {
            genius_account_logger()->debug( "Loaded existing Genius address" );
            return CreateInstanceFromResponse( token_id, std::move( response.value() ), full_node );
        }

        genius_account_logger()->info( "Could not find existing Genius address, generating one with random credentials" );

        std::uniform_int_distribution<uint64_t> dist( 0, std::numeric_limits<uint64_t>::max() );
        uint64_t                                num   = dist( eng );
        std::string                             email = base::hex_lower(
            gsl::span<const uint8_t>( reinterpret_cast<uint8_t *>( &num ), sizeof( num ) ) );
        email.append( SUFFIX );

        num                  = dist( eng );
        std::string password = base::hex_lower(
            gsl::span<const uint8_t>( reinterpret_cast<uint8_t *>( &num ), sizeof( num ) ) );

        return New( token_id, { std::move( email ), std::move( password ) }, base_path, full_node );
    }

    // Refactored LoadGeniusAccount (mostly unchanged, but uses helper for path)
    outcome::result<
        std::pair<std::shared_ptr<ISecureStorage>, std::pair<KeyGenerator::ElGamal, ethereum::EthereumKeyGenerator>>>
    GeniusAccount::LoadGeniusAccount( const boost::filesystem::path &base_path )
    {
        constexpr std::string_view PREFIX = "SGNS";

        auto file_path = SetupStoragePath( base_path );
        genius_account_logger()->info( "Secure storage ID path: {}", file_path.string() );

        std::ifstream file( file_path.string() );
        if ( !file.is_open() )
        {
            genius_account_logger()->debug( "Secure storage ID file does not exist" );
            return std::errc::no_such_file_or_directory;
        }

        std::string public_key;
        file >> public_key;
        genius_account_logger()->info( "Loaded public key from file: {} (length: {})",
                                       public_key.substr( 0, 16 ) + "...",
                                       public_key.length() );

        OUTCOME_TRY( std::vector<uint8_t> vec, base::unhex( public_key ) );

        auto storage = std::make_shared<SecureStorageImpl>( std::string( PREFIX ) +
                                                            libp2p::multi::detail::encodeBase58( vec ) );

        auto load_res = storage->Load( "sgns_key" );
        if ( !load_res )
        {
            genius_account_logger()->warn( "Could not load sgns_key from secure storage" );
            return std::errc::no_such_file_or_directory;
        }

        auto key_seed = nil::crypto3::multiprecision::uint256_t( load_res.value() );
        genius_account_logger()->info( "Successfully loaded key_seed from storage" );

        // Validate loaded key_seed matches stored public key
        if ( ethereum::EthereumKeyGenerator( key_seed ).GetEntirePubValue() != public_key )
        {
            genius_account_logger()->error( "Validation failed: key_seed does not match stored public key" );
            return std::errc::bad_message;
        }

        genius_account_logger()->info( "Validation successful: key_seed matches stored public key" );
        return CreateStorageAndKeys( key_seed );
    }

    // Refactored GenerateGeniusAddress (credentials version)
    outcome::result<
        std::pair<std::shared_ptr<ISecureStorage>, std::pair<KeyGenerator::ElGamal, ethereum::EthereumKeyGenerator>>>
    GeniusAccount::GenerateGeniusAddress( const Credentials &credentials, const boost::filesystem::path &base_path )
    {
        genius_account_logger()->trace( "Key seed from credentials" );

        if ( credentials.email.empty() || credentials.password.empty() )
        {
            return std::errc::invalid_argument;
        }

        std::string s      = credentials.email + credentials.password;
        auto        hashed = TW::Hash::sha256( s );
        auto        hexed  = base::hex_lower( hashed );

        return GenerateGeniusAddress( hexed.data(), base_path );
    }

    // Refactored GenerateGeniusAddress (private key version)
    outcome::result<
        std::pair<std::shared_ptr<ISecureStorage>, std::pair<KeyGenerator::ElGamal, ethereum::EthereumKeyGenerator>>>
    GeniusAccount::GenerateGeniusAddress( const char *eth_private_key, const boost::filesystem::path &base_path )
    {
        genius_account_logger()->trace( "Key seed from ethereum private key" );

        if ( eth_private_key == nullptr )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        OUTCOME_TRY( auto as_vec, base::unhex( eth_private_key ) );
        auto signed_secret = TW::PrivateKey( as_vec ).sign(
            TW::Data( ELGAMAL_PUBKEY_PREDEFINED.cbegin(), ELGAMAL_PUBKEY_PREDEFINED.cend() ),
            TWCurveSECP256k1 );

        if ( signed_secret.empty() )
        {
            genius_account_logger()->error( "Cannot sign secret" );
            return outcome::failure( std::errc::invalid_argument );
        }

        auto key_seed = nil::crypto3::multiprecision::uint256_t( TW::Hash::sha256( signed_secret ) );

        // Create storage and keys
        OUTCOME_TRY( auto result, CreateStorageAndKeys( key_seed ) );

        // Save public key to file
        auto file_path = SetupStoragePath( base_path );
        genius_account_logger()->info( "Secure storage ID path: {}", file_path.string() );

        std::ofstream out_file( file_path.string() );
        if ( !out_file.is_open() )
        {
            return outcome::failure( std::errc::bad_file_descriptor );
        }

        auto &[storage, addresses] = result;
        auto &[elgamal, eth_key]   = addresses;
        out_file << eth_key.GetEntirePubValue() << std::endl;

        return result;
    }

    bool GeniusAccount::InitMessenger( std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub )
    {
        bool                               ret = false;
        AccountMessenger::InterfaceMethods methods;
        methods.sign_ =
            [weakptr( weak_from_this() )]( const std::vector<uint8_t> &data ) -> outcome::result<std::vector<uint8_t>>
        {
            if ( auto self = weakptr.lock() )
            {
                return self->Sign( data );
            }

            return outcome::failure( std::errc::owner_dead );
        };
        methods.verify_signature_ = [weakptr( weak_from_this() )](
                                        const std::string          &address,
                                        std::string_view            sig,
                                        const std::vector<uint8_t> &data ) -> outcome::result<bool>
        { return VerifySignature( address, sig, data ); };
        methods.get_local_nonce_ =
            [weakptr( weak_from_this() )]( const std::string &address ) -> outcome::result<uint64_t>
        {
            if ( auto self = weakptr.lock() )
            {
                return self->GetPeerNonce( address );
            }

            return outcome::failure( std::errc::owner_dead );
        };
        methods.get_block_cid_ = [weakptr( weak_from_this() )](
                                     uint8_t            block_index,
                                     const std::string &address ) -> outcome::result<std::string>
        {
            if ( auto self = weakptr.lock() )
            {
                std::lock_guard lock( self->get_cids_mutex_ );
                if ( self->get_cids_method_ )
                {
                    return self->get_cids_method_( block_index, address );
                }

                return outcome::failure( AccountMessenger::Error::GENESIS_REQUEST_ERROR );
            }

            return outcome::failure( std::errc::owner_dead );
        };
        methods.has_block_cid_ = [weakptr( weak_from_this() )]( const std::string &cid ) -> outcome::result<bool>
        {
            if ( auto self = weakptr.lock() )
            {
                std::lock_guard lock( self->get_cids_mutex_ );
                if ( self->has_cid_method_ )
                {
                    return self->has_cid_method_( cid );
                }

                return outcome::failure( AccountMessenger::Error::GENESIS_REQUEST_ERROR );
            }

            return outcome::failure( std::errc::owner_dead );
        };
        messenger_ = AccountMessenger::New( eth_keypair_->GetEntirePubValue(),
                                            std::move( pubsub ),
                                            std::move( methods ) );

        if ( messenger_ )
        {
            genius_account_logger()->debug( "Created AccountMessenger" );
            ret = true;
        }
        return ret;
    }

    bool GeniusAccount::ConfigureMessengerHandlers( std::shared_ptr<crdt::GlobalDB> global_db )
    {
        bool ret = false;
        if ( messenger_ )
        {
            messenger_->RegisterBlockResponseHandler(
                [weakptr{ std::weak_ptr<crdt::PubSubBroadcasterExt>( global_db->GetBroadcaster() ) }](
                    const std::string &cid,
                    const std::string &peer_id,
                    const std::string &address )
                {
                    if ( auto strong = weakptr.lock() )
                    {
                        return strong->AddSingleCIDInfo( cid, peer_id, address );
                    }
                    return false;
                } );

            messenger_->RegisterHeadRequestHandler(
                [weak_globaldb = std::weak_ptr<crdt::GlobalDB>( global_db )]( const std::set<std::string> &topics )
                {
                    if ( auto globaldb = weak_globaldb.lock() )
                    {
                        auto result = globaldb->RequestHeadBroadcast( topics );
                        if ( result.has_error() )
                        {
                            auto logger = base::createLogger( "GeniusAccount" );
                            logger->error( "Failed to request head broadcast for {} topics", topics.size() );
                        }
                    }
                } );

            SetHasBlockCidMethod(
                [weakptr{ std::weak_ptr<crdt::PubSubBroadcasterExt>( global_db->GetBroadcaster() ) }](
                    const std::string &cid ) -> outcome::result<bool>
                {
                    if ( auto strong = weakptr.lock() )
                    {
                        auto cid_result = CID::fromString( cid );
                        if ( cid_result.has_error() )
                        {
                            return outcome::failure( std::errc::invalid_argument );
                        }
                        auto dag_syncer = std::static_pointer_cast<crdt::GraphsyncDAGSyncer>( strong->GetDagSyncer() );
                        if ( !dag_syncer )
                        {
                            return outcome::failure( std::errc::no_such_device );
                        }
                        auto has_block = dag_syncer->HasBlock( cid_result.value() );
                        if ( has_block.has_error() )
                        {
                            return outcome::failure( has_block.error() );
                        }
                        return has_block.value();
                    }
                    return outcome::failure( std::errc::owner_dead );
                } );
            genius_account_logger()->debug( "Registered block response handler" );
            ret = true;
        }
        return ret;
    }

    GeniusAccount::GeniusAccount( TokenID token_id, std::shared_ptr<ISecureStorage> storage, bool full_node ) :
        token( token_id ),
        storage_( std::move( storage ) ),
        is_full_node_( full_node ),
        nonce_request_in_progress_( false ),
        cached_nonce_timestamp_( std::chrono::steady_clock::time_point{} )
    {
    }

    GeniusAccount::~GeniusAccount() {}

    std::string GeniusAccount::GetAddress() const
    {
        return eth_keypair_->GetEntirePubValue();
    }

    TokenID GeniusAccount::GetToken() const
    {
        return token;
    }

    bool GeniusAccount::VerifySignature( const std::string          &address,
                                         std::string_view            sig,
                                         const std::vector<uint8_t> &data )
    {
        bool ret = false;

        do
        {
            if ( sig.size() != SIGNATURE_EXP_SIZE )
            {
                genius_account_logger()->error( "Incorrect signature size {}, expected ",
                                                sig.size(),
                                                SIGNATURE_EXP_SIZE );
                break;
            }
            std::vector<uint8_t> vec_sig( sig.cbegin(), sig.cend() );

            std::array<uint8_t, 32> hashed = nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( data );

            auto [r_success, r] =
                nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_from_bytes(
                    vec_sig.cbegin(),
                    vec_sig.cbegin() + 32 );

            if ( !r_success )
            {
                break;
            }
            auto [s_success, s] =
                nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_from_bytes(
                    vec_sig.cbegin() + 32,
                    vec_sig.cbegin() + 64 );

            if ( !s_success )
            {
                break;
            }
            ethereum::signature_type sig( r, s );
            auto                     eth_pubkey = ethereum::EthereumKeyGenerator::BuildPublicKey( address );
            ret                                 = nil::crypto3::verify( hashed, sig, eth_pubkey );
        } while ( 0 );

        return ret;
    }

    std::vector<uint8_t> GeniusAccount::Sign( const std::vector<uint8_t> &data ) const
    {
        std::array<uint8_t, 32> hashed = nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( data );

        ethereum::signature_type  signature = nil::crypto3::sign( hashed, eth_keypair_->get_private_key() );
        std::vector<std::uint8_t> signed_vector( SIGNATURE_EXP_SIZE );

        nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( std::get<0>( signature ),
                                                  signed_vector.begin(),
                                                  signed_vector.begin() + 32 );
        nil::marshalling::bincode::field<ecdsa_t::scalar_field_type>::field_element_to_bytes<
            std::vector<std::uint8_t>::iterator>( std::get<1>( signature ),
                                                  signed_vector.begin() + 32,
                                                  signed_vector.end() );

        return signed_vector;
    }

    void GeniusAccount::SetLocalConfirmedNonce( uint64_t nonce )
    {
        genius_account_logger()->debug( "Setting local confirmed nonce to {}", nonce );
        SetPeerConfirmedNonce( nonce, eth_keypair_->GetEntirePubValue() );
        std::lock_guard lock( nonce_mutex_ );
    }

    void GeniusAccount::SetPeerConfirmedNonce( uint64_t nonce, const std::string &address )
    {
        std::lock_guard lock( nonce_mutex_ );
        auto            current_confirmed_nonce = confirmed_nonces_[address];
        genius_account_logger()->debug( "Setting the max value between {} and {} as a confirmed nonce for address {}",
                                        current_confirmed_nonce,
                                        nonce,
                                        address.substr( 0, 8 ) );
        auto updated_nonce         = std::max( nonce, current_confirmed_nonce );
        confirmed_nonces_[address] = updated_nonce;

        if ( address == eth_keypair_->GetEntirePubValue() )
        {
            if ( !local_confirmed_nonce_ || updated_nonce > local_confirmed_nonce_.value() )
            {
                local_confirmed_nonce_ = updated_nonce;
            }
            auto it = pending_nonces_.begin();
            while ( it != pending_nonces_.end() &&
                    ( !local_confirmed_nonce_ || *it <= local_confirmed_nonce_.value() ) )
            {
                it = pending_nonces_.erase( it );
            }
        }
    }

    void GeniusAccount::RollBackPeerConfirmedNonce( uint64_t nonce, const std::string &address )
    {
        std::lock_guard lock( nonce_mutex_ );
        auto            it                      = confirmed_nonces_.find( address );
        uint64_t        current_confirmed_nonce = 0;
        if ( it != confirmed_nonces_.end() )
        {
            current_confirmed_nonce = it->second;
        }
        genius_account_logger()->debug( "Rolling back nonce {} for address {} (current confirmed {})",
                                        nonce,
                                        address.substr( 0, 8 ),
                                        current_confirmed_nonce );
        if ( it != confirmed_nonces_.end() && nonce == current_confirmed_nonce )
        {
            if ( current_confirmed_nonce > 0 )
            {
                it->second = current_confirmed_nonce - 1;
            }
            else
            {
                confirmed_nonces_.erase( it );
            }
        }

        if ( address == eth_keypair_->GetEntirePubValue() )
        {
            if ( local_confirmed_nonce_.has_value() && ( nonce == local_confirmed_nonce_.value() ) )
            {
                if ( local_confirmed_nonce_.value() > 0 )
                {
                    local_confirmed_nonce_ = local_confirmed_nonce_.value() - 1;
                }
                else
                {
                    local_confirmed_nonce_.reset();
                }
            }
            pending_nonces_.erase( nonce );
        }
    }

    uint64_t GeniusAccount::GetNextNonceLocked() const
    {
        uint64_t next = local_confirmed_nonce_.has_value() ? local_confirmed_nonce_.value() + 1 : 0;
        while ( pending_nonces_.count( next ) != 0 )
        {
            ++next;
        }
        return next;
    }

    uint64_t GeniusAccount::GetProposedNonce() const
    {
        std::shared_lock lock( nonce_mutex_ );
        return GetNextNonceLocked();
    }

    uint64_t GeniusAccount::ReserveNextNonce()
    {
        std::lock_guard lock( nonce_mutex_ );
        auto            nonce = GetNextNonceLocked();
        pending_nonces_.insert( nonce );
        return nonce;
    }

    void GeniusAccount::ReleaseNonce( uint64_t nonce )
    {
        std::lock_guard lock( nonce_mutex_ );
        pending_nonces_.erase( nonce );
    }

    outcome::result<uint64_t> GeniusAccount::GetPeerNonce( const std::string &address ) const
    {
        std::unordered_map<std::string, uint64_t> nonces_copy;
        {
            std::shared_lock lock( nonce_mutex_ );
            nonces_copy = confirmed_nonces_;
        }
        if ( auto it = nonces_copy.find( address ); it != nonces_copy.end() )
        {
            return it->second;
        }

        return outcome::failure( std::errc::invalid_argument );
    }

    outcome::result<uint64_t> GeniusAccount::GetLocalConfirmedNonce() const
    {
        return GetPeerNonce( eth_keypair_->GetEntirePubValue() );
    }

    outcome::result<uint64_t> GeniusAccount::GetConfirmedNonce( uint64_t timeout_ms ) const
    {
        if ( !messenger_ )
        {
            return outcome::failure( std::errc::no_such_device );
        }
        std::unique_lock<std::mutex> lock( nonce_request_mutex_ );

        // Check if we have a fresh cached result (within 5 seconds)
        if ( cached_nonce_result_.has_value() )
        {
            auto now          = std::chrono::steady_clock::now();
            auto cache_age_ms = std::chrono::duration_cast<std::chrono::milliseconds>( now - cached_nonce_timestamp_ )
                                    .count();

            if ( cache_age_ms < NONCE_CACHE_DURATION_MS )
            {
                genius_account_logger()->debug( "Returning cached nonce result (age: {} ms)", cache_age_ms );
                return cached_nonce_result_.value();
            }
            genius_account_logger()->debug( "Cached nonce expired (age: {} ms), fetching fresh nonce", cache_age_ms );
        }

        // If a request is already in progress, wait for it
        if ( nonce_request_in_progress_ )
        {
            genius_account_logger()->debug( "Nonce request already in progress, waiting for result..." );

            // Wait for the in-progress request to complete
            nonce_request_cv_.wait( lock, [this]() { return !nonce_request_in_progress_; } );

            // Return the cached result if available
            if ( cached_nonce_result_.has_value() )
            {
                genius_account_logger()->debug( "Returning cached nonce result from completed request" );
                return cached_nonce_result_.value();
            }
        }

        // Mark that we're starting a request
        nonce_request_in_progress_ = true;
        cached_nonce_result_.reset();

        // Release lock while making the network call
        lock.unlock();

        genius_account_logger()->info( "Requesting nonce from the network with timeout {} ms", timeout_ms );

        auto latest_nonce_result = messenger_->GetLatestNonce( timeout_ms );

        outcome::result<uint64_t> result = outcome::failure( std::errc::io_error );
        if ( latest_nonce_result.has_value() )
        {
            result = latest_nonce_result.value();
            genius_account_logger()->debug( "Nonce replied with value {}", result.value() );
        }
        else if ( latest_nonce_result.error() == AccountMessenger::Error::NO_RESPONSE_RECEIVED )
        {
            genius_account_logger()->debug( "Network didn't answer nonce request" );
            result = latest_nonce_result;
        }
        else if ( latest_nonce_result.error() == AccountMessenger::Error::RESPONSE_WITHOUT_NONCE )
        {
            genius_account_logger()->debug( "No nonce information on the network, get local data" );
            result = GetLocalConfirmedNonce();
        }
        else
        {
            result = latest_nonce_result;
        }

        // Re-acquire lock to update state
        lock.lock();
        nonce_request_in_progress_ = false;

        // Only cache successful results
        if ( result.has_value() )
        {
            cached_nonce_result_    = result;
            cached_nonce_timestamp_ = std::chrono::steady_clock::now();
            genius_account_logger()->debug( "Cached successful nonce result: {}", result.value() );
        }
        else
        {
            genius_account_logger()->debug( "Not caching failed nonce request" );
        }

        // Notify all waiting threads
        lock.unlock();
        nonce_request_cv_.notify_all();

        return result;
    }

    outcome::result<void> GeniusAccount::RequestGenesis(
        uint64_t                                            timeout_ms,
        std::function<void( outcome::result<std::string> )> callback ) const
    {
        if ( !messenger_ )
        {
            return outcome::failure( std::errc::no_such_device );
        }
        genius_account_logger()->debug( "Requesting Genesis block from the network" );

        return messenger_->RequestGenesis( timeout_ms, std::move( callback ) );
    }

    outcome::result<void> GeniusAccount::RequestAccountCreation(
        uint64_t                                            timeout_ms,
        std::function<void( outcome::result<std::string> )> callback ) const
    {
        if ( !messenger_ )
        {
            return outcome::failure( std::errc::no_such_device );
        }
        genius_account_logger()->debug( "Requesting Genesis block from the network" );

        return messenger_->RequestAccountCreation( timeout_ms, std::move( callback ) );
    }

    outcome::result<void> GeniusAccount::RequestHeads( const std::unordered_set<std::string> &topics ) const
    {
        if ( !messenger_ )
        {
            return outcome::failure( std::errc::no_such_device );
        }
        genius_account_logger()->debug( "Requesting heads broadcast for {} topics", topics.size() );

        return messenger_->RequestHeads( topics );
    }

    outcome::result<void> GeniusAccount::RequestRegularBlock(
        uint64_t                                            timeout_ms,
        const std::string                                  &cid,
        std::function<void( outcome::result<std::string> )> callback ) const
    {
        if ( !messenger_ )
        {
            return outcome::failure( std::errc::no_such_device );
        }
        genius_account_logger()->debug( "Requesting block by CID {}", cid );

        return messenger_->RequestRegularBlock( timeout_ms, cid, std::move( callback ) );
    }

    void GeniusAccount::SetGetBlockChainCIDMethod(
        std::function<outcome::result<std::string>( uint8_t, const std::string & )> method )
    {
        std::lock_guard lock( get_cids_mutex_ );
        get_cids_method_ = std::move( method );
    }

    void GeniusAccount::ClearGetBlockChainCIDMethod( void )
    {
        std::lock_guard lock( get_cids_mutex_ );
        get_cids_method_ = nullptr;
    }

    void GeniusAccount::SetHasBlockCidMethod( std::function<outcome::result<bool>( const std::string & )> method )
    {
        std::lock_guard lock( get_cids_mutex_ );
        has_cid_method_ = std::move( method );
    }

    void GeniusAccount::ClearHasBlockCidMethod( void )
    {
        std::lock_guard lock( get_cids_mutex_ );
        has_cid_method_ = nullptr;
    }
}
