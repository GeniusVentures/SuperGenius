#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <boost/filesystem/operations.hpp>
#include <openssl/crypto.h>

#include "account/GeniusAccount.hpp"
#include "account/GeniusSigner.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/securecrdt_test_node.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"
#include "trustedpeer/genesis_tool/GenesisCeremony.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::trustedpeer;

    constexpr char PRIVATE_KEY[] = "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa";

    class TrustGenesisToolTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            path_ = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
            boost::filesystem::create_directories( path_ );
            key_path_ = path_ / "bootstrap.key";
            WriteKeyFile();

            GeniusSigner bootstrapper{ ethereum::EthereumKeyGenerator( PRIVATE_KEY ) };
            manifest_.network_id = 42;
            manifest_.bootstrapper_public_key = bootstrapper.GetAddress();
            manifest_.peers = { bootstrapper.GetAddress(), GeniusSigner::Generate().GetAddress(),
                                GeniusSigner::Generate().GetAddress() };
            manifest_.membership_threshold = 2;
            manifest_.burn_threshold = 2;
            manifest_ = manifest_.Canonicalized().value();
        }

        void TearDown() override
        {
            registry_.reset();
            secure_crdt_.reset();
            node_.reset();
            store_.reset();
            GeniusAccount::SetSecureStorageFactory( nullptr );
            boost::filesystem::remove_all( path_ );
        }

        void WriteKeyFile()
        {
            std::ofstream out( key_path_.string(), std::ios::binary | std::ios::trunc );
            out << PRIVATE_KEY << '\n';
            out.close();
            ASSERT_EQ( ::chmod( key_path_.c_str(), S_IRUSR | S_IWUSR ), 0 );
        }

        GenesisCeremony::Network RealNetwork()
        {
            node_ = test::securecrdt::MakeSecureCrdtTestNode( "trust_genesis_tool" );
            EXPECT_NE( node_, nullptr );
            secure_crdt_ = std::make_shared<securecrdt::SecureCrdt>( node_->db, "trust-genesis-tool-topic" );
            store_ = TrustStateStore::Open( ( path_ / "trust" ).string(), manifest_.network_id ).value();

            GenesisCeremony::Network network;
            network.start = [] { return outcome::success(); };
            network.submit = [this]( const GenesisManifest &manifest,
                                     const std::vector<uint8_t> &manifest_signature,
                                     const std::string &address,
                                     TrustedPeerRegistry::SignCallback sign )
                -> outcome::result<securecrdt::CandidateId>
            {
                auto created = TrustedPeerRegistry::NewProduction(
                    secure_crdt_, store_, manifest, manifest_signature, address, std::move( sign ) );
                if ( created.has_error() )
                    return created.error();
                registry_ = created.value();
                if ( !secure_crdt_->RegisterFilters() )
                    return outcome::failure( std::errc::operation_not_permitted );
                return registry_->SubmitReviewedGenesisApproval();
            };
            network.confirmed = [this]() -> outcome::result<std::optional<ConfirmedTrustSnapshot>>
            {
                auto loaded = store_->LoadAndVerify();
                if ( loaded.has_error() )
                {
                    if ( loaded.error() == TrustStateStore::Error::NOT_FOUND )
                        return std::optional<ConfirmedTrustSnapshot>{};
                    return loaded.error();
                }
                return std::optional<ConfirmedTrustSnapshot>( loaded.value() );
            };
            return network;
        }

        GenesisCeremony::Request Request() const
        {
            GenesisCeremony::Request request;
            request.manifest = manifest_;
            request.key_file = key_path_.string();
            request.confirmation_timeout = std::chrono::milliseconds( 50 );
            request.poll_interval = std::chrono::milliseconds( 1 );
            return request;
        }

        GenesisCeremony::Error Run( GenesisCeremony &ceremony,
                                    GenesisCeremony::Network network,
                                    std::string confirmation )
        {
            std::istringstream input( std::move( confirmation ) );
            std::ostringstream output;
            std::ostringstream errors;
            auto result = ceremony.Run( Request(), network, input, output, errors );
            captured_output_ = output.str();
            captured_errors_ = errors.str();
            return result.has_error() ? static_cast<GenesisCeremony::Error>( result.error().value() )
                                      : GenesisCeremony::Error::SUCCESS;
        }

        boost::filesystem::path path_;
        boost::filesystem::path key_path_;
        GenesisManifest manifest_;
        std::unique_ptr<test::securecrdt::SecureCrdtTestNode> node_;
        std::shared_ptr<securecrdt::SecureCrdt> secure_crdt_;
        std::shared_ptr<TrustStateStore> store_;
        std::shared_ptr<TrustedPeerRegistry> registry_;
        std::string captured_output_;
        std::string captured_errors_;
    };
}

TEST_F( TrustGenesisToolTest, SecretFileReviewSubmitsDurablyCleansesThenUnlinks )
{
    std::vector<std::string> lifecycle;
    size_t account_storage_calls = 0;
    GeniusAccount::SetSecureStorageFactory(
        [&]( const std::string & ) -> std::shared_ptr<ISecureStorage>
        {
            ++account_storage_calls;
            return nullptr;
        } );

    GenesisCeremony::Hooks hooks = GenesisCeremony::DefaultHooks();
    hooks.cleanse = [&]( void *data, size_t size )
    {
        lifecycle.emplace_back( "cleanse" );
        OPENSSL_cleanse( data, size );
    };
    hooks.unlink_file = [&]( const std::string &path )
    {
        lifecycle.emplace_back( "unlink" );
        return ::unlink( path.c_str() );
    };
    GenesisCeremony ceremony( std::move( hooks ) );

    EXPECT_EQ( Run( ceremony, RealNetwork(), manifest_.Fingerprint().value() + "\n" ),
               GenesisCeremony::Error::SUCCESS );
    ASSERT_EQ( lifecycle, ( std::vector<std::string>{ "cleanse", "unlink" } ) );
    EXPECT_FALSE( boost::filesystem::exists( key_path_ ) );
    ASSERT_TRUE( store_->LoadAndVerify().has_value() );
    EXPECT_EQ( store_->LoadAndVerify().value().genesis_fingerprint, manifest_.Fingerprint().value() );
    EXPECT_NE( captured_output_.find( "network: 42" ), std::string::npos );
    EXPECT_NE( captured_output_.find( "membership threshold: 2" ), std::string::npos );
    EXPECT_NE( captured_output_.find( "burn threshold: 2" ), std::string::npos );
    EXPECT_NE( captured_output_.find( "initial burn basis points: 100" ), std::string::npos );
    for ( const auto &peer : manifest_.peers )
        EXPECT_NE( captured_output_.find( peer ), std::string::npos );
    EXPECT_NE( captured_output_.find( manifest_.Fingerprint().value() ), std::string::npos );
    EXPECT_EQ( account_storage_calls, 0U );
    EXPECT_EQ( captured_output_.find( PRIVATE_KEY ), std::string::npos );
    EXPECT_EQ( captured_errors_.find( PRIVATE_KEY ), std::string::npos );
}

TEST_F( TrustGenesisToolTest, WrongFingerprintLeavesSecretAndDoesNotSubmit )
{
    size_t submits = 0;
    GenesisCeremony::Network network;
    network.start = [] { return outcome::success(); };
    network.submit = [&]( const auto &, const auto &, const auto &, auto )
        -> outcome::result<securecrdt::CandidateId>
    {
        ++submits;
        return outcome::failure( std::errc::operation_not_permitted );
    };
    network.confirmed = []() -> outcome::result<std::optional<ConfirmedTrustSnapshot>>
    { return std::optional<ConfirmedTrustSnapshot>{}; };
    GenesisCeremony ceremony;
    EXPECT_EQ( Run( ceremony, std::move( network ), std::string( 64, '0' ) + "\n" ),
               GenesisCeremony::Error::CONFIRMATION_MISMATCH );
    EXPECT_EQ( submits, 0U );
    EXPECT_TRUE( boost::filesystem::exists( key_path_ ) );
}

TEST_F( TrustGenesisToolTest, WrongBootstrapperLeavesSecretAndDoesNotConfirm )
{
    manifest_.bootstrapper_public_key = GeniusSigner::Generate().GetAddress();
    GenesisCeremony ceremony;
    GenesisCeremony::Network network;
    network.start = [] { return outcome::success(); };
    network.submit = []( const auto &, const auto &, const auto &, auto )
        -> outcome::result<securecrdt::CandidateId>
    { return outcome::failure( std::errc::operation_not_permitted ); };
    network.confirmed = []() -> outcome::result<std::optional<ConfirmedTrustSnapshot>>
    { return std::optional<ConfirmedTrustSnapshot>{}; };
    EXPECT_EQ( Run( ceremony, std::move( network ), "unused\n" ), GenesisCeremony::Error::BOOTSTRAPPER_MISMATCH );
    EXPECT_TRUE( boost::filesystem::exists( key_path_ ) );
}

TEST_F( TrustGenesisToolTest, ConfirmationTimeoutLeavesSecretAndReportsCriticalRecovery )
{
    GenesisCeremony ceremony;
    GenesisCeremony::Network network;
    network.start = [] { return outcome::success(); };
    network.submit = []( const GenesisManifest &manifest, const auto &, const auto &, auto )
        -> outcome::result<securecrdt::CandidateId>
    {
        securecrdt::CandidateCore core;
        core.domain = "trusted-peer-genesis";
        core.network_id = manifest.network_id;
        core.kind = securecrdt::CandidateKind::TrustedPeerGenesis;
        core.version = manifest.policy_version;
        core.expected_previous_hash = manifest.Fingerprint().value();
        core.authorizing_policy_hash = manifest.Fingerprint().value();
        core.payload = manifest.CanonicalBytes().value();
        return securecrdt::CandidateId::FromCore( core ).value();
    };
    network.confirmed = []() -> outcome::result<std::optional<ConfirmedTrustSnapshot>>
    { return std::optional<ConfirmedTrustSnapshot>{}; };
    EXPECT_EQ( Run( ceremony, std::move( network ), manifest_.Fingerprint().value() + "\n" ),
               GenesisCeremony::Error::CONFIRMATION_TIMEOUT );
    EXPECT_TRUE( boost::filesystem::exists( key_path_ ) );
    EXPECT_NE( captured_errors_.find( "CRITICAL" ), std::string::npos );
}

TEST_F( TrustGenesisToolTest, UnsafeKeyMetadataReturnsTypedFailuresAndRetainsSecret )
{
    const std::vector<std::pair<GenesisCeremony::KeyFileStatus, GenesisCeremony::Error>> cases = {
        { { true, true, false, true, 0644 }, GenesisCeremony::Error::KEY_FILE_MODE },
        { { true, true, false, false, 0600 }, GenesisCeremony::Error::KEY_FILE_OWNER },
        { { true, true, true, true, 0600 }, GenesisCeremony::Error::KEY_FILE_SYMLINK },
        { { true, false, false, true, 0600 }, GenesisCeremony::Error::KEY_FILE_NOT_REGULAR },
    };

    for ( const auto &[status, expected] : cases )
    {
        SCOPED_TRACE( static_cast<int>( expected ) );
        GenesisCeremony::Hooks hooks = GenesisCeremony::DefaultHooks();
        const auto status_copy = status;
        hooks.inspect_key_file = [status_copy]( const std::string & ) { return outcome::success( status_copy ); };
        GenesisCeremony ceremony( std::move( hooks ) );
        GenesisCeremony::Network network;
        network.start = [] { return outcome::success(); };
        network.submit = []( const auto &, const auto &, const auto &, auto )
            -> outcome::result<securecrdt::CandidateId>
        { return outcome::failure( std::errc::operation_not_permitted ); };
        network.confirmed = []() -> outcome::result<std::optional<ConfirmedTrustSnapshot>>
        { return std::optional<ConfirmedTrustSnapshot>{}; };
        EXPECT_EQ( Run( ceremony, std::move( network ), "unused\n" ), expected );
        EXPECT_TRUE( boost::filesystem::exists( key_path_ ) );
    }
}
