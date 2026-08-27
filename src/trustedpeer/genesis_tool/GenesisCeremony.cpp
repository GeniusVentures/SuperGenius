#include "trustedpeer/genesis_tool/GenesisCeremony.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>

#include <openssl/crypto.h>

#include "account/GeniusSigner.hpp"
#include "base/hexutil.hpp"
#include "trustedpeer/genesis_tool/GenesisCeremonyPlatform.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns::trustedpeer, GenesisCeremony::Error, e )
{
    using Error = sgns::trustedpeer::GenesisCeremony::Error;
    switch ( e )
    {
        case Error::SUCCESS: return "genesis ceremony completed";
        case Error::INVALID_MANIFEST: return "genesis manifest is not canonical and valid";
        case Error::INVALID_KEY_SOURCE: return "select exactly one protected key source";
        case Error::KEY_FILE_IO: return "unable to read the bootstrap key file";
        case Error::KEY_FILE_NOT_REGULAR: return "bootstrap key path is not a regular file";
        case Error::KEY_FILE_SYMLINK: return "bootstrap key path must not be a symlink";
        case Error::KEY_FILE_OWNER: return "bootstrap key file is not owned by this process user";
        case Error::KEY_FILE_MODE: return "bootstrap key file mode must be 0600";
        case Error::INVALID_PRIVATE_KEY: return "bootstrap private key is invalid";
        case Error::BOOTSTRAPPER_MISMATCH: return "derived bootstrapper does not match the manifest";
        case Error::CONFIRMATION_MISMATCH: return "typed fingerprint does not match the reviewed manifest";
        case Error::NETWORK_START_FAILED: return "unable to start the production CRDT composition";
        case Error::SUBMISSION_FAILED: return "unable to submit the reviewed genesis approval";
        case Error::CONFIRMATION_FAILED: return "durable genesis confirmation failed";
        case Error::CONFIRMATION_TIMEOUT: return "durable genesis confirmation timed out";
        case Error::KEY_FILE_UNLINK_FAILED: return "confirmed genesis key file could not be removed";
    }
    return "unknown GenesisCeremony::Error";
}

namespace sgns::trustedpeer
{
    namespace
    {
        constexpr size_t PRIVATE_KEY_HEX_LENGTH = 64;

        bool IsPrivateKeyHex( const std::string &value )
        {
            return value.size() == PRIVATE_KEY_HEX_LENGTH &&
                   std::all_of( value.begin(), value.end(), []( unsigned char c ) { return std::isxdigit( c ) != 0; } );
        }

        void TrimLineEnding( std::string &value )
        {
            while ( !value.empty() && ( value.back() == '\n' || value.back() == '\r' ) )
                value.pop_back();
        }

        void WriteCriticalRetention( const GenesisCeremony::Request &request, std::ostream &errors )
        {
            if ( request.key_file )
                errors << "CRITICAL: genesis is not durably confirmed; retain protected key file at "
                       << *request.key_file << " and investigate before retrying.\n";
            else
                errors << "CRITICAL: genesis is not durably confirmed; securely retain the bootstrap key and investigate.\n";
        }
    } // namespace

    GenesisCeremony::Hooks GenesisCeremony::DefaultHooks()
    {
        Hooks hooks;
        hooks.inspect_key_file = genesis_ceremony_platform::InspectKeyFile;
        hooks.read_key_file = genesis_ceremony_platform::ReadKeyFile;
        hooks.create_signer = []( std::string_view private_key ) -> outcome::result<Signer>
        {
            if ( private_key.size() != PRIVATE_KEY_HEX_LENGTH )
                return outcome::failure( Error::INVALID_PRIVATE_KEY );
            auto key_bytes = sgns::base::unhex( private_key );
            if ( key_bytes.has_error() || key_bytes.value().size() != GeniusSigner::PRIVATE_KEY_SIZE )
                return outcome::failure( Error::INVALID_PRIVATE_KEY );
            try
            {
                GeniusSigner::PrivateKey secret_key{};
                std::copy( key_bytes.value().begin(), key_bytes.value().end(), secret_key.begin() );
                std::shared_ptr<GeniusSigner> local_key = std::make_shared<GeniusSigner>( secret_key );
                if ( local_key->GetAddress().empty() )
                    return outcome::failure( Error::INVALID_PRIVATE_KEY );
                return Signer{ local_key->GetAddress(),
                               [local_key]( const std::vector<uint8_t> &bytes ) { return local_key->Sign( bytes ); } };
            }
            catch ( const std::exception & )
            {
                return outcome::failure( Error::INVALID_PRIVATE_KEY );
            }
        };
        hooks.cleanse = []( void *data, size_t size ) { OPENSSL_cleanse( data, size ); };
        hooks.unlink_file = genesis_ceremony_platform::RemoveKeyFile;
        hooks.sleep = []( std::chrono::milliseconds duration ) { std::this_thread::sleep_for( duration ); };
        return hooks;
    }

    GenesisCeremony::GenesisCeremony() : hooks_( DefaultHooks() )
    {
    }

    GenesisCeremony::GenesisCeremony( Hooks hooks ) : hooks_( std::move( hooks ) )
    {
    }

    outcome::result<void> GenesisCeremony::Run( const Request &request,
                                                Network &network,
                                                std::istream &input,
                                                std::ostream &output,
                                                std::ostream &errors ) const
    {
        const auto canonical = request.manifest.Canonicalized();
        const auto manifest_bytes = request.manifest.CanonicalBytes();
        const auto fingerprint = request.manifest.Fingerprint();
        if ( !canonical || !manifest_bytes || !fingerprint || !( *canonical == request.manifest ) )
            return outcome::failure( Error::INVALID_MANIFEST );
        if ( request.key_file.has_value() == request.key_stdin )
            return outcome::failure( Error::INVALID_KEY_SOURCE );
        if ( !network.start || !network.submit || !network.confirmed )
            return outcome::failure( Error::NETWORK_START_FAILED );

        output << "Trusted-peer genesis review\n"
               << "network: " << canonical->network_id << '\n'
               << "bootstrapper: " << canonical->bootstrapper_public_key << '\n'
               << "policy version: " << canonical->policy_version << '\n'
               << "membership threshold: " << canonical->membership_threshold << '\n'
               << "burn threshold: " << canonical->burn_threshold << '\n'
               << "initial burn basis points: " << canonical->initial_burn_basis_points << '\n'
               << "ordered peers:\n";
        for ( const auto &peer : canonical->peers ) output << "  " << peer << '\n';
        output << "fingerprint: " << *fingerprint << '\n';

        std::string private_key;
        if ( request.key_file )
        {
            auto status = hooks_.inspect_key_file( *request.key_file );
            if ( status.has_error() || !status.value().exists )
            {
                WriteCriticalRetention( request, errors );
                return outcome::failure( Error::KEY_FILE_IO );
            }
            if ( status.value().symlink )
            {
                WriteCriticalRetention( request, errors );
                return outcome::failure( Error::KEY_FILE_SYMLINK );
            }
            if ( !status.value().regular )
            {
                WriteCriticalRetention( request, errors );
                return outcome::failure( Error::KEY_FILE_NOT_REGULAR );
            }
            if ( !status.value().owner )
            {
                WriteCriticalRetention( request, errors );
                return outcome::failure( Error::KEY_FILE_OWNER );
            }
            if ( status.value().mode != 0600 )
            {
                WriteCriticalRetention( request, errors );
                return outcome::failure( Error::KEY_FILE_MODE );
            }
            auto read = hooks_.read_key_file( *request.key_file );
            if ( read.has_error() )
            {
                WriteCriticalRetention( request, errors );
                return read.error();
            }
            private_key = std::move( read.value() );
        }
        else
        {
            output << "bootstrap key (protected stdin): " << std::flush;
            const auto read = genesis_ceremony_platform::ReadProtectedLine( input, output, private_key );
            if ( read == genesis_ceremony_platform::ProtectedInputResult::NOT_A_TERMINAL )
                return outcome::failure( Error::INVALID_KEY_SOURCE );
            if ( read != genesis_ceremony_platform::ProtectedInputResult::SUCCESS )
                return outcome::failure( Error::KEY_FILE_IO );
            TrimLineEnding( private_key );
        }

        bool cleansed = false;
        const auto cleanse = [&]
        {
            if ( !cleansed && !private_key.empty() )
            {
                hooks_.cleanse( private_key.data(), private_key.size() );
                cleansed = true;
            }
        };
        if ( !IsPrivateKeyHex( private_key ) )
        {
            cleanse();
            WriteCriticalRetention( request, errors );
            return outcome::failure( Error::INVALID_PRIVATE_KEY );
        }
        outcome::result<Signer> local_signer = hooks_.create_signer( private_key );
        cleanse();
        if ( local_signer.has_error() )
        {
            WriteCriticalRetention( request, errors );
            return local_signer.error();
        }
        if ( local_signer.value().address != canonical->bootstrapper_public_key )
        {
            WriteCriticalRetention( request, errors );
            return outcome::failure( Error::BOOTSTRAPPER_MISMATCH );
        }

        output << "Type the exact fingerprint to submit: " << std::flush;
        std::string typed_fingerprint;
        if ( !std::getline( input, typed_fingerprint ) || typed_fingerprint != *fingerprint )
        {
            WriteCriticalRetention( request, errors );
            return outcome::failure( Error::CONFIRMATION_MISMATCH );
        }

        auto started = network.start();
        if ( started.has_error() )
        {
            WriteCriticalRetention( request, errors );
            return outcome::failure( Error::NETWORK_START_FAILED );
        }
        const std::vector<uint8_t> manifest_signature = local_signer.value().sign( *manifest_bytes );
        auto submitted = network.submit( *canonical,
                                         manifest_signature,
                                         local_signer.value().address,
                                         local_signer.value().sign );
        if ( submitted.has_error() )
        {
            WriteCriticalRetention( request, errors );
            return outcome::failure( Error::SUBMISSION_FAILED );
        }

        const auto deadline = std::chrono::steady_clock::now() + request.confirmation_timeout;
        do
        {
            auto confirmed = network.confirmed();
            if ( confirmed.has_error() )
            {
                WriteCriticalRetention( request, errors );
                return outcome::failure( Error::CONFIRMATION_FAILED );
            }
            if ( confirmed.value() && confirmed.value()->genesis_fingerprint == *fingerprint &&
                 confirmed.value()->genesis == *canonical )
            {
                if ( request.key_file && hooks_.unlink_file( *request.key_file ) != 0 )
                {
                    errors << "CRITICAL: genesis is confirmed but the protected key file could not be removed; "
                              "remove it securely before continuing.\n";
                    return outcome::failure( Error::KEY_FILE_UNLINK_FAILED );
                }
                output << "Genesis durably confirmed.\n";
                return outcome::success();
            }
            if ( request.confirmation_timeout.count() > 0 ) hooks_.sleep( request.poll_interval );
        } while ( std::chrono::steady_clock::now() < deadline );

        WriteCriticalRetention( request, errors );
        return outcome::failure( Error::CONFIRMATION_TIMEOUT );
    }
} // namespace sgns::trustedpeer
