#include "GeniusAccount.hpp"
#include <nil/crypto3/algebra/marshalling.hpp>
#include <nil/crypto3/pubkey/algorithm/sign.hpp>
#include <nil/crypto3/pubkey/algorithm/verify.hpp>
#include "WalletCore/Hash.h"
#include "local_secure_storage/ISecureStorage.hpp"
#include "singleton/CComponentFactory.hpp"
#include "WalletCore/PrivateKey.h"
#include <boost/algorithm/hex.hpp>
#include "crypto/hasher/hasher_impl.hpp"
#include "ipfs_pubsub/gossip_pubsub.hpp"
#include "account/AccountMessenger.hpp"

namespace
{
    std::array<uint8_t, 32> get_elgamal_pubkey()
    {
        const auto              elgamal_key = KeyGenerator::ElGamal( 0x1234_cppui256 ).GetPublicKey().public_key_value;
        std::array<uint8_t, 32> exported;
        export_bits( elgamal_key, exported.begin(), 8, false );

        return exported;
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

    std::shared_ptr<GeniusAccount> GeniusAccount::New( TokenID                         token_id,
                                                       std::shared_ptr<ISecureStorage> storage,
                                                       const char                     *eth_private_key,
                                                       bool                            full_node )
    {
        std::shared_ptr<GeniusAccount> instance;

        if ( auto maybe_address = GenerateGeniusAddress( *storage, eth_private_key ); maybe_address.has_value() )
        {
            genius_account_logger()->debug( "Generated a Genius Address from private key" );
            auto [temp_elgamal_address, temp_eth_address] = maybe_address.value();

            instance = std::shared_ptr<GeniusAccount>(
                new GeniusAccount( std::move( token_id ), std::move( storage ), full_node ) );

            instance->eth_keypair = std::make_shared<ethereum::EthereumKeyGenerator>( std::move( temp_eth_address ) );
            instance->elgamal_address = std::make_shared<KeyGenerator::ElGamal>( std::move( temp_elgamal_address ) );
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
                return self->GetPeerNonce( std::move( address ) );
            }

            return outcome::failure( std::errc::owner_dead );
        };
        messenger_ = AccountMessenger::New( eth_keypair->GetEntirePubValue(),
                                            std::move( pubsub ),
                                            std::move( methods ) );
        if ( messenger_ )
        {
            genius_account_logger()->debug( "Created AccountMessenger" );
            ret = true;
        }
        return ret;
    }

    GeniusAccount::GeniusAccount( TokenID token_id, std::shared_ptr<ISecureStorage> storage, bool full_node ) :
        token( token_id ), storage_( std::move( storage ) ), is_full_node_( full_node )
    {
    }

    GeniusAccount::~GeniusAccount() {}

    std::string GeniusAccount::GetAddress() const
    {
        return eth_keypair->GetEntirePubValue();
    }

    uint64_t GeniusAccount::GetBalance( const std::string &address ) const
    {
        uint64_t retval = 0;

        // If not a full node and trying to get balance for other addresses, return 0
        if ( !is_full_node_ && address != GetAddress() )
        {
            genius_account_logger()->error( "Non-full node cannot get balance for other addresses" );
            return 0;
        }

        std::shared_lock lock( utxos_mutex_ );
        if ( auto it = utxos_.find( address ); it != utxos_.end() )
        {
            for ( const auto &curr : it->second )
            {
                if ( !curr.GetLock() )
                {
                    retval += curr.GetAmount();
                }
            }
        }

        return retval;
    }

    TokenID GeniusAccount::GetToken() const
    {
        return token;
    }

    uint64_t GeniusAccount::GetBalance() const
    {
        return GetBalance( GetAddress() );
    }

    uint64_t GeniusAccount::GetBalance( const TokenID token_id ) const
    {
        return GetBalance( token_id, GetAddress() );
    }

    bool GeniusAccount::PutUTXO( const GeniusUTXO &new_utxo, const std::string &address )
    {
        // If not a full node and trying to store UTXOs for other addresses, reject
        if ( !is_full_node_ && address != GetAddress() )
        {
            genius_account_logger()->debug( "Non-full node cannot store UTXOs for other addresses" );
            return false;
        }

        std::unique_lock lock( utxos_mutex_ );
        auto            &utxo_list = utxos_[address];

        bool is_new = true;
        for ( auto &curr : utxo_list )
        {
            if ( new_utxo.GetTxID() != curr.GetTxID() )
            {
                continue;
            }
            if ( new_utxo.GetOutputIdx() != curr.GetOutputIdx() )
            {
                continue;
            }
            //TODO - If it's the same, might be locked, then unlock
            is_new = false;
            break;
        }
        if ( is_new )
        {
            utxo_list.push_back( new_utxo );
        }
        return is_new;
    }

    void GeniusAccount::DeleteUTXO( const base::Hash256 &utxo_id, const std::string &address )
    {
        // If not a full node and trying to delete UTXOs for other addresses, reject
        if ( !is_full_node_ && address != GetAddress() )
        {
            genius_account_logger()->warn( "Non-full node cannot delete UTXOs for other addresses" );
            return;
        }

        std::unique_lock lock( utxos_mutex_ );
        if ( auto it = utxos_.find( address ); it != utxos_.end() )
        {
            auto &utxo_list = it->second;
            for ( auto utxo_it = utxo_list.begin(); utxo_it != utxo_list.end(); )
            {
                if ( utxo_it->GetTxID() == utxo_id )
                {
                    utxo_it = utxo_list.erase( utxo_it );
                    continue;
                }
                ++utxo_it;
            }
        }
    }

    bool GeniusAccount::ConsumeUTXOs( const std::vector<InputUTXOInfo> &infos )
    {
        bool             consumed = false;
        std::unique_lock lock( utxos_mutex_ );
        for ( auto &kv : utxos_ )
        {
            auto &utxo_list = kv.second;
            auto  old_size  = utxo_list.size();
            utxo_list.erase( std::remove_if( utxo_list.begin(),
                                             utxo_list.end(),
                                             [&infos]( const GeniusUTXO &x )
                                             {
                                                 return std::any_of( infos.begin(),
                                                                     infos.end(),
                                                                     [&x]( const InputUTXOInfo &a )
                                                                     {
                                                                         return ( a.txid_hash_ == x.GetTxID() ) &&
                                                                                ( a.output_idx_ == x.GetOutputIdx() );
                                                                     } );
                                             } ),
                             utxo_list.end() );
            if ( utxo_list.size() != old_size )
            {
                consumed = true;
            }
        }
        return consumed;
    }

    std::vector<GeniusUTXO> GeniusAccount::GetUTXOs( const std::string &address ) const
    {
        // If not a full node and trying to get UTXOs for other addresses, return empty
        if ( !is_full_node_ && address != GetAddress() )
        {
            genius_account_logger()->warn( "Non-full node cannot get UTXOs for other addresses" );
            return {};
        }

        std::shared_lock lock( utxos_mutex_ );
        if ( auto it = utxos_.find( address ); it != utxos_.end() )
        {
            return it->second;
        }
        return {};
    }

    void GeniusAccount::SetUTXOs( const std::vector<GeniusUTXO> &utxos, const std::string &address )
    {
        // If not a full node and trying to set UTXOs for other addresses, reject
        if ( !is_full_node_ && address != GetAddress() )
        {
            genius_account_logger()->warn( "Non-full node cannot set UTXOs for other addresses" );
            return;
        }

        std::unique_lock lock( utxos_mutex_ );
        utxos_[address] = utxos;

        genius_account_logger()->debug( "Set {} UTXOs for address {}", utxos.size(), address.substr( 0, 8 ) );
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

        ethereum::signature_type  signature = nil::crypto3::sign( hashed, eth_keypair->get_private_key() );
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

    outcome::result<std::pair<KeyGenerator::ElGamal, ethereum::EthereumKeyGenerator>> GeniusAccount::
        GenerateGeniusAddress( ISecureStorage &storage, const char *eth_private_key )
    {
        auto load_res = storage.Load( "sgns_key" );

        nil::crypto3::multiprecision::uint256_t key_seed;
        if ( load_res )
        {
            key_seed = nil::crypto3::multiprecision::uint256_t( load_res.value() );
        }
        else
        {
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

            auto hashed = TW::Hash::sha256( signed_secret );

            key_seed = nil::crypto3::multiprecision::uint256_t( hashed );

            BOOST_OUTCOME_TRYV2( auto &&, storage.Save( "sgns_key", key_seed.str() ) );
        }

        KeyGenerator::ElGamal          elgamal_key( key_seed );
        ethereum::EthereumKeyGenerator eth_key( key_seed );

        return std::make_pair( std::move( elgamal_key ), std::move( eth_key ) );
    }

    uint64_t GeniusAccount::GetBalance( const TokenID token_id, const std::string &address ) const
    {
        uint64_t balance = 0;

        // If not a full node and trying to get balance for other addresses, return 0
        if ( !is_full_node_ && address != GetAddress() )
        {
            genius_account_logger()->warn( "Non-full node cannot get balance for other addresses" );
            return 0;
        }

        std::shared_lock lock( utxos_mutex_ );
        if ( auto it = utxos_.find( address ); it != utxos_.end() )
        {
            for ( const auto &utxo : it->second )
            {
                if ( !utxo.GetLock() && token_id.Equals( utxo.GetTokenID() ) )
                {
                    balance += utxo.GetAmount();
                }
            }
        }
        return balance;
    }

    void GeniusAccount::SetLocalConfirmedNonce( uint64_t nonce )
    {
        genius_account_logger()->debug( "Setting local confirmed nonce to {}", nonce );
        SetPeerConfirmedNonce( nonce, eth_keypair->GetEntirePubValue() );
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

        if ( address == eth_keypair->GetEntirePubValue() )
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

        if ( address == eth_keypair->GetEntirePubValue() )
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
        return GetPeerNonce( eth_keypair->GetEntirePubValue() );
    }

    outcome::result<uint64_t> GeniusAccount::GetConfirmedNonce( uint64_t timeout_ms ) const
    {
        uint64_t result = 0;
        genius_account_logger()->debug( "Trying to get nonce from the network for {} ms ", timeout_ms );

        auto latest_nonce_result = messenger_->GetLatestNonce( std::move( timeout_ms ) );
        if ( latest_nonce_result.has_value() )
        {
            result = latest_nonce_result.value();
            genius_account_logger()->debug( "Nonce replied with value {}", result );
        }
        else if ( latest_nonce_result.error() == AccountMessenger::Error::NO_RESPONSE_RECEIVED )
        {
            genius_account_logger()->debug( "Network didn't answer nonce request " );
            return latest_nonce_result;
        }
        else if ( latest_nonce_result.error() == AccountMessenger::Error::RESPONSE_WITHOUT_NONCE )
        {
            genius_account_logger()->debug( "No nonce information on the network, get local data " );
            return GetLocalConfirmedNonce();
        }
        return result;
    }
}
