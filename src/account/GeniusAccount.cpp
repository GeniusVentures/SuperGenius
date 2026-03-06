#include "GeniusAccount.hpp"

#include <nil/crypto3/algebra/marshalling.hpp>
#include <nil/crypto3/pubkey/algorithm/sign.hpp>
#include <nil/crypto3/pubkey/algorithm/verify.hpp>
#include <openssl/rand.h>
#include <WalletCore/Hash.h>
#include <WalletCore/PrivateKey.h>
#include <ipfs_pubsub/gossip_pubsub.hpp>

#include "base/hexutil.hpp"
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include "local_secure_storage/SecureStorage.hpp"
#include "outcome/outcome.hpp"
#include "account/AccountMessenger.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
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

    outcome::result<std::shared_ptr<ISecureStorage>>

    LoadSecureStorage( const boost::filesystem::path &base_path, const std::vector<uint8_t> &public_key )
    {
        constexpr std::string_view PREFIX = "SGNS";

        auto secure_storage          = std::make_shared<SecureStorageImpl>( std::string( PREFIX ) +
                                                                   libp2p::multi::detail::encodeBase58( public_key ) );
        auto current_secure_storage_ = secure_storage->LoadJSON();
        if ( current_secure_storage_.has_value() &&
             ( ( current_secure_storage_.value().IsArray() && !current_secure_storage_.value().Empty() ) ||
               ( current_secure_storage_.value().IsObject() && !current_secure_storage_.value().ObjectEmpty() ) ) )
        {
            genius_account_logger()->debug( "Secure storage already migrated, returning it" );
            return secure_storage;
        }

        auto json_storage_ = std::make_shared<JSONSecureStorage>( base_path.generic_string() );
        auto old_json      = json_storage_->LoadJSON();

        if ( old_json.has_error() )
        {
            if ( old_json.error() == std::errc::no_such_file_or_directory )
            {
                genius_account_logger()->debug( "There were no legacy JSON storage file to migrate from" );
            }
            else
            {
                genius_account_logger()->error( "Could not load legacy JSON storage at {}: {}",
                                                base_path.c_str(),
                                                old_json.error().message() );
            }
            return secure_storage;
        }

        auto maybe_field = old_json.value().FindMember( "GeniusAccount" );
        if ( maybe_field == old_json.value().MemberEnd() || !maybe_field->value.IsObject() )
        {
            genius_account_logger()->error( "Failed to find GeniusAccount member in old JSON storage" );
            return secure_storage;
        }

        rj::Document new_doc;
        new_doc.CopyFrom( maybe_field->value, new_doc.GetAllocator() );

        if ( auto res = secure_storage->SaveJSON( std::move( new_doc ) ); res.has_error() )
        {
            genius_account_logger()->error( "Failed to migrate JSON secure storage" );
            return res.error();
        }

        genius_account_logger()->debug( "Sucessfully migrated JSON secure storage" );

        return secure_storage;
    }
}

namespace sgns
{

    base::Logger genius_account_logger()
    {
        // Always call base::createLogger to get the current logger
        // This will return existing logger or create new one as needed
        return base::createLogger( "GeniusAccount" );
    }

    const std::array<uint8_t, 32> GeniusAccount::ELGAMAL_PUBKEY_PREDEFINED = get_elgamal_pubkey();

    std::shared_ptr<GeniusAccount> GeniusAccount::New( TokenID                 token_id,
                                                       const char             *eth_private_key,
                                                       boost::filesystem::path base_path,
                                                       bool                    full_node )
    {
        std::shared_ptr<GeniusAccount> instance;
        genius_account_logger()->set_level( spdlog::level::trace );
        if ( auto response = GenerateGeniusAddress( eth_private_key, base_path ); response.has_value() )
        {
            auto [storage, maybe_address]                 = response.value();
            auto [temp_elgamal_address, temp_eth_address] = maybe_address;
            genius_account_logger()->debug( "Generated a Genius Address from private key" );

            instance = std::shared_ptr<GeniusAccount>(
                new GeniusAccount( std::move( token_id ), std::move( storage ), full_node ) );

            instance->eth_keypair_ = std::make_shared<ethereum::EthereumKeyGenerator>( std::move( temp_eth_address ) );
            instance->elgamal_address_ = std::make_shared<KeyGenerator::ElGamal>( std::move( temp_elgamal_address ) );
        }

        return instance;
    }

    bool GeniusAccount::InitMessenger( std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub )
    {
        bool                               ret = false;
        AccountMessenger::InterfaceMethods methods;
        methods.sign_ =
            [weakptr( weak_from_this() )]( std::vector<uint8_t> data ) -> outcome::result<std::vector<uint8_t>>
        {
            if ( auto self = weakptr.lock() )
            {
                return self->Sign( std::move( data ) );
            }

            return outcome::failure( std::errc::owner_dead );
        };
        methods.verify_signature_ = [weakptr( weak_from_this() )]( std::string          address,
                                                                   std::string          sig,
                                                                   std::vector<uint8_t> data ) -> outcome::result<bool>
        {
            if ( auto self = weakptr.lock() )
            {
                return self->VerifySignature( std::move( address ), std::move( sig ), std::move( data ) );
            }

            return outcome::failure( std::errc::owner_dead );
        };
        methods.get_local_nonce_ = [weakptr( weak_from_this() )]( std::string address ) -> outcome::result<uint64_t>
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

    bool GeniusAccount::VerifySignature( std::string address, std::string sig, std::vector<uint8_t> data )
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

    std::vector<uint8_t> GeniusAccount::Sign( std::vector<uint8_t> data )
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

    outcome::result<
        std::pair<std::shared_ptr<ISecureStorage>, std::pair<KeyGenerator::ElGamal, ethereum::EthereumKeyGenerator>>>
    GeniusAccount::GenerateGeniusAddress( const char *eth_private_key, boost::filesystem::path base_path )
    {
        constexpr std::string_view PREFIX    = "SGNS";
        constexpr std::string_view FILE_NAME = "secure_storage_id";

        // Convert to absolute path to handle relative paths properly
        auto base_directory = boost::filesystem::absolute( base_path );

        boost::filesystem::create_directories( base_directory );

        // Use canonical() after directory exists to get fully normalized path
        base_directory = boost::filesystem::canonical( base_directory );
        auto storage_id_path = base_directory / FILE_NAME;

        genius_account_logger()->info( "Secure storage ID path: {}", storage_id_path.string() );

        // Try to load existing storage
        std::shared_ptr<ISecureStorage>         storage;
        nil::crypto3::multiprecision::uint256_t key_seed;
        bool                                    key_seed_loaded = false;

        if ( std::ifstream file( storage_id_path.string() ); file.is_open() )
        {
            std::string public_key;
            file >> public_key;
            genius_account_logger()->info( "Loaded public key from file: {} (length: {})",
                                           public_key.substr( 0, 16 ) + "...",
                                           public_key.length() );

            OUTCOME_TRY( std::vector<uint8_t> vec, base::unhex( public_key ) );

            genius_account_logger()->info( "Unhexed public key vector size: {}", vec.size() );

            // Create storage using the public key from the file
            OUTCOME_TRY( auto loaded_storage, LoadSecureStorage( base_directory, vec ) );
            storage = std::move( loaded_storage );

            if ( auto load_res = storage->Load( "sgns_key" ) )
            {
                genius_account_logger()->info( "Successfully loaded key_seed from storage" );
                key_seed = nil::crypto3::multiprecision::uint256_t( load_res.value() );

                // Validate that the loaded key_seed produces the same public key
                ethereum::EthereumKeyGenerator temp_eth_key( key_seed );
                auto                           regenerated_pub_key = temp_eth_key.GetEntirePubValue();

                if ( regenerated_pub_key == public_key )
                {
                    genius_account_logger()->info( "Validation successful: key_seed matches stored public key" );
                    key_seed_loaded = true;
                }
                else
                {
                    genius_account_logger()->error( "Validation failed: key_seed does not match stored public key" );
                    genius_account_logger()->error( "Expected: {}", public_key.substr( 0, 16 ) + "..." );
                    genius_account_logger()->error( "Got: {}", regenerated_pub_key.substr( 0, 16 ) + "..." );
                    // Don't set key_seed_loaded, will regenerate
                }
            }
            else
            {
                genius_account_logger()->warn( "Could not load sgns_key from secure storage, will regenerate" );
            }
        }
        else
        {
            genius_account_logger()->debug( "Secure storage ID file does not exist, trying legacy secure_storage.json" );

            auto legacy_storage = std::make_shared<JSONSecureStorage>( base_directory.generic_string() + "/" );
            if ( auto legacy_seed_res = legacy_storage->Load( "sgns_key" ) )
            {
                key_seed = nil::crypto3::multiprecision::uint256_t( legacy_seed_res.value() );

                ethereum::EthereumKeyGenerator temp_eth_key( key_seed );
                auto                           pub_key = temp_eth_key.GetEntirePubValue();

                OUTCOME_TRY( std::vector<uint8_t> vec, base::unhex( pub_key ) );
                OUTCOME_TRY( auto loaded_storage, LoadSecureStorage( base_directory, vec ) );
                storage = std::move( loaded_storage );

                if ( auto load_res = storage->Load( "sgns_key" ) )
                {
                    key_seed        = nil::crypto3::multiprecision::uint256_t( load_res.value() );
                    key_seed_loaded = true;
                    genius_account_logger()->info( "Loaded key seed from legacy secure storage migration" );

                    std::ofstream out_file( storage_id_path.string() );
                    if ( out_file.is_open() )
                    {
                        out_file << pub_key << std::endl;
                    }
                    else
                    {
                        genius_account_logger()->warn( "Could not write secure_storage_id after legacy migration" );
                    }
                }
                else
                {
                    genius_account_logger()->warn( "Legacy seed found but could not be loaded from secure storage" );
                }
            }
        }

        // Generate key_seed from ethereum private key if not loaded
        if ( !key_seed_loaded )
        {
            genius_account_logger()->trace( "Key seed from ethereum private key" );
            if ( eth_private_key == nullptr )
            {
                return outcome::failure( std::errc::invalid_argument );
            }

            OUTCOME_TRY( auto as_vec, base::unhex( eth_private_key ) );
            TW::PrivateKey private_key( as_vec );

            auto signed_secret = private_key.sign(
                TW::Data( ELGAMAL_PUBKEY_PREDEFINED.cbegin(), ELGAMAL_PUBKEY_PREDEFINED.cend() ),
                TWCurveSECP256k1 );

            if ( signed_secret.empty() )
            {
                genius_account_logger()->error( "Cannot sign secret" );
                return outcome::failure( std::errc::invalid_argument );
            }

            key_seed = nil::crypto3::multiprecision::uint256_t( TW::Hash::sha256( signed_secret ) );

            // Create storage with loaded key
            ethereum::EthereumKeyGenerator temp_eth_key( key_seed );
            auto                           pub_key = temp_eth_key.GetEntirePubValue();
            OUTCOME_TRY( std::vector<uint8_t> vec, base::unhex( pub_key ) );
            storage = std::make_shared<SecureStorageImpl>( std::string( PREFIX ) +
                                                           libp2p::multi::detail::encodeBase58( vec ) );

            BOOST_OUTCOME_TRYV2( auto &&, storage->Save( "sgns_key", key_seed.str() ) );

            // Write public key to file
            std::ofstream out_file( storage_id_path.string() );
            if ( !out_file.is_open() )
            {
                return outcome::failure( std::errc::bad_file_descriptor );
            }
            out_file << pub_key << std::endl;
        }

        KeyGenerator::ElGamal          elgamal_key( key_seed );
        ethereum::EthereumKeyGenerator eth_key( key_seed );
        auto                           pub_key = eth_key.GetEntirePubValue();

        return std::make_pair( storage, std::make_pair( elgamal_key, eth_key ) );
    }

    void GeniusAccount::SetLocalConfirmedNonce( uint64_t nonce )
    {
        genius_account_logger()->debug( "Setting local confirmed nonce to {}", nonce );
        SetPeerConfirmedNonce( nonce, eth_keypair_->GetEntirePubValue() );
        std::lock_guard lock( nonce_mutex_ );
    }

    void GeniusAccount::SetPeerConfirmedNonce( uint64_t nonce, std::string address )
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

    void GeniusAccount::RollBackPeerConfirmedNonce( uint64_t nonce, std::string address )
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
        while ( pending_nonces_.count( next ) )
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

    outcome::result<uint64_t> GeniusAccount::GetPeerNonce( std::string address ) const
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

        auto latest_nonce_result = messenger_->GetLatestNonce( std::move( timeout_ms ) );

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

    outcome::result<void> GeniusAccount::RequestHeads( const std::set<std::string> &topics ) const
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
        get_cids_method_ = method;
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
