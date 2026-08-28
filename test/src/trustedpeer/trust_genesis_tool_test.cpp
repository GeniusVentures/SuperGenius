#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include <boost/filesystem/operations.hpp>
#include <openssl/crypto.h>

#include "account/GeniusAccount.hpp"
#include "account/GeniusSigner.hpp"
#include "base/hexutil.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/securecrdt_test_node.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "storage/rocksdb/rocksdb_batch.hpp"
#include "testutil/remove_all.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"
#include "trustedpeer/genesis_tool/GenesisCeremony.hpp"
#include "trustedpeer/genesis_tool/GenesisCeremonyPlatform.hpp"
#include "trustedpeer/genesis_tool/LocalTrustAdmin.hpp"

namespace
{
    using namespace sgns;
    using namespace sgns::trustedpeer;

    constexpr char PRIVATE_KEY[] = "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa";

    GeniusSigner::PrivateKey PrivateKeyFromHex()
    {
        auto key_bytes = sgns::base::unhex( PRIVATE_KEY );
        EXPECT_FALSE( key_bytes.has_error() );
        GeniusSigner::PrivateKey secret_key{};
        if ( !key_bytes.has_error() && key_bytes.value().size() == secret_key.size() )
        {
            std::copy( key_bytes.value().begin(), key_bytes.value().end(), secret_key.begin() );
        }
        return secret_key;
    }

    class TrustGenesisToolTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            path_ = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
            boost::filesystem::create_directories( path_ );
            key_path_ = path_ / "bootstrap.key";
            WriteKeyFile();

            signers_.emplace_back( PrivateKeyFromHex() );
            signers_.push_back( GeniusSigner::Generate() );
            signers_.push_back( GeniusSigner::Generate() );
            signers_.push_back( GeniusSigner::Generate() );
            const auto &bootstrapper = signers_.front();
            manifest_.network_id = 42;
            manifest_.bootstrapper_public_key = bootstrapper.GetAddress();
            manifest_.peers = { bootstrapper.GetAddress(), signers_[1].GetAddress(), signers_[2].GetAddress() };
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
            test::removeAllWithRetry( path_.string() );
        }

        void WriteKeyFile()
        {
            std::ofstream out( key_path_.string(), std::ios::binary | std::ios::trunc );
            out << PRIVATE_KEY << '\n';
            out.close();
            ASSERT_EQ( genesis_ceremony_platform::RestrictKeyFileToCurrentUser( key_path_.string() ), 0 );
        }

        GenesisCeremony::Network RealNetwork()
        {
            node_ = test::securecrdt::MakeSecureCrdtTestNode( "trust_genesis_tool" );
            EXPECT_NE( node_, nullptr );
            secure_crdt_ = std::make_shared<securecrdt::SecureCrdt>( node_->db, "trust-genesis-tool-topic" );
            store_ = TrustStateStore::Open(
                ( path_ / "trust" ).string(),
                manifest_.network_id,
                [this]( sgns::storage::rocksdb &database,
                        const std::vector<TrustStateStore::Write> &writes ) -> outcome::result<void>
                {
                    if ( fail_commits_.load() )
                        return outcome::failure( std::errc::io_error );
                    auto batch = database.batch();
                    if ( !batch )
                        return outcome::failure( std::errc::io_error );
                    for ( const auto &[key, value] : writes )
                    {
                        auto put = batch->put( key, value );
                        if ( put.has_error() )
                            return put.error();
                    }
                    return batch->commit();
                } ).value();

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

        void ConfirmForAdmin()
        {
            GenesisCeremony ceremony;
            EXPECT_EQ( Run( ceremony, RealNetwork(), manifest_.Fingerprint().value() + "\n" ),
                       GenesisCeremony::Error::SUCCESS );
            registry_.reset();
            auto rebuilt = TrustedPeerRegistry::NewProduction(
                secure_crdt_,
                store_,
                manifest_,
                {},
                signers_[0].GetAddress(),
                [this]( const std::vector<uint8_t> &bytes )
                {
                    ++admin_sign_invocations_;
                    return signers_[0].Sign( bytes );
                } );
            ASSERT_TRUE( rebuilt.has_value() ) << rebuilt.error().message();
            registry_ = rebuilt.value();
            auto burn = account::BurnConfig::NewProduction(
                secure_crdt_,
                registry_,
                store_,
                signers_[0].GetAddress(),
                [this]( const std::vector<uint8_t> &bytes )
                {
                    ++admin_sign_invocations_;
                    return signers_[0].Sign( bytes );
                } );
            ASSERT_TRUE( burn.has_value() ) << burn.error().message();
            burn_config_ = burn.value();
        }

        QuorumPolicyState Successor( bool alternate = false ) const
        {
            auto current = registry_->GetConfirmedSnapshot().value().policy;
            const auto hash = current.Hash().value();
            current.version += 1;
            current.expected_previous_hash = hash;
            current.authorizing_policy_hash = hash;
            if ( alternate )
                current.peers = { signers_[0].GetAddress(), signers_[1].GetAddress(), signers_[3].GetAddress() };
            return current.Canonicalized().value();
        }

        securecrdt::CandidateId SubmitRemotePolicyApproval( const QuorumPolicyState &candidate )
        {
            const auto core = TrustedPeerRegistry::PolicyCandidateCore( candidate ).value();
            const auto bytes = core.CanonicalBytes().value();
            return secure_crdt_->SubmitCandidateApproval(
                { securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
                  core,
                  signers_[1].GetAddress(),
                  signers_[1].Sign( bytes ) } ).value();
        }

        void ConfirmInitialBurn()
        {
            auto local = burn_config_->OnTrustedPeerGenesisConfirmed();
            ASSERT_TRUE( local.has_value() ) << local.error().message();
            const auto snapshot = store_->LoadAndVerify().value();
            const auto core = account::BurnConfig::BurnCandidateCore( snapshot.burn ).value();
            const auto bytes = core.CanonicalBytes().value();
            ASSERT_TRUE( secure_crdt_->SubmitCandidateApproval(
                { securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
                  core,
                  signers_[1].GetAddress(),
                  signers_[1].Sign( bytes ) } ).has_value() );
            ASSERT_TRUE( burn_config_->TryActivateBurnCandidate( local.value() ).has_value() );
            ASSERT_TRUE( burn_config_->IsEconomicallyReady() );
        }

        securecrdt::CandidateId SubmitRemoteInitialBurnApproval()
        {
            const auto snapshot = store_->LoadAndVerify().value();
            const auto core = account::BurnConfig::BurnCandidateCore( snapshot.burn ).value();
            const auto bytes = core.CanonicalBytes().value();
            return secure_crdt_->SubmitCandidateApproval(
                { securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
                  core,
                  signers_[1].GetAddress(),
                  signers_[1].Sign( bytes ) } ).value();
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
        std::shared_ptr<account::BurnConfig> burn_config_;
        std::vector<GeniusSigner> signers_;
        std::atomic_uint32_t admin_sign_invocations_{ 0 };
        std::atomic_bool fail_commits_{ false };
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

TEST_F( TrustGenesisToolTest, SecretConfirmationFailureRetainsKeyAndProducesNoConfirmedRecord )
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
    { return outcome::failure( std::errc::io_error ); };
    EXPECT_EQ( Run( ceremony, std::move( network ), manifest_.Fingerprint().value() + "\n" ),
               GenesisCeremony::Error::CONFIRMATION_FAILED );
    EXPECT_TRUE( boost::filesystem::exists( key_path_ ) );
    EXPECT_EQ( store_, nullptr );
    EXPECT_NE( captured_errors_.find( "CRITICAL" ), std::string::npos );
}

TEST_F( TrustGenesisToolTest, ArgvEnvironmentAndStructuredLogSurfacesExcludeSecretBytes )
{
    const std::vector<std::string> argv_capture = {
        "sgns-trust", "genesis", "--manifest", ( path_ / "manifest" ).string(),
        "--network-config", ( path_ / "network.json" ).string(), "--database", path_.string(),
        "--topic", "existing-production-topic", "--key-file", key_path_.string()
    };
    const std::map<std::string, std::string> environment_capture = {
        { "PATH", "/usr/bin" }, { "SGNS_NETWORK", "42" }
    };
    const std::vector<std::string> structured_logs;
    for ( const auto &argument : argv_capture )
        EXPECT_EQ( argument.find( PRIVATE_KEY ), std::string::npos );
    for ( const auto &[name, value] : environment_capture )
    {
        EXPECT_EQ( name.find( PRIVATE_KEY ), std::string::npos );
        EXPECT_EQ( value.find( PRIVATE_KEY ), std::string::npos );
    }
    for ( const auto &entry : structured_logs )
        EXPECT_EQ( entry.find( PRIVATE_KEY ), std::string::npos );
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

TEST_F( TrustGenesisToolTest, AdminReceiptAndListNeverSignWhileExplicitProposeSignsOnce )
{
    ConfirmForAdmin();
    ConfirmInitialBurn();
    const auto candidate = Successor();
    const auto id = SubmitRemotePolicyApproval( candidate );
    admin_sign_invocations_.store( 0 );

    LocalTrustAdmin admin( registry_, burn_config_ );
    auto listed = admin.ListCandidates();
    ASSERT_TRUE( listed.has_value() ) << listed.error().message();
    ASSERT_NE( std::find_if( listed.value().begin(), listed.value().end(),
                            [&]( const auto &item ) { return item.id == id; } ),
               listed.value().end() );
    EXPECT_EQ( admin_sign_invocations_.load(), 0U );

    auto proposed = admin.ProposePolicy( candidate );
    ASSERT_TRUE( proposed.has_value() ) << proposed.error().message();
    EXPECT_EQ( proposed.value(), id );
    EXPECT_EQ( admin_sign_invocations_.load(), 1U );
    EXPECT_EQ( store_->LoadAndVerify().value().policy, candidate.Canonicalized().value() );
    EXPECT_EQ( admin_sign_invocations_.load(), 1U );
}

TEST_F( TrustGenesisToolTest, AdminApproveTargetsOnlyExactCandidateId )
{
    ConfirmForAdmin();
    ConfirmInitialBurn();
    const auto first = SubmitRemotePolicyApproval( Successor() );
    const auto second = SubmitRemotePolicyApproval( Successor( true ) );
    admin_sign_invocations_.store( 0 );

    LocalTrustAdmin admin( registry_, burn_config_ );
    ASSERT_TRUE( admin.Approve( first ).has_value() );
    EXPECT_EQ( admin_sign_invocations_.load(), 1U );
    const auto first_approvals = secure_crdt_->ReadCandidateApprovals( first ).value();
    const auto second_approvals = secure_crdt_->ReadCandidateApprovals( second ).value();
    EXPECT_EQ( first_approvals.size(), 2U );
    EXPECT_EQ( second_approvals.size(), 1U );
    EXPECT_TRUE( std::none_of( second_approvals.begin(), second_approvals.end(), [&]( const auto &approval ) {
        return approval.signer == signers_[0].GetAddress();
    } ) );
}

TEST_F( TrustGenesisToolTest, AdminExplicitBurnProposalContributesOneApproval )
{
    ConfirmForAdmin();
    ConfirmInitialBurn();
    admin_sign_invocations_.store( 0 );

    LocalTrustAdmin admin( registry_, burn_config_ );
    auto proposed = admin.ProposeBurn( 250 );
    ASSERT_TRUE( proposed.has_value() ) << proposed.error().message();
    EXPECT_EQ( admin_sign_invocations_.load(), 1U );
    EXPECT_EQ( secure_crdt_->ReadCandidateApprovals( proposed.value() ).value().size(), 1U );
}

TEST_F( TrustGenesisToolTest, AdminInitialBurnGatePreservesBurnV1Approval )
{
    ConfirmForAdmin();
    const auto policy = Successor();
    const auto policy_id = SubmitRemotePolicyApproval( policy );
    admin_sign_invocations_.store( 0 );

    LocalTrustAdmin admin( registry_, burn_config_ );
    auto rejected_proposal = admin.ProposePolicy( policy );
    ASSERT_TRUE( rejected_proposal.has_error() );
    EXPECT_EQ( rejected_proposal.error(), std::make_error_code( std::errc::operation_not_permitted ) );
    auto rejected_approval = admin.Approve( policy_id );
    ASSERT_TRUE( rejected_approval.has_error() );
    EXPECT_EQ( rejected_approval.error(), std::make_error_code( std::errc::operation_not_permitted ) );
    EXPECT_EQ( admin_sign_invocations_.load(), 0U );

    const auto burn_id = SubmitRemoteInitialBurnApproval();
    auto approved_burn = admin.Approve( burn_id );
    ASSERT_TRUE( approved_burn.has_value() ) << approved_burn.error().message();
    EXPECT_EQ( approved_burn.value(), burn_id );
    EXPECT_EQ( admin_sign_invocations_.load(), 1U );
    EXPECT_TRUE( burn_config_->IsEconomicallyReady() );
}

TEST_F( TrustGenesisToolTest, AdminActivationFailureIsReturnedWhileUnderQuorumRemainsPending )
{
    ConfirmForAdmin();
    ConfirmInitialBurn();
    LocalTrustAdmin admin( registry_, burn_config_ );
    const auto candidate = Successor( true );
    const auto durable_before = store_->LoadAndVerify().value();

    auto pending = admin.ProposePolicy( candidate );
    ASSERT_TRUE( pending.has_value() ) << pending.error().message();
    EXPECT_EQ( store_->LoadAndVerify().value(), durable_before );

    const auto core = TrustedPeerRegistry::PolicyCandidateCore( candidate ).value();
    const auto bytes = core.CanonicalBytes().value();
    ASSERT_TRUE( secure_crdt_->SubmitCandidateApproval(
        { securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
          core,
          signers_[1].GetAddress(),
          signers_[1].Sign( bytes ) } ).has_value() );
    fail_commits_.store( true );
    auto failed = admin.Approve( pending.value() );
    ASSERT_TRUE( failed.has_error() );
    EXPECT_EQ( failed.error(), TrustStateStore::Error::COMMIT_FAILED );
    EXPECT_EQ( store_->LoadAndVerify().value(), durable_before );
}
