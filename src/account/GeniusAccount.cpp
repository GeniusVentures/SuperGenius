// Keep these include files here to prevent errors within crypto3's headers
#include <nil/crypto3/algebra/marshalling.hpp>
#include <nil/crypto3/pubkey/algorithm/sign.hpp>
#include <nil/crypto3/pubkey/algorithm/verify.hpp>

#include "GeniusAccount.hpp"

#include <TrustWalletCore/TWCoinType.h>
#include <TrustWalletCore/TWDerivation.h>
#include <WalletCore/Coin.h>
#include <WalletCore/HDWallet.h>
#include <WalletCore/Hash.h>
#include <WalletCore/PrivateKey.h>
#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <string>

#include "base/hexutil.hpp"
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include "local_secure_storage/SecureStorage.hpp"
#include "outcome/outcome.hpp"
#include "account/AccountMessenger.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "primitives/cid/cid.hpp"

constexpr std::string_view SECURE_STORAGE_PREFIX = "SGNS";

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

    outcome::result<std::shared_ptr<ISecureStorage>> MigrateSecureStorage( const boost::filesystem::path &base_path )
    {
        JSONSecureStorage json_storage( base_path.generic_string() );
        auto              old_json = json_storage.LoadJSON();

        if ( old_json.has_error() )
        {
            if ( old_json.error() == std::errc::no_such_file_or_directory )
            {
                genius_account_logger()->debug( "There were no legacy JSON storage file to migrate from" );
                return std::errc::no_such_file_or_directory;
            }
            genius_account_logger()->error( "Could not load legacy JSON storage at {}: {}",
                                            base_path.generic_string(),
                                            old_json.error().message() );
            return std::errc::bad_file_descriptor;
        }

        auto maybe_field = old_json.value().FindMember( "GeniusAccount" );
        if ( maybe_field == old_json.value().MemberEnd() || !maybe_field->value.IsObject() )
        {
            genius_account_logger()->error( "Failed to find GeniusAccount member in old JSON storage" );
            return std::errc::bad_message;
        }

        auto maybe_key = maybe_field->value.FindMember( "sgns_key" );
        if ( maybe_key == maybe_field->value.MemberEnd() || !maybe_key->value.IsString() )
        {
            genius_account_logger()->error( "Failed to find GeniusAccount public key in old JSON storage" );
            return std::errc::bad_message;
        }

        // Create storage and keys
        nil::crypto3::multiprecision::uint256_t key_seed( maybe_key->value.GetString() );
        ethereum::EthereumKeyGenerator          eth_key( key_seed );
        auto                                    pub_key = eth_key.GetEntirePubValue();
        BOOST_OUTCOME_TRY( std::vector<uint8_t> pub_key_vec, base::unhex( pub_key ) );

        auto secure_storage = std::make_shared<SecureStorageImpl>( std::string( SECURE_STORAGE_PREFIX ) +
                                                                   libp2p::multi::detail::encodeBase58( pub_key_vec ) );

        auto          path = SetupStoragePath( base_path );
        std::ofstream file( path.c_str() );
        file << pub_key << std::endl;
        file.close();

        rj::Document new_doc;
        new_doc.CopyFrom( maybe_field->value, new_doc.GetAllocator() );
        BOOST_OUTCOME_TRY( secure_storage->SaveJSON( std::move( new_doc ) ) );
        genius_account_logger()->debug( "Successfully migrated JSON secure storage" );

        return secure_storage;
    }
}

namespace sgns
{
    const std::array<uint8_t, 32> GeniusAccount::ELGAMAL_PUBKEY_PREDEFINED = get_elgamal_pubkey();

    std::shared_ptr<GeniusAccount> GeniusAccount::CreateInstanceFromResponse( TokenID            token_id,
                                                                              StorageWithAddress response_value,
                                                                              bool               full_node )
    {
        auto [storage, addresses]           = std::move( response_value );
        auto [elgamal_address, eth_address] = std::move( addresses );

        auto instance = std::shared_ptr<GeniusAccount>(
            new GeniusAccount( std::make_shared<ethereum::EthereumKeyGenerator>( std::move( eth_address ) ),
                               token_id,
                               std::move( storage ),
                               full_node ) );
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

    std::shared_ptr<GeniusAccount> GeniusAccount::NewFromMnemonic( TokenID                        token_id,
                                                                   const std::string             &mnemonic,
                                                                   const boost::filesystem::path &base_path,
                                                                   bool                           full_node )
    {
        if ( auto response = LoadGeniusAccount( base_path ); response.has_value() )
        {
            genius_account_logger()->debug( "Loaded existing Genius address" );
            return CreateInstanceFromResponse( token_id, std::move( response.value() ), full_node );
        }

        genius_account_logger()->info(
            "Could not load Genius address from storage, attempting to generate from mnemonic" );

        try
        {
            TW::HDWallet   wallet( mnemonic, "", true );
            auto           derivation_path = TW::derivationPath( TWCoinTypeEthereum );
            TW::PrivateKey private_key     = wallet.getKey( TWCoinTypeEthereum, derivation_path );

            auto response = GenerateGeniusAddress( private_key, base_path );
            if ( response.has_error() )
            {
                genius_account_logger()->error( "Failed to generate Genius address from private key" );
                return nullptr;
            }

            genius_account_logger()->debug( "Generated a Genius address from private key" );
            auto account = CreateInstanceFromResponse( token_id, std::move( response.value() ), full_node );

            account->storage_->Save( "mnemonic", wallet.getMnemonic() );

            return account;
        }
        catch ( const std::invalid_argument & )
        {
            genius_account_logger()->error( "Tried to create private key from invalid mnemonic" );
        }

        return nullptr;
    }

    std::shared_ptr<GeniusAccount> GeniusAccount::NewFromPublicKey( TokenID          token_id,
                                                                    std::string_view public_key,
                                                                    bool             full_node )
    {
        if ( auto response = LoadGeniusAccount( public_key ); response.has_value() )
        {
            genius_account_logger()->debug( "Loaded existing Genius address" );
            return CreateInstanceFromResponse( token_id, std::move( response.value() ), full_node );
        }

        genius_account_logger()->error( "Could not load Genius address from storage" );

        return nullptr;
    }

    std::shared_ptr<GeniusAccount> GeniusAccount::New( TokenID                        token_id,
                                                       const boost::filesystem::path &base_path,
                                                       bool                           full_node )
    {
        if ( auto response = LoadGeniusAccount( base_path ); response.has_value() )
        {
            genius_account_logger()->debug( "Loaded existing Genius address" );
            return CreateInstanceFromResponse( token_id, std::move( response.value() ), full_node );
        }

        genius_account_logger()->error(
            "Could not find existing Genius address, generating one from a random mnemonic" );

        TW::HDWallet wallet( 128, "" );

        return GeniusAccount::NewFromMnemonic( token_id, wallet.getMnemonic(), base_path, full_node );
    }

    std::vector<std::string> GeniusAccount::GetAvailableAccounts( const boost::filesystem::path &base_path )
    {
        auto file_path = SetupStoragePath( base_path );
        genius_account_logger()->info( "Secure storage ID path: {}", file_path.string() );
        std::ifstream file( file_path.string() );

        if ( !file.is_open() )
        {
            genius_account_logger()->debug( "Could not find ID file" );
            return {};
        }

        std::vector<std::string> addresses;
        std::string              line;

        while ( std::getline( file, line ) )
        {
            addresses.push_back( std::move( line ) );
        }

        genius_account_logger()->debug( "Found {} public addresses in storage", addresses.size() );

        return addresses;
    }

    outcome::result<GeniusAccount::StorageWithAddress> GeniusAccount::LoadGeniusAccount(
        const boost::filesystem::path &base_path )
    {
        auto file_path = SetupStoragePath( base_path );
        genius_account_logger()->info( "Secure storage ID path: {}", file_path.string() );

        std::shared_ptr<ISecureStorage> storage;
        std::ifstream                   file( file_path.string() );
        std::string                     public_key;
        bool                            loaded_from_storage = false;

        if ( file.is_open() )
        {
            file >> public_key;
            genius_account_logger()->info( "Loaded public key from file: {} (length: {})",
                                           public_key.substr( 0, 16 ) + "...",
                                           public_key.length() );

            BOOST_OUTCOME_TRY( std::vector<uint8_t> vec, base::unhex( public_key ) );

            storage = std::make_shared<SecureStorageImpl>( std::string( SECURE_STORAGE_PREFIX ) +
                                                           libp2p::multi::detail::encodeBase58( vec ) );

            file.close();
            loaded_from_storage = true;
        }
        else
        {
            genius_account_logger()->debug( "Secure storage ID file does not exist, will try migration" );
            BOOST_OUTCOME_TRY( storage, MigrateSecureStorage( base_path ) );
        }

        auto load_res = storage->Load( "sgns_key" );
        if ( !load_res )
        {
            genius_account_logger()->warn( "Could not load sgns_key from secure storage" );
            return std::errc::no_such_file_or_directory;
        }

        auto key_seed = nil::crypto3::multiprecision::uint256_t( load_res.value() );
        genius_account_logger()->info( "Successfully loaded key_seed from storage" );

        if ( loaded_from_storage )
        {
            // Validate loaded key_seed matches stored public key
            if ( ethereum::EthereumKeyGenerator( key_seed ).GetEntirePubValue() != public_key )
            {
                genius_account_logger()->error( "Validation failed: key_seed does not match stored public key" );
                return std::errc::bad_message;
            }
            genius_account_logger()->info( "Validation successful: key_seed matches stored public key" );
        }

        ethereum::EthereumKeyGenerator eth_key( key_seed );

        return std::make_pair( std::move( storage ),
                               std::make_pair( KeyGenerator::ElGamal( std::move( key_seed ) ), std::move( eth_key ) ) );
    }

    outcome::result<GeniusAccount::StorageWithAddress> GeniusAccount::LoadGeniusAccount( std::string_view public_key )
    {
        BOOST_OUTCOME_TRY( std::vector<uint8_t> vec, base::unhex( public_key ) );

        std::shared_ptr<ISecureStorage> storage = std::make_shared<SecureStorageImpl>(
            std::string( SECURE_STORAGE_PREFIX ) + libp2p::multi::detail::encodeBase58( vec ) );

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

        ethereum::EthereumKeyGenerator eth_key( key_seed );

        return std::make_pair( std::move( storage ),
                               std::make_pair( KeyGenerator::ElGamal( std::move( key_seed ) ), std::move( eth_key ) ) );
    }

    outcome::result<GeniusAccount::StorageWithAddress> GeniusAccount::GenerateGeniusAddress(
        const char                    *eth_private_key,
        const boost::filesystem::path &base_path )
    {
        genius_account_logger()->trace( "Key seed from ethereum private key" );

        if ( eth_private_key == nullptr )
        {
            genius_account_logger()->error( "No ethereum address to generate from" );
            return std::errc::invalid_argument;
        }

        auto private_key_vec = base::unhex( eth_private_key );
        if ( private_key_vec.has_error() )
        {
            genius_account_logger()->error( "Could not extract private key from hexadecimal" );
            return std::errc::invalid_argument;
        }

        TW::PrivateKey tw_private_key( private_key_vec.value() );

        return GenerateGeniusAddress( tw_private_key, base_path );
    }

    outcome::result<GeniusAccount::StorageWithAddress> GeniusAccount::GenerateGeniusAddress(
        const TW::PrivateKey          &private_key,
        const boost::filesystem::path &base_path )
    {
        genius_account_logger()->trace( "Key seed from TW private key" );

        auto signed_secret = private_key.sign(
            TW::Data( ELGAMAL_PUBKEY_PREDEFINED.cbegin(), ELGAMAL_PUBKEY_PREDEFINED.cend() ),
            TWCurveSECP256k1 );

        if ( signed_secret.empty() )
        {
            genius_account_logger()->error( "Cannot sign secret" );
            return outcome::failure( std::errc::invalid_argument );
        }

        auto key_seed = nil::crypto3::multiprecision::uint256_t( TW::Hash::sha256( signed_secret ) );

        // Create storage and keys
        ethereum::EthereumKeyGenerator eth_key( key_seed );
        auto                           pub_key = eth_key.GetEntirePubValue();
        BOOST_OUTCOME_TRY( std::vector<uint8_t> pub_key_vec, base::unhex( pub_key ) );

        auto storage = std::make_shared<SecureStorageImpl>( std::string( SECURE_STORAGE_PREFIX ) +
                                                            libp2p::multi::detail::encodeBase58( pub_key_vec ) );
        BOOST_OUTCOME_TRY( storage->Save( "sgns_key", key_seed.str() ) );

        // Save public key to file
        auto file_path = SetupStoragePath( base_path );
        genius_account_logger()->info( "Secure storage ID path: {}", file_path.string() );

        std::ofstream out_file( file_path.string() );
        if ( !out_file.is_open() )
        {
            return outcome::failure( std::errc::bad_file_descriptor );
        }

        auto elgamal = KeyGenerator::ElGamal( std::move( key_seed ) );
        out_file << eth_key.GetEntirePubValue() << std::endl;

        return std::make_pair( std::move( storage ), std::make_pair( std::move( elgamal ), std::move( eth_key ) ) );
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
        methods.verify_signature_ = []( const std::string          &address,
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
        methods.get_utxos_ =
            [weakptr( weak_from_this() )]( const std::string &address ) -> outcome::result<std::vector<std::string>>
        {
            if ( auto self = weakptr.lock() )
            {
                std::vector<std::string> results;
                auto                     utxos = self->GetUTXOManager().GetUTXOs( address );
                results.reserve( utxos.size() );

                for ( const auto &utxo : utxos )
                {
                    results.push_back( utxo.GetTxID().toReadableString() );
                }

                return results;
            }

            return outcome::failure( std::errc::owner_dead );
        };
        methods.get_validator_weight_ =
            [weakptr( weak_from_this() )]( const std::string &address ) -> outcome::result<std::optional<uint64_t>>
        {
            if ( auto self = weakptr.lock() )
            {
                std::lock_guard lock( self->get_cids_mutex_ );
                if ( self->get_validator_weight_method_ )
                {
                    return self->get_validator_weight_method_( address );
                }

                return outcome::failure( AccountMessenger::Error::UTXO_REQUEST_ERROR );
            }

            return outcome::failure( std::errc::owner_dead );
        };
        methods.get_transaction_cid_ =
            [weakptr( weak_from_this() )]( const std::string &tx_hash ) -> outcome::result<std::string>
        {
            if ( auto self = weakptr.lock() )
            {
                std::lock_guard lock( self->get_cids_mutex_ );
                if ( self->get_transaction_cid_method_ )
                {
                    return self->get_transaction_cid_method_( tx_hash );
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

    bool GeniusAccount::ConfigureDatabaseDependencies( std::shared_ptr<crdt::GlobalDB> global_db )
    {
        bool ret = false;

        SetNonceStore( global_db->GetDataStore() );
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

    void GeniusAccount::DeconfigureDatabaseDependencies()
    {
        SetNonceStore( nullptr );

        if ( messenger_ )
        {
            messenger_->ClearBlockResponseHandler();
            messenger_->ClearHeadRequestHandler();
        }

        ClearHasBlockCidMethod();
        genius_account_logger()->debug( "Cleared database dependency handlers" );
    }

    GeniusAccount::GeniusAccount( std::shared_ptr<ethereum::EthereumKeyGenerator> eth_keypair,
                                  TokenID                                         token_id,
                                  std::shared_ptr<ISecureStorage>                 storage,
                                  bool                                            full_node ) :
        token( token_id ),
        is_full_node_( full_node ),
        eth_keypair_( std::move( eth_keypair ) ),
        storage_( std::move( storage ) ),
        utxo_manager_(
            is_full_node_,
            GetAddress(),
            [this]( const std::vector<uint8_t> &data ) { return this->Sign( data ); },
            []( const std::string &address, const std::vector<uint8_t> &signature, const std::vector<uint8_t> &data )
            {
                return GeniusAccount::VerifySignature( address,
                                                       std::string( signature.begin(), signature.end() ),
                                                       data );
            } ),
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
        std::unique_lock lock( nonce_mutex_ );
        auto             current_confirmed_nonce = confirmed_nonces_[address];
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

        lock.unlock();
        PersistConfirmedNonce( address, updated_nonce );
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

    outcome::result<std::optional<uint64_t>> GeniusAccount::FetchNetworkNonce( uint64_t timeout_ms ) const
    {
        if ( !messenger_ )
        {
            return outcome::failure( std::errc::no_such_device );
        }
        genius_account_logger()->debug( "Fetching nonce from the network with timeout {} ms", timeout_ms );

        auto result = messenger_->GetLatestNonce( timeout_ms );
        if ( result.has_value() )
        {
            genius_account_logger()->debug( "Nonce replied with value {}", result.value() );
            return result.value();
        }
        if ( result.error() == AccountMessenger::Error::RESPONSE_WITHOUT_NONCE )
        {
            genius_account_logger()->debug( "Network didn't answer nonce request" );
            return outcome::success( std::nullopt );
        }

        return outcome::failure( result.error() );
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
            auto     now = std::chrono::steady_clock::now();
            uint64_t cache_age_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>( now - cached_nonce_timestamp_ ).count();

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

    outcome::result<void> GeniusAccount::RequestTransaction(
        uint64_t                                            timeout_ms,
        const std::string                                  &tx_hash,
        std::function<void( outcome::result<std::string> )> callback ) const
    {
        if ( !messenger_ )
        {
            return outcome::failure( std::errc::no_such_device );
        }
        genius_account_logger()->debug( "Requesting transaction {}", tx_hash.substr( 0, 8 ) );

        return messenger_->RequestTransaction( timeout_ms, tx_hash, std::move( callback ) );
    }

    outcome::result<std::unordered_set<std::string>> GeniusAccount::RequestUTXOs( uint64_t           timeout_ms,
                                                                                  const std::string &address,
                                                                                  uint64_t silent_time_ms ) const
    {
        if ( !messenger_ )
        {
            return outcome::failure( std::errc::no_such_device );
        }
        genius_account_logger()->debug( "Requesting UTXOs for {}", address.substr( 0, 8 ) );

        return messenger_->RequestUTXOs( timeout_ms, address, silent_time_ms );
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

    void GeniusAccount::SetGetValidatorWeightMethod(
        std::function<outcome::result<std::optional<uint64_t>>( const std::string & )> method )
    {
        std::lock_guard lock( get_cids_mutex_ );
        get_validator_weight_method_ = std::move( method );
    }

    void GeniusAccount::ClearGetValidatorWeightMethod()
    {
        std::lock_guard lock( get_cids_mutex_ );
        get_validator_weight_method_ = nullptr;
    }

    void GeniusAccount::SetGetTransactionCIDMethod(
        std::function<outcome::result<std::string>( const std::string & )> method )
    {
        std::lock_guard lock( get_cids_mutex_ );
        get_transaction_cid_method_ = std::move( method );
    }

    void GeniusAccount::ClearGetTransactionCIDMethod()
    {
        std::lock_guard lock( get_cids_mutex_ );
        get_transaction_cid_method_ = nullptr;
    }

    void GeniusAccount::SetNonceStore( std::shared_ptr<storage::rocksdb> db )
    {
        nonce_db_ = std::move( db );
        LoadConfirmedNonces();
    }

    void GeniusAccount::LoadConfirmedNonces()
    {
        if ( !nonce_db_ )
        {
            return;
        }

        base::Buffer prefix;
        prefix.put( std::string( NONCE_KEY_PREFIX ) );
        auto query_res = nonce_db_->query( prefix );
        if ( query_res.has_error() )
        {
            return;
        }

        std::unordered_map<std::string, uint64_t> loaded;
        uint64_t                                  max_local = 0;
        bool                                      has_local = false;

        for ( const auto &[key_buf, val_buf] : query_res.value() )
        {
            const auto key = std::string( key_buf.toString() );

            if ( key.rfind( std::string( NONCE_KEY_PREFIX ) ) != 0 )
            {
                continue;
            }

            auto address = key.substr( std::string( NONCE_KEY_PREFIX ).size() );
            try
            {
                uint64_t nonce  = std::stoull( std::string( val_buf.toString() ) );
                loaded[address] = nonce;
            }
            catch ( const std::exception & )
            {
                genius_account_logger()->error( "Failed to parse nonce for {}: ",
                                                address.substr( 0, 8 ),
                                                val_buf.toString() );
            }
        }

        std::lock_guard lock( nonce_mutex_ );
        for ( const auto &[address, nonce] : loaded )
        {
            confirmed_nonces_[address] = nonce;
        }

        if ( has_local )
        {
            local_confirmed_nonce_ = max_local;
        }
    }

    void GeniusAccount::PersistConfirmedNonce( const std::string &address, uint64_t nonce )
    {
        if ( !nonce_db_ )
        {
            return;
        }

        base::Buffer key;

        key.put( std::string( NONCE_KEY_PREFIX ) + address );

        base::Buffer value;
        value.put( std::to_string( nonce ) );
        auto put_res = nonce_db_->put( key, value );
        if ( put_res.has_error() )
        {
            genius_account_logger()->error( "Failed to persist nonce for {}", address.substr( 0, 8 ) );
        }
    }
}
