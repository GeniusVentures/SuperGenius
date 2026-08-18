#include "GeniusAccount.hpp"

#include <fmt/std.h>
#include <fmt/ranges.h>
#include <TrustWalletCore/TWCoinType.h>
#include <TrustWalletCore/TWDerivation.h>
#include <WalletCore/Coin.h>
#include <WalletCore/HDWallet.h>
#include <WalletCore/Hash.h>
#include <WalletCore/PrivateKey.h>
#include <charconv>
#include <cstdlib>
#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <algorithm>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_set>

#include "base/hexutil.hpp"
#include "local_secure_storage/impl/JSONBackend.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include "local_secure_storage/SecureStorage.hpp"
#include "outcome/outcome.hpp"
#include "account/AccountMessenger.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "primitives/cid/cid.hpp"

constexpr std::string_view SECURE_STORAGE_PREFIX = "SGNS";
constexpr size_t           PUBLIC_KEY_HEX_LENGTH = 128;

namespace
{
    using namespace sgns;

    base::Logger genius_account_logger()
    {
        // Always call base::createLogger to get the current logger
        // This will return existing logger or create new one as needed
        return base::createLogger( "GeniusAccount" );
    }

    /// Order of the secp256k1 group. A key seed is a raw 256-bit number, so it
    /// must be reduced into the scalar field before it is a usable secret key
    /// (this is what the crypto3 scalar-field conversion used to do implicitly).
    const uint256_t SECP256K1_CURVE_ORDER( "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141" );

    /// Re-encode a stored key seed as the big-endian 32-byte secret key
    /// libsecp256k1 (and therefore GeniusSigner) expects.
    GeniusSigner::PrivateKey KeySeedToPrivateKey( const uint256_t &key_seed )
    {
        uint256_t reduced = key_seed % SECP256K1_CURVE_ORDER;

        GeniusSigner::PrivateKey secret_key{};
        for ( size_t i = secret_key.size(); i-- > 0; )
        {
            secret_key[i]   = static_cast<uint8_t>( reduced & 0xFFu );
            reduced       >>= 8;
        }
        return secret_key;
    }

    /// Interpret a hash as a key seed, most significant byte first.
    uint256_t KeySeedFromBytes( const std::vector<uint8_t> &bytes )
    {
        uint256_t key_seed;
        boost::multiprecision::import_bits( key_seed, bytes.begin(), bytes.end(), 8 );
        return key_seed;
    }

    boost::filesystem::path SetupStoragePath( const boost::filesystem::path &base_path )
    {
        constexpr std::string_view FILE_NAME = "secure_storage_id";
        boost::filesystem::create_directories( base_path );
        return boost::filesystem::canonical( boost::filesystem::absolute( base_path ) ) / FILE_NAME;
    }

    /// Global factory override (null = use default SecureStorageImpl)
    GeniusAccount::SecureStorageFactory g_storage_factory;

    outcome::result<std::shared_ptr<JSONBackend>> CreateSecureStorage( std::string_view public_key_hex )
    {
        BOOST_OUTCOME_TRY( std::vector<uint8_t> vec, base::unhex( public_key_hex ) );

        const std::string identifier( std::string( SECURE_STORAGE_PREFIX ) +
                                      libp2p::multi::detail::encodeBase58( vec ) );

        if ( g_storage_factory )
        {
            auto storage = g_storage_factory( identifier );
            auto backend = std::dynamic_pointer_cast<JSONBackend>( storage );
            if ( backend )
            {
                return backend;
            }
            genius_account_logger()->error( "Custom secure storage factory did not return a JSONBackend" );
            return outcome::failure( std::errc::invalid_argument );
        }

        return std::make_shared<SecureStorageImpl>( identifier );
    }

    std::vector<std::string> ReadPublicKeysFromFile( const boost::filesystem::path &file_path )
    {
        std::ifstream file( file_path.string() );
        if ( !file.is_open() )
        {
            return {};
        }

        std::vector<std::string>        keys;
        std::unordered_set<std::string> seen;
        std::string                     line;

        while ( std::getline( file, line ) )
        {
            if ( line.empty() )
            {
                continue;
            }

            if ( !GeniusAccount::IsValidPublicKey( line ) )
            {
                genius_account_logger()->warn( "Skipping invalid public key in storage file: {}",
                                               line.substr( 0, 16 ) );
                continue;
            }

            if ( seen.insert( line ).second )
            {
                keys.push_back( std::move( line ) );
            }
            else
            {
                genius_account_logger()->debug( "Skipping duplicate public key: {}", line.substr( 0, 16 ) );
            }
        }

        return keys;
    }

    outcome::result<void> WritePublicKeysToFile( const boost::filesystem::path  &file_path,
                                                 const std::vector<std::string> &keys )
    {
        std::ofstream file( file_path.string() );
        if ( !file.is_open() )
        {
            genius_account_logger()->error( "Could not open ID file for writing: {}", file_path.string() );
            return std::errc::bad_file_descriptor;
        }

        for ( const auto &key : keys )
        {
            file << key << '\n';
        }
        file.close();

        return outcome::success();
    }

    outcome::result<void> AppendPublicKeyToFile( const boost::filesystem::path &base_path,
                                                 std::string_view               public_key_hex )
    {
        auto file_path = SetupStoragePath( base_path );
        if ( !GeniusAccount::IsValidPublicKey( public_key_hex ) )
        {
            genius_account_logger()->error( "Can't write invalid public key" );
            return std::errc::invalid_argument;
        }

        auto existing_keys = ReadPublicKeysFromFile( file_path );
        auto key_it        = std::find( existing_keys.cbegin(), existing_keys.cend(), public_key_hex );
        if ( key_it != existing_keys.cend() )
        {
            genius_account_logger()->debug( "Public key already present in storage file, skipping write" );
            return outcome::success();
        }
        existing_keys.emplace_back( public_key_hex );

        return WritePublicKeysToFile( file_path, existing_keys );
    }

    bool TryParseUint64( std::string_view input, uint64_t &value, bool allow_suffix = false )
    {
        if ( input.empty() )
        {
            return false;
        }

        const auto *begin = input.data();
        const auto *end   = input.data() + input.size();
        auto [ptr, ec]    = std::from_chars( begin, end, value );
        if ( ec != std::errc() || ptr == begin )
        {
            return false;
        }

        return allow_suffix || ptr == end;
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

        uint256_t key_seed( maybe_key->value.GetString() );
        auto      pub_key = GeniusSigner( KeySeedToPrivateKey( key_seed ) ).GetAddress();

        BOOST_OUTCOME_TRY( auto secure_storage, CreateSecureStorage( pub_key ) );

        BOOST_OUTCOME_TRY( AppendPublicKeyToFile( base_path, pub_key ) );

        rj::Document new_doc;
        new_doc.CopyFrom( maybe_field->value, new_doc.GetAllocator() );
        BOOST_OUTCOME_TRY( secure_storage->SaveJSON( std::move( new_doc ) ) );
        genius_account_logger()->debug( "Successfully migrated JSON secure storage" );

        return secure_storage;
    }

    outcome::result<GeniusAccount::StorageWithAddress> BuildStorageWithAddress( std::shared_ptr<ISecureStorage> storage,
                                                                                std::string_view public_key )
    {
        auto load_res = storage->Load( "sgns_key" );
        if ( !load_res )
        {
            genius_account_logger()->warn( "Could not load sgns_key from secure storage" );
            return std::errc::no_such_file_or_directory;
        }

        auto key_seed = uint256_t( load_res.value() );
        genius_account_logger()->info( "Successfully loaded key_seed from storage" );

        auto private_key = KeySeedToPrivateKey( key_seed );

        // Validate loaded key_seed matches the expected public key
        if ( GeniusSigner( private_key ).GetAddress() != public_key )
        {
            genius_account_logger()->error( "Validation failed: key_seed does not match stored public key" );
            return std::errc::bad_message;
        }
        genius_account_logger()->info( "Validation successful: key_seed matches stored public key" );

        return std::make_pair( std::move( storage ), private_key );
    }
}

namespace sgns
{
    bool GeniusAccount::IsValidPublicKey( std::string_view key ) noexcept
    {
        if ( key.length() != PUBLIC_KEY_HEX_LENGTH )
        {
            return false;
        }
        return std::all_of(
            key.begin(),
            key.end(),
            []( char c ) { return ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' ); } );
    }

    const std::array<uint8_t, 32> GeniusAccount::ELGAMAL_PUBKEY_PREDEFINED{
        0xfc, 0x60, 0x52, 0x6c, 0x91, 0xec, 0x81, 0xd5, 0xd4, 0xfa, 0xb2, 0x78, 0x04, 0xad, 0x93, 0xd0,
        0xd4, 0xf9, 0x4b, 0x55, 0xc7, 0x5e, 0xed, 0x6f, 0xda, 0x2e, 0xa0, 0xc9, 0xc8, 0x2c, 0x21, 0x36,
    };

    void GeniusAccount::SetSecureStorageFactory( SecureStorageFactory factory )
    {
        g_storage_factory = std::move( factory );
    }

    const GeniusAccount::SecureStorageFactory &GeniusAccount::GetSecureStorageFactory()
    {
        return g_storage_factory;
    }

    std::shared_ptr<GeniusAccount> GeniusAccount::CreateInstanceFromResponse( TokenID            token_id,
                                                                              StorageWithAddress response_value )
    {
        auto [storage, private_key] = std::move( response_value );

        auto instance = std::shared_ptr<GeniusAccount>(
            new GeniusAccount( GeniusSigner( private_key ), token_id, std::move( storage ) ) );

        return instance;
    }

    std::shared_ptr<GeniusAccount> GeniusAccount::New( TokenID token_id, const boost::filesystem::path &base_path )
    {
        if ( auto response = LoadGeniusAccount( base_path ); response.has_value() )
        {
            genius_account_logger()->debug( "Loaded existing Genius address" );
            return CreateInstanceFromResponse( token_id, std::move( response.value() ) );
        }

        genius_account_logger()->error(
            "Could not find existing Genius address, generating one from a random mnemonic" );

        return NewFromRandomMnemonic( token_id, base_path ).first;
    }

    std::shared_ptr<GeniusAccount> GeniusAccount::NewEphemeral( TokenID token_id )
    {
        auto signer  = GeniusSigner::Generate();
        auto storage = std::make_shared<MemorySecureStorage>( "ephemeral:" + signer.GetAddress() );
        return std::shared_ptr<GeniusAccount>(
            new GeniusAccount( std::move( signer ), token_id, std::move( storage ) ) );
    }

    std::shared_ptr<GeniusAccount> GeniusAccount::NewFromPrivateKey( TokenID                        token_id,
                                                                     const char                    *eth_private_key,
                                                                     const boost::filesystem::path &base_path )
    {
        auto response = GenerateGeniusAddress( eth_private_key, base_path );
        if ( response.has_error() )
        {
            genius_account_logger()->error( "Failed to generate Genius address from private key" );
            return nullptr;
        }

        genius_account_logger()->debug( "Generated a Genius address from private key" );
        return CreateInstanceFromResponse( token_id, std::move( response.value() ) );
    }

    std::shared_ptr<GeniusAccount> GeniusAccount::NewFromPublicKey( TokenID token_id, std::string_view public_key )
    {
        if ( auto response = LoadGeniusAccount( public_key ); response.has_value() )
        {
            genius_account_logger()->debug( "Loaded existing Genius address" );
            return CreateInstanceFromResponse( token_id, std::move( response.value() ) );
        }

        genius_account_logger()->error( "Could not load Genius address from storage" );

        return nullptr;
    }

    std::shared_ptr<GeniusAccount> GeniusAccount::NewFromMnemonic( TokenID                        token_id,
                                                                   const std::string             &mnemonic,
                                                                   const boost::filesystem::path &base_path )
    {
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
            auto account = CreateInstanceFromResponse( token_id, std::move( response.value() ) );

            if ( account->storage_->Save( "mnemonic", wallet.getMnemonic() ).has_failure() )
            {
                genius_account_logger()->error( "Failed to save mnemonic to secure storage" );
            }

            return account;
        }
        catch ( const std::invalid_argument & )
        {
            genius_account_logger()->error( "Tried to create private key from invalid mnemonic" );
        }

        return nullptr;
    }

    std::pair<std::shared_ptr<GeniusAccount>, std::string> GeniusAccount::NewFromRandomMnemonic(
        TokenID                        token_id,
        const boost::filesystem::path &base_path )
    {
        TW::HDWallet wallet( 128, "" );
        std::string  mnemonic = wallet.getMnemonic();
        auto         account  = NewFromMnemonic( token_id, mnemonic, base_path );
        return { account, mnemonic };
    }

    std::string_view GeniusAccount::NormalizeAddress( std::string_view address ) noexcept
    {
        if ( address.size() >= 2 && address[0] == '0' && address[1] == 'x' )
        {
            return address.substr( 2 );
        }
        return address;
    }

    std::vector<std::string> GeniusAccount::GetAvailableAccounts( const boost::filesystem::path &base_path )
    {
        auto file_path = SetupStoragePath( base_path );
        genius_account_logger()->info( "Secure storage ID path: {}", file_path.string() );

        auto addresses = ReadPublicKeysFromFile( file_path );
        if ( addresses.empty() )
        {
            genius_account_logger()->debug( "No valid public addresses found in storage" );
        }
        else
        {
            genius_account_logger()->debug( "Found {} public addresses in storage", addresses.size() );
        }

        return addresses;
    }

    outcome::result<void> GeniusAccount::DeleteAccount( std::string_view               public_address,
                                                        const boost::filesystem::path &base_path )
    {
        auto file_path = SetupStoragePath( base_path );
        auto addresses = ReadPublicKeysFromFile( file_path );

        public_address = NormalizeAddress( public_address );
        auto it        = std::find( addresses.begin(), addresses.end(), public_address );
        if ( it == addresses.end() )
        {
            genius_account_logger()->warn( "Can't delete account that was not added" );
            return std::errc::address_not_available;
        }
        addresses.erase( it );

        return WritePublicKeysToFile( file_path, addresses );
    }

    outcome::result<GeniusAccount::StorageWithAddress> GeniusAccount::LoadGeniusAccount(
        const boost::filesystem::path &base_path )
    {
        auto file_path = SetupStoragePath( base_path );
        genius_account_logger()->info( "Secure storage ID path: {}", file_path.string() );

        auto keys = ReadPublicKeysFromFile( file_path );

        if ( !keys.empty() )
        {
            const auto &public_key = keys.front();
            genius_account_logger()->info( "Loaded public key from file: {} (length: {})",
                                           public_key.substr( 0, 16 ) + "...",
                                           public_key.length() );

            BOOST_OUTCOME_TRY( auto storage, CreateSecureStorage( public_key ) );
            return BuildStorageWithAddress( std::move( storage ), public_key );
        }

        genius_account_logger()->debug( "Secure storage ID file does not exist or has no valid keys, "
                                        "will try migration" );
        BOOST_OUTCOME_TRY( auto storage, MigrateSecureStorage( base_path ) );

        // After migration, re-read the first key that was written
        auto migrated_keys = ReadPublicKeysFromFile( file_path );
        if ( migrated_keys.empty() )
        {
            genius_account_logger()->error( "Migration succeeded but no valid keys found in file" );
            return std::errc::bad_message;
        }

        return BuildStorageWithAddress( std::move( storage ), migrated_keys.front() );
    }

    outcome::result<GeniusAccount::StorageWithAddress> GeniusAccount::LoadGeniusAccount( std::string_view public_key )
    {
        if ( !IsValidPublicKey( public_key ) )
        {
            genius_account_logger()->error( "Invalid public key format (length: {}, expected: {})",
                                            public_key.length(),
                                            PUBLIC_KEY_HEX_LENGTH );
            return std::errc::invalid_argument;
        }

        BOOST_OUTCOME_TRY( auto storage, CreateSecureStorage( public_key ) );
        return BuildStorageWithAddress( std::move( storage ), public_key );
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

        auto key_seed = KeySeedFromBytes( TW::Hash::sha256( signed_secret ) );

        // Create storage and keys
        auto sgns_private_key = KeySeedToPrivateKey( key_seed );
        auto pub_key          = GeniusSigner( sgns_private_key ).GetAddress();

        BOOST_OUTCOME_TRY( auto storage, CreateSecureStorage( pub_key ) );
        BOOST_OUTCOME_TRY( storage->Save( "sgns_key", key_seed.str() ) );

        BOOST_OUTCOME_TRY( AppendPublicKeyToFile( base_path, pub_key ) );

        return std::make_pair( std::move( storage ), sgns_private_key );
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
        messenger_ = AccountMessenger::New( signer_.GetAddress(), std::move( pubsub ), std::move( methods ) );

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
                        BOOST_OUTCOME_TRY( auto has_block, dag_syncer->HasBlock( cid_result.value() ) );
                        return has_block;
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

    GeniusAccount::GeniusAccount( GeniusSigner signer, TokenID token_id, std::shared_ptr<ISecureStorage> storage ) :
        token( token_id ),
        storage_( std::move( storage ) ),
        signer_( std::move( signer ) ),
        utxo_manager_(
            GetAddress(),
            [this]( const std::vector<uint8_t> &data ) { return this->Sign( data ); },
            []( const std::string &address, const std::vector<uint8_t> &signature, const std::vector<uint8_t> &data )
            { return GeniusAccount::VerifySignature( address, signature, data ); } ),
        nonce_request_in_progress_( false ),
        cached_nonce_timestamp_( std::chrono::steady_clock::time_point{} )
    {
    }

    GeniusAccount::~GeniusAccount()
    {
    }

    std::string GeniusAccount::GetAddress() const
    {
        return signer_.GetAddress();
    }

    TokenID GeniusAccount::GetToken() const
    {
        return token;
    }

    bool GeniusAccount::VerifySignature( const std::string          &address,
                                         std::string_view            sig,
                                         const std::vector<uint8_t> &data )
    {
        return GeniusSigner::VerifySignature( address, sig, data );
    }

    bool GeniusAccount::VerifySignature( const std::string          &address,
                                         const std::vector<uint8_t> &sig,
                                         const std::vector<uint8_t> &data )
    {
        return GeniusSigner::VerifySignature( address, sig, data );
    }

    std::vector<uint8_t> GeniusAccount::Sign( const std::vector<uint8_t> &data ) const
    {
        return signer_.Sign( data );
    }

    std::vector<InputUTXOInfo> GeniusAccount::CreateInputsFromUTXOs( const std::vector<GeniusUTXO> &utxos ) const
    {
        std::vector<InputUTXOInfo> inputs;
        inputs.reserve( utxos.size() );

        for ( const auto &utxo : utxos )
        {
            InputUTXOInfo input;
            input.txid_hash_  = utxo.GetTxID();
            input.output_idx_ = utxo.GetOutputIdx();
            input.signature_  = Sign( input.SerializeForSigning() );
            inputs.emplace_back( std::move( input ) );
        }

        return inputs;
    }

    void GeniusAccount::SetPeerConfirmedNonce( uint64_t nonce, const std::string &address, const std::string &tx_hash )
    {
        std::unique_lock lock( nonce_mutex_ );
        auto             current_confirmed_nonce = confirmed_nonces_[address];
        genius_account_logger()->debug(
            "Setting the max value between {} and {:.8} as a confirmed nonce for address {}",
            current_confirmed_nonce,
            nonce,
            address );
        auto updated_nonce         = std::max( nonce, current_confirmed_nonce );
        confirmed_nonces_[address] = updated_nonce;

        if ( address == signer_.GetAddress() )
        {
            if ( !local_confirmed_nonce_ || updated_nonce > local_confirmed_nonce_.value() )
            {
                local_confirmed_nonce_ = updated_nonce;
            }
            if ( !tx_hash.empty() )
            {
                UpdateLocalConfirmedTxHistoryLocked( updated_nonce, tx_hash );
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
        std::unique_lock lock( nonce_mutex_ );
        auto             it                      = confirmed_nonces_.find( address );
        uint64_t         current_confirmed_nonce = 0;
        bool             should_persist          = false;
        if ( it != confirmed_nonces_.end() )
        {
            current_confirmed_nonce = it->second;
        }
        genius_account_logger()->debug( "Rolling back nonce {} for address {:.8} (current confirmed {})",
                                        nonce,
                                        address,
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
            should_persist = true;
        }

        if ( address == signer_.GetAddress() )
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
            RollbackLocalConfirmedTxHistoryLocked( nonce );
            pending_nonces_.erase( nonce );
            should_persist = true;
        }

        const auto     final_it        = confirmed_nonces_.find( address );
        const uint64_t persisted_nonce = address == signer_.GetAddress()
                                             ? local_confirmed_nonce_.value_or( 0 )
                                             : ( final_it != confirmed_nonces_.end() ? final_it->second : 0 );

        lock.unlock();
        if ( should_persist )
        {
            PersistConfirmedNonce( address, persisted_nonce );
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
        return GetPeerNonce( signer_.GetAddress() );
    }

    outcome::result<std::string> GeniusAccount::GetLocalConfirmedTxHash( uint64_t nonce ) const
    {
        std::shared_lock lock( nonce_mutex_ );
        for ( auto it = local_confirmed_transactions_.rbegin(); it != local_confirmed_transactions_.rend(); ++it )
        {
            if ( it->nonce == nonce )
            {
                return it->hash;
            }
        }

        return outcome::failure( std::errc::no_message );
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

    outcome::result<void> GeniusAccount::RequestValidatorRegistry(
        uint64_t                                            timeout_ms,
        std::function<void( outcome::result<std::string> )> callback ) const
    {
        if ( !messenger_ )
        {
            return outcome::failure( std::errc::no_such_device );
        }
        genius_account_logger()->debug( "Requesting Validator Registry block from the network" );

        return messenger_->RequestValidatorRegistry( timeout_ms, std::move( callback ) );
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
        genius_account_logger()->debug( "Requesting transaction {:.8}", tx_hash );

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
        genius_account_logger()->debug( "Requesting UTXOs for {:.8}", address );

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
        {
            std::lock_guard lock( nonce_db_mutex_ );
            nonce_db_ = std::move( db );
        }
        LoadConfirmedNonces();
    }

    void GeniusAccount::LoadConfirmedNonces()
    {
        std::shared_ptr<storage::rocksdb> nonce_db;
        {
            std::lock_guard lock( nonce_db_mutex_ );
            nonce_db = nonce_db_;
        }

        if ( !nonce_db )
        {
            return;
        }

        const auto self_address = signer_.GetAddress();

        base::Buffer prefix;
        prefix.put( std::string( NONCE_KEY_PREFIX ) );
        auto query_res = nonce_db->query( prefix );
        if ( query_res.has_error() )
        {
            return;
        }

        std::unordered_map<std::string, uint64_t> loaded_nonces;
        for ( const auto &[key_buf, val_buf] : query_res.value() )
        {
            const auto key = std::string( key_buf.toString() );
            if ( key.rfind( std::string( NONCE_KEY_PREFIX ) ) != 0 )
            {
                continue;
            }

            const auto address      = key.substr( std::string( NONCE_KEY_PREFIX ).size() );
            uint64_t   parsed_nonce = 0;
            if ( TryParseUint64( val_buf.toString(), parsed_nonce, true ) )
            {
                loaded_nonces[address] = parsed_nonce;
            }
            else
            {
                genius_account_logger()->error( "Failed to parse nonce for {:.8}: ", address, val_buf.toString() );
            }
        }

        std::optional<uint64_t> local_nonce;
        if ( auto it = loaded_nonces.find( self_address ); it != loaded_nonces.end() )
        {
            local_nonce = it->second;
        }

        std::deque<ConfirmedTxRecord> local_history;
        base::Buffer                  local_history_key;
        local_history_key.put( std::string( LOCAL_CONFIRMED_TX_HISTORY_KEY_PREFIX ) + self_address );
        if ( auto local_history_res = nonce_db->get( local_history_key ); local_history_res.has_value() )
        {
            local_history = DeserializeConfirmedTxHistory( std::string( local_history_res.value().toString() ) );
        }

        std::lock_guard lock( nonce_mutex_ );
        confirmed_nonces_ = std::move( loaded_nonces );
        if ( local_nonce.has_value() )
        {
            confirmed_nonces_[self_address] = local_nonce.value();
            local_confirmed_nonce_          = local_nonce.value();
        }
        else if ( !local_history.empty() )
        {
            confirmed_nonces_[self_address] = local_history.back().nonce;
            local_confirmed_nonce_          = local_history.back().nonce;
        }
        else
        {
            local_confirmed_nonce_.reset();
        }
        local_confirmed_transactions_ = std::move( local_history );
    }

    void GeniusAccount::PersistConfirmedNonce( const std::string &address, uint64_t nonce )
    {
        std::shared_ptr<storage::rocksdb> nonce_db;
        {
            std::lock_guard lock( nonce_db_mutex_ );
            nonce_db = nonce_db_;
        }

        if ( !nonce_db )
        {
            return;
        }

        base::Buffer nonce_key;
        nonce_key.put( std::string( NONCE_KEY_PREFIX ) + address );

        base::Buffer nonce_value;
        nonce_value.put( std::to_string( nonce ) );
        auto nonce_put_res = nonce_db->put( nonce_key, nonce_value );
        if ( nonce_put_res.has_error() )
        {
            genius_account_logger()->error( "Failed to persist nonce for {:.8}", address );
        }

        if ( address != signer_.GetAddress() )
        {
            return;
        }

        std::deque<ConfirmedTxRecord> history_copy;
        {
            std::shared_lock lock( nonce_mutex_ );
            history_copy = local_confirmed_transactions_;
        }

        base::Buffer history_key;
        history_key.put( std::string( LOCAL_CONFIRMED_TX_HISTORY_KEY_PREFIX ) + address );

        base::Buffer history_value;
        history_value.put( SerializeConfirmedTxHistory( history_copy ) );
        auto history_put_res = nonce_db->put( history_key, history_value );
        if ( history_put_res.has_error() )
        {
            genius_account_logger()->error( "Failed to persist confirmed tx history for {}", address.substr( 0, 8 ) );
        }
    }

    std::string GeniusAccount::SerializeConfirmedTxHistory( const std::deque<ConfirmedTxRecord> &history )
    {
        std::ostringstream out;
        for ( const auto &record : history )
        {
            out << record.nonce << '|' << record.hash << '\n';
        }

        return out.str();
    }

    std::deque<GeniusAccount::ConfirmedTxRecord> GeniusAccount::DeserializeConfirmedTxHistory(
        const std::string &serialized )
    {
        std::deque<ConfirmedTxRecord> history;
        std::istringstream            input( serialized );
        std::string                   line;

        while ( std::getline( input, line ) )
        {
            if ( line.empty() )
            {
                continue;
            }

            const auto separator = line.find( '|' );
            if ( separator == std::string::npos )
            {
                continue;
            }

            uint64_t parsed_nonce = 0;
            if ( TryParseUint64( line.substr( 0, separator ), parsed_nonce ) )
            {
                history.push_back( { parsed_nonce, line.substr( separator + 1 ) } );
            }
            else
            {
                continue;
            }
        }

        while ( history.size() > LOCAL_CONFIRMED_TX_HISTORY_LIMIT )
        {
            history.pop_front();
        }

        return history;
    }

    void GeniusAccount::UpdateLocalConfirmedTxHistoryLocked( uint64_t nonce, const std::string &tx_hash )
    {
        while ( !local_confirmed_transactions_.empty() && local_confirmed_transactions_.back().nonce > nonce )
        {
            local_confirmed_transactions_.pop_back();
        }

        for ( auto &record : local_confirmed_transactions_ )
        {
            if ( record.nonce == nonce )
            {
                record.hash = tx_hash;
                return;
            }
        }

        local_confirmed_transactions_.push_back( { nonce, tx_hash } );
        while ( local_confirmed_transactions_.size() > LOCAL_CONFIRMED_TX_HISTORY_LIMIT )
        {
            local_confirmed_transactions_.pop_front();
        }
    }

    void GeniusAccount::RollbackLocalConfirmedTxHistoryLocked( uint64_t nonce )
    {
        while ( !local_confirmed_transactions_.empty() && local_confirmed_transactions_.back().nonce >= nonce )
        {
            local_confirmed_transactions_.pop_back();
        }
    }
}
