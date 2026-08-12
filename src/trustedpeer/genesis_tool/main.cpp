#include <boost/range/concepts.hpp>

#include <nil/crypto3/algebra/marshalling.hpp>
#include <nil/crypto3/pubkey/algorithm/sign.hpp>
#include <nil/crypto3/pubkey/algorithm/verify.hpp>

#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <charconv>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "account/BurnConfig.hpp"
#include "base/logger.hpp"
#include "crdt/globaldb/GlobalDbNetworkComposition.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "trustedpeer/GenesisManifest.hpp"
#include "trustedpeer/QuorumPolicy.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"
#include "trustedpeer/genesis_tool/GenesisCeremony.hpp"
#include "trustedpeer/genesis_tool/LocalTrustAdmin.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::trustedpeer;

    constexpr std::string_view POLICY_DOMAIN = "trusted-peer";
    constexpr std::string_view BURN_DOMAIN = "burn-config";

    void PrintHelp( std::ostream &out )
    {
        out << "Usage: sgns-trust <operation> [options]\n"
               "\nLocal operations:\n"
               "  genesis          review and submit one trusted-peer genesis\n"
               "  list             list authenticated current-head candidates\n"
               "  propose-policy   explicitly propose one policy successor\n"
               "  propose-burn     explicitly propose one burn successor\n"
               "  approve          explicitly approve one exact candidate ID\n";
    }

    struct Arguments
    {
        std::string operation;
        std::map<std::string, std::string> values;
        std::set<std::string> flags;
    };

    std::optional<Arguments> ParseArguments( int argc, char **argv, std::ostream &errors )
    {
        if ( argc < 2 )
            return std::nullopt;
        Arguments parsed;
        parsed.operation = argv[1];
        const std::set<std::string> flag_options{ "--key-stdin" };
        for ( int i = 2; i < argc; ++i )
        {
            const std::string option = argv[i];
            if ( option.rfind( "--", 0 ) != 0 )
            {
                errors << "unexpected positional argument\n";
                return std::nullopt;
            }
            if ( flag_options.count( option ) != 0 )
            {
                if ( !parsed.flags.insert( option ).second )
                {
                    errors << "duplicate option: " << option << '\n';
                    return std::nullopt;
                }
                continue;
            }
            if ( i + 1 >= argc || std::string( argv[i + 1] ).rfind( "--", 0 ) == 0 )
            {
                errors << "missing value for option: " << option << '\n';
                return std::nullopt;
            }
            if ( !parsed.values.emplace( option, argv[++i] ).second )
            {
                errors << "duplicate option: " << option << '\n';
                return std::nullopt;
            }
        }
        return parsed;
    }

    bool ValidateOptions( const Arguments &arguments, std::ostream &errors )
    {
        static const std::set<std::string> operations{ "genesis", "list", "propose-policy", "propose-burn", "approve" };
        if ( operations.count( arguments.operation ) == 0 )
        {
            errors << "unknown local operation: " << arguments.operation << '\n';
            return false;
        }

        std::set<std::string> allowed{ "--manifest", "--network-config", "--database", "--topic" };
        if ( arguments.operation != "list" )
        {
            allowed.insert( "--key-file" );
            allowed.insert( "--key-stdin" );
        }
        if ( arguments.operation == "genesis" )
            allowed.insert( "--timeout-seconds" );
        else if ( arguments.operation == "propose-policy" )
            allowed.insert( "--candidate" );
        else if ( arguments.operation == "propose-burn" )
            allowed.insert( "--basis-points" );
        else if ( arguments.operation == "approve" )
            allowed.insert( "--candidate-id" );

        for ( const auto &[option, unused] : arguments.values )
        {
            (void)unused;
            if ( allowed.count( option ) == 0 )
            {
                errors << "option is not valid for " << arguments.operation << ": " << option << '\n';
                return false;
            }
        }
        for ( const auto &option : arguments.flags )
        {
            if ( allowed.count( option ) == 0 )
            {
                errors << "option is not valid for " << arguments.operation << ": " << option << '\n';
                return false;
            }
        }
        for ( const auto *required : { "--manifest", "--network-config", "--database", "--topic" } )
        {
            if ( arguments.values.count( required ) == 0 )
            {
                errors << "required option missing: " << required << '\n';
                return false;
            }
        }
        if ( arguments.operation != "list" )
        {
            const bool file = arguments.values.count( "--key-file" ) != 0;
            const bool input = arguments.flags.count( "--key-stdin" ) != 0;
            if ( file == input )
            {
                errors << "select exactly one of --key-file or --key-stdin\n";
                return false;
            }
        }
        if ( arguments.operation == "propose-policy" && arguments.values.count( "--candidate" ) == 0 )
            return errors << "required option missing: --candidate\n", false;
        if ( arguments.operation == "propose-burn" && arguments.values.count( "--basis-points" ) == 0 )
            return errors << "required option missing: --basis-points\n", false;
        if ( arguments.operation == "approve" && arguments.values.count( "--candidate-id" ) == 0 )
            return errors << "required option missing: --candidate-id\n", false;
        return true;
    }

    std::optional<std::vector<uint8_t>> ReadBoundedFile( const std::string &path, size_t maximum )
    {
        std::ifstream input( path, std::ios::binary );
        if ( !input.good() )
            return std::nullopt;
        std::vector<uint8_t> bytes;
        char value = 0;
        while ( input.get( value ) )
        {
            if ( bytes.size() == maximum )
                return std::nullopt;
            bytes.push_back( static_cast<uint8_t>( value ) );
        }
        return input.eof() ? std::optional<std::vector<uint8_t>>( std::move( bytes ) ) : std::nullopt;
    }

    std::optional<uint64_t> ParseUint64( const std::string &value )
    {
        uint64_t result = 0;
        const auto parsed = std::from_chars( value.data(), value.data() + value.size(), result );
        if ( parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() )
            return std::nullopt;
        return result;
    }

    std::optional<sgns::securecrdt::CandidateId> ParseCandidateId( const std::string &value )
    {
        const auto first = value.find( ':' );
        const auto second = first == std::string::npos ? std::string::npos : value.find( ':', first + 1 );
        if ( first == 0 || second == std::string::npos || second + 1 >= value.size() )
            return std::nullopt;
        const auto version = ParseUint64( value.substr( first + 1, second - first - 1 ) );
        const auto hash = value.substr( second + 1 );
        if ( !version || hash.size() != 64 ||
             !std::all_of( hash.begin(), hash.end(), []( char c ) { return ( c >= '0' && c <= '9' ) ||
                                                                         ( c >= 'a' && c <= 'f' ); } ) )
            return std::nullopt;
        return sgns::securecrdt::CandidateId{ value.substr( 0, first ), *version, hash };
    }

    std::string FormatCandidateId( const sgns::securecrdt::CandidateId &id )
    {
        return id.domain + ":" + std::to_string( id.version ) + ":" + id.content_hash;
    }

    outcome::result<GenesisCeremony::Signer> LoadLocalSigner( const Arguments &arguments,
                                                              std::istream &input,
                                                              std::ostream &output )
    {
        auto hooks = GenesisCeremony::DefaultHooks();
        std::string key;
        const auto file = arguments.values.find( "--key-file" );
        if ( file != arguments.values.end() )
        {
            BOOST_OUTCOME_TRY( auto status, hooks.inspect_key_file( file->second ) );
            if ( status.symlink )
                return outcome::failure( GenesisCeremony::Error::KEY_FILE_SYMLINK );
            if ( !status.regular )
                return outcome::failure( GenesisCeremony::Error::KEY_FILE_NOT_REGULAR );
            if ( !status.owner )
                return outcome::failure( GenesisCeremony::Error::KEY_FILE_OWNER );
            if ( status.mode != 0600 )
                return outcome::failure( GenesisCeremony::Error::KEY_FILE_MODE );
            BOOST_OUTCOME_TRY( key, hooks.read_key_file( file->second ) );
        }
        else
        {
            if ( &input == &std::cin && ::isatty( STDIN_FILENO ) == 0 )
                return outcome::failure( GenesisCeremony::Error::INVALID_KEY_SOURCE );
            output << "local signing key (protected stdin): " << std::flush;
            struct termios original
            {
            };
            bool disabled = false;
            if ( &input == &std::cin && ::tcgetattr( STDIN_FILENO, &original ) == 0 )
            {
                auto protected_mode = original;
                protected_mode.c_lflag &= static_cast<tcflag_t>( ~ECHO );
                disabled = ::tcsetattr( STDIN_FILENO, TCSAFLUSH, &protected_mode ) == 0;
            }
            const bool read = static_cast<bool>( std::getline( input, key ) );
            if ( disabled )
            {
                ::tcsetattr( STDIN_FILENO, TCSAFLUSH, &original );
                output << '\n';
            }
            if ( !read )
                return outcome::failure( GenesisCeremony::Error::KEY_FILE_IO );
        }
        outcome::result<GenesisCeremony::Signer> local_signer = hooks.create_signer( key );
        if ( !key.empty() )
            hooks.cleanse( key.data(), key.size() );
        return local_signer;
    }

    class TrustRuntime
    {
    public:
        TrustRuntime( const Arguments &arguments, GenesisManifest manifest ) :
            arguments_( arguments ), manifest_( std::move( manifest ) )
        {
            sgns::crdt::GlobalDbNetworkComposition::Config config;
            config.network_config_path = arguments_.values.at( "--network-config" );
            config.database_path = arguments_.values.at( "--database" );
            config.listen_topic = arguments_.values.at( "--topic" );
            config.broadcast_topic = arguments_.values.at( "--topic" );
            config.logger = sgns::base::createLogger( "sgns-trust" );
            auto created = sgns::crdt::GlobalDbNetworkComposition::Create( std::move( config ) );
            if ( created.has_value() )
                composition_ = created.value();
            else
                composition_error_ = created.error();
        }

        outcome::result<void> Start()
        {
            if ( !composition_ )
                return outcome::failure( composition_error_ ? composition_error_ : std::make_error_code( std::errc::invalid_argument ) );
            return composition_->Start();
        }

        outcome::result<sgns::securecrdt::CandidateId> SubmitGenesis(
            const GenesisManifest &manifest,
            const std::vector<uint8_t> &signature,
            const std::string &address,
            TrustedPeerRegistry::SignCallback sign )
        {
            BOOST_OUTCOME_TRY( Prepare( address, std::move( sign ), signature ) );
            return registry_->SubmitReviewedGenesisApproval();
        }

        outcome::result<void> PrepareAdmin( const GenesisCeremony::Signer &signer )
        {
            return Prepare( signer.address, signer.sign, {} );
        }

        outcome::result<std::optional<ConfirmedTrustSnapshot>> Confirmed() const
        {
            if ( !store_ )
                return std::optional<ConfirmedTrustSnapshot>{};
            auto loaded = store_->LoadAndVerify();
            if ( loaded.has_error() )
            {
                if ( loaded.error() == TrustStateStore::Error::NOT_FOUND )
                    return std::optional<ConfirmedTrustSnapshot>{};
                return loaded.error();
            }
            return std::optional<ConfirmedTrustSnapshot>( loaded.value() );
        }

        std::shared_ptr<TrustedPeerRegistry> registry() const { return registry_; }
        std::shared_ptr<sgns::account::BurnConfig> burn_config() const { return burn_config_; }

    private:
        outcome::result<void> Prepare( const std::string &address,
                                       TrustedPeerRegistry::SignCallback sign,
                                       const std::vector<uint8_t> &bootstrap_signature )
        {
            if ( !composition_ || !composition_->db() )
                return outcome::failure( std::errc::not_connected );
            secure_crdt_ = std::make_shared<sgns::securecrdt::SecureCrdt>(
                composition_->db(), arguments_.values.at( "--topic" ) );
            BOOST_OUTCOME_TRY( store_, TrustStateStore::Open(
                arguments_.values.at( "--database" ) + "/trust-state", manifest_.network_id ) );
            BOOST_OUTCOME_TRY( registry_, TrustedPeerRegistry::NewProduction(
                secure_crdt_, store_, manifest_, bootstrap_signature, address, sign ) );
            BOOST_OUTCOME_TRY( burn_config_, sgns::account::BurnConfig::NewProduction(
                secure_crdt_, registry_, store_, address, std::move( sign ) ) );
            if ( !secure_crdt_->RegisterFilters() )
                return outcome::failure( std::errc::operation_not_permitted );
            return outcome::success();
        }

        const Arguments &arguments_;
        GenesisManifest manifest_;
        std::error_code composition_error_;
        std::shared_ptr<sgns::crdt::GlobalDbNetworkComposition> composition_;
        std::shared_ptr<sgns::securecrdt::SecureCrdt> secure_crdt_;
        std::shared_ptr<TrustStateStore> store_;
        std::shared_ptr<TrustedPeerRegistry> registry_;
        std::shared_ptr<sgns::account::BurnConfig> burn_config_;
    };
} // namespace

int main( int argc, char **argv )
{
    if ( argc == 2 && std::string( argv[1] ) == "--help" )
    {
        PrintHelp( std::cout );
        return EXIT_SUCCESS;
    }
    auto arguments = ParseArguments( argc, argv, std::cerr );
    if ( !arguments || !ValidateOptions( *arguments, std::cerr ) )
    {
        PrintHelp( std::cerr );
        return EXIT_FAILURE;
    }

    auto manifest_bytes = ReadBoundedFile( arguments->values.at( "--manifest" ), 65536 );
    auto manifest = manifest_bytes ? GenesisManifest::DecodeCanonical( *manifest_bytes ) : std::nullopt;
    if ( !manifest )
    {
        std::cerr << "manifest must contain canonical GenesisManifest bytes\n";
        return EXIT_FAILURE;
    }

    TrustRuntime runtime( *arguments, *manifest );
    if ( arguments->operation == "genesis" )
    {
        GenesisCeremony::Request request;
        request.manifest = *manifest;
        if ( const auto key = arguments->values.find( "--key-file" ); key != arguments->values.end() )
            request.key_file = key->second;
        request.key_stdin = arguments->flags.count( "--key-stdin" ) != 0;
        if ( const auto timeout = arguments->values.find( "--timeout-seconds" ); timeout != arguments->values.end() )
        {
            const auto seconds = ParseUint64( timeout->second );
            if ( !seconds || *seconds > 86400 )
            {
                std::cerr << "invalid --timeout-seconds\n";
                return EXIT_FAILURE;
            }
            request.confirmation_timeout = std::chrono::seconds( *seconds );
        }
        GenesisCeremony::Network network;
        network.start = [&] { return runtime.Start(); };
        network.submit = [&]( const GenesisManifest &value,
                              const std::vector<uint8_t> &signature,
                              const std::string &address,
                              TrustedPeerRegistry::SignCallback sign )
        { return runtime.SubmitGenesis( value, signature, address, std::move( sign ) ); };
        network.confirmed = [&] { return runtime.Confirmed(); };
        GenesisCeremony ceremony;
        auto result = ceremony.Run( request, network, std::cin, std::cout, std::cerr );
        return result.has_value() ? EXIT_SUCCESS : ( std::cerr << result.error().message() << '\n', EXIT_FAILURE );
    }

    GenesisCeremony::Signer signer;
    if ( arguments->operation != "list" )
    {
        auto loaded = LoadLocalSigner( *arguments, std::cin, std::cout );
        if ( loaded.has_error() )
        {
            std::cerr << loaded.error().message() << '\n';
            return EXIT_FAILURE;
        }
        signer = std::move( loaded.value() );
    }
    if ( runtime.Start().has_error() || runtime.PrepareAdmin( signer ).has_error() )
    {
        std::cerr << "unable to start local trust administration\n";
        return EXIT_FAILURE;
    }

    LocalTrustAdmin admin( runtime.registry(), runtime.burn_config() );
    if ( arguments->operation == "list" )
    {
        auto listed = admin.ListCandidates();
        if ( listed.has_error() )
            return std::cerr << listed.error().message() << '\n', EXIT_FAILURE;
        for ( const auto &candidate : listed.value() )
            std::cout << ( candidate.type == LocalTrustAdmin::CandidateType::Policy ? "policy " : "burn " )
                      << FormatCandidateId( candidate.id ) << '\n';
        return EXIT_SUCCESS;
    }
    if ( arguments->operation == "propose-policy" )
    {
        auto bytes = ReadBoundedFile( arguments->values.at( "--candidate" ), 65536 );
        auto candidate = bytes ? QuorumPolicyState::DecodeCanonical( *bytes ) : std::nullopt;
        if ( !candidate )
            return std::cerr << "invalid canonical policy candidate\n", EXIT_FAILURE;
        auto proposed = admin.ProposePolicy( *candidate );
        if ( proposed.has_error() )
            return std::cerr << proposed.error().message() << '\n', EXIT_FAILURE;
        std::cout << FormatCandidateId( proposed.value() ) << '\n';
        return EXIT_SUCCESS;
    }
    if ( arguments->operation == "propose-burn" )
    {
        const auto basis_points = ParseUint64( arguments->values.at( "--basis-points" ) );
        if ( !basis_points )
            return std::cerr << "invalid basis points\n", EXIT_FAILURE;
        auto proposed = admin.ProposeBurn( *basis_points );
        if ( proposed.has_error() )
            return std::cerr << proposed.error().message() << '\n', EXIT_FAILURE;
        std::cout << FormatCandidateId( proposed.value() ) << '\n';
        return EXIT_SUCCESS;
    }

    const auto candidate = ParseCandidateId( arguments->values.at( "--candidate-id" ) );
    if ( !candidate )
        return std::cerr << "invalid candidate ID\n", EXIT_FAILURE;
    auto approved = admin.Approve( *candidate );
    if ( approved.has_error() )
        return std::cerr << approved.error().message() << '\n', EXIT_FAILURE;
    std::cout << FormatCandidateId( approved.value() ) << '\n';
    return EXIT_SUCCESS;
}
