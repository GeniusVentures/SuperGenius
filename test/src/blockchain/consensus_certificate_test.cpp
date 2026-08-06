#include <gtest/gtest.h>

#include "blockchain/Consensus.hpp"

#include "account/GeniusAccount.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

namespace sgns
{
    class ConsensusManagerTestAccess
    {
    public:
        static bool HasProposal( const std::shared_ptr<ConsensusManager> &manager, const std::string &proposal_id )
        {
            return manager && manager->proposals_.find( proposal_id ) != manager->proposals_.end();
        }

        static bool IsProposalQuorumReached( const std::shared_ptr<ConsensusManager> &manager,
                                             const std::string                       &proposal_id )
        {
            if ( !manager )
            {
                return false;
            }
            auto it = manager->proposals_.find( proposal_id );
            return it != manager->proposals_.end() && it->second.quorum_reached;
        }

        static bool HasPendingProposal( const std::shared_ptr<ConsensusManager> &manager,
                                        const std::string                       &proposal_id )
        {
            return manager && manager->pending_entries_.find( proposal_id ) != manager->pending_entries_.end();
        }

        static bool HasSubjectHandler( const std::shared_ptr<ConsensusManager> &manager,
                                       const base::Hash256                     &type_hash )
        {
            return manager && manager->subject_handlers_.find( type_hash ) != manager->subject_handlers_.end();
        }

        static bool HasCertificateSubjectHandler( const std::shared_ptr<ConsensusManager> &manager,
                                                  const base::Hash256                     &type_hash )
        {
            return manager && manager->certificate_subject_handlers_.find( type_hash ) !=
                                  manager->certificate_subject_handlers_.end();
        }

        static bool ValidateSubject( const ConsensusManager::Subject &subject )
        {
            return ConsensusManager::ValidateSubject( subject );
        }

        static void HandleProposal( const std::shared_ptr<ConsensusManager> &manager,
                                    const ConsensusManager::Proposal        &proposal )
        {
            manager->HandleProposal( proposal );
        }

        static void HandleVote( const std::shared_ptr<ConsensusManager> &manager, const ConsensusManager::Vote &vote )
        {
            manager->HandleVote( vote );
        }

        static void HandleCertificate( const std::shared_ptr<ConsensusManager> &manager,
                                       const ConsensusManager::Certificate     &certificate )
        {
            manager->HandleCertificate( certificate );
        }
    };
} // namespace sgns

namespace
{
    constexpr const char *kTestPrivateKey  = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    constexpr const char *kTestPrivateKey2 = "deedbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

    std::shared_ptr<sgns::GeniusAccount> MakeAccount( const std::string &path )
    {
        auto account = sgns::GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ), kTestPrivateKey, path, false );
        EXPECT_TRUE( account );
        if ( !account )
        {
            return nullptr;
        }
        return account;
    }

    std::shared_ptr<sgns::GeniusAccount> MakeAccount( const std::string &path, const char *private_key )
    {
        auto account = sgns::GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ), private_key, path, false );
        EXPECT_TRUE( account );
        if ( !account )
        {
            return nullptr;
        }
        return account;
    }

    std::shared_ptr<sgns::ValidatorRegistry> MakeRegistry( const std::shared_ptr<sgns::crdt::GlobalDB> &db,
                                                           const std::shared_ptr<sgns::GeniusAccount>  &account )
    {
        using sgns::ValidatorRegistry;
        if ( !db || !account )
        {
            ADD_FAILURE() << "MakeRegistry received null dependency";
            return nullptr;
        }
        auto registry = ValidatorRegistry::New(
            db,
            1,
            1,
            ValidatorRegistry::WeightConfig{},
            account->GetAddress(),
            []( const std::string &, std::function<void( outcome::result<std::string> )> cb )
            { cb( outcome::failure( std::errc::not_supported ) ); } );
        EXPECT_TRUE( registry );

        auto store_result = registry->StoreGenesisRegistry( std::vector<std::string>{ account->GetAddress() },
                                                            [account]( std::vector<uint8_t> payload )
                                                            { return account->Sign( std::move( payload ) ); } );
        EXPECT_FALSE( store_result.has_error() );

        ASSERT_WAIT_FOR_CONDITION(
            [&registry]()
            {
                auto load = registry->LoadCurrentRegistry();
                return load.has_value() && !registry->GetRegistryCid().empty();
            },
            std::chrono::milliseconds( 2000 ),
            "registry initialized",
            nullptr );

        return registry;
    }

    std::shared_ptr<sgns::ConsensusManager> MakeManager( const std::shared_ptr<sgns::ValidatorRegistry> &registry,
                                                         const std::shared_ptr<sgns::crdt::GlobalDB>    &db,
                                                         const std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> &pubs,
                                                         const std::shared_ptr<sgns::GeniusAccount> &account )
    {
        if ( !registry || !db || !pubs || !account )
        {
            ADD_FAILURE() << "MakeManager received null dependency";
            return nullptr;
        }
        auto manager = sgns::ConsensusManager::New(
            registry,
            db,
            pubs,
            [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); },
            account->GetAddress() );
        EXPECT_TRUE( manager );
        return manager;
    }

    sgns::UTXOTransitionCommitment MakeTestCommitment()
    {
        sgns::UTXOTransitionCommitment commitment;
        auto                          *consumed = commitment.add_consumed_outpoints();
        consumed->set_tx_id_hash( std::string( 32, '\x01' ) );
        consumed->set_output_index( 0 );
        auto *produced = commitment.add_produced_outputs();
        produced->set_tx_id_hash( std::string( 32, '\x02' ) );
        produced->set_output_index( 0 );
        produced->set_owner_address( "owner" );
        produced->set_token_id( std::string( 32, '\x03' ) );
        produced->set_amount( 1 );
        commitment.set_consumed_outpoints_root( std::string( 32, '\x05' ) );
        commitment.set_produced_outputs_root( std::string( 32, '\x04' ) );
        return commitment;
    }

    sgns::UTXOWitness MakeTestWitness()
    {
        sgns::UTXOWitness witness;
        return witness;
    }
}

namespace sgns::test
{
    class ConsensusCertificateTest : public ::test::CRDTFixture
    {
    public:
        ConsensusCertificateTest() : CRDTFixture( "ConsensusCertificateTest" )
        {
        }

        static void SetUpTestSuite()
        {
            CRDTFixture::SetUpTestSuite();
        }
    };

    TEST_F( ConsensusCertificateTest, CreateCertificateEmbedsProposal )
    {
        auto account = MakeAccount( getPathString() );
        ASSERT_TRUE( account );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( registry );

        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );

        std::string tx_hash        = "0x010203";
        auto        subject_result = ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                           1,
                                                                           tx_hash,
                                                                           std::string{},
                                                                           std::vector<uint8_t>{},
                                                                           MakeTestCommitment(),
                                                                           MakeTestWitness() );
        ASSERT_TRUE( subject_result.has_value() );

        auto proposal_result = manager->CreateProposal( subject_result.value(),
                                                        account->GetAddress(),
                                                        registry->GetRegistryCid(),
                                                        registry->GetRegistryEpoch() );
        ASSERT_TRUE( proposal_result.has_value() );

        auto vote_result = manager->CreateVote( proposal_result.value().proposal_id(),
                                                account->GetAddress(),
                                                true,
                                                [account]( std::vector<uint8_t> payload )
                                                { return account->Sign( std::move( payload ) ); } );
        ASSERT_TRUE( vote_result.has_value() );

        auto cert_result = manager->CreateCertificate( proposal_result.value(), { vote_result.value() } );
        ASSERT_TRUE( cert_result.has_value() );

        const auto &cert = cert_result.value();
        EXPECT_TRUE( cert.has_proposal() );
        EXPECT_EQ( cert.proposal().proposal_id(), proposal_result.value().proposal_id() );
    }

    TEST_F( ConsensusCertificateTest, HandleCertificateRejectsMismatchedProposal )
    {
        auto account = MakeAccount( getPathString() );
        ASSERT_TRUE( account );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( registry );

        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );

        std::string tx_hash        = "0x010203";
        auto        subject_result = ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                           7,
                                                                           tx_hash,
                                                                           std::string{},
                                                                           std::vector<uint8_t>{},
                                                                           MakeTestCommitment(),
                                                                           MakeTestWitness() );
        ASSERT_TRUE( subject_result.has_value() );

        auto proposal_result = manager->CreateProposal( subject_result.value(),
                                                        account->GetAddress(),
                                                        registry->GetRegistryCid(),
                                                        registry->GetRegistryEpoch(),
                                                        [account]( std::vector<uint8_t> payload )
                                                        { return account->Sign( std::move( payload ) ); } );
        ASSERT_TRUE( proposal_result.has_value() );

        auto vote_result = manager->CreateVote( proposal_result.value().proposal_id(),
                                                account->GetAddress(),
                                                true,
                                                [account]( std::vector<uint8_t> payload )
                                                { return account->Sign( std::move( payload ) ); } );
        ASSERT_TRUE( vote_result.has_value() );

        auto cert_result = manager->CreateCertificate( proposal_result.value(), { vote_result.value() } );
        ASSERT_TRUE( cert_result.has_value() );

        auto cert = cert_result.value();

        manager->RegisterSubjectHandler( NONCE_SUBJECT_TYPE,
                                         []( const ConsensusManager::Subject & )
                                         { return ConsensusManager::ValidationResult::Approve(); } );
        ConsensusManagerTestAccess::HandleProposal( manager, proposal_result.value() );
        EXPECT_TRUE( ConsensusManagerTestAccess::HasProposal( manager, proposal_result.value().proposal_id() ) );
        ConsensusManagerTestAccess::HandleCertificate( manager, cert );
        EXPECT_FALSE( ConsensusManagerTestAccess::HasProposal( manager, proposal_result.value().proposal_id() ) );

        ConsensusManagerTestAccess::HandleProposal( manager, proposal_result.value() );
        EXPECT_TRUE( ConsensusManagerTestAccess::HasProposal( manager, proposal_result.value().proposal_id() ) );
        auto bad_subject = ConsensusManager::DecodeNonceSubject( cert.proposal().subject() );
        ASSERT_TRUE( bad_subject.has_value() );
        bad_subject.value().set_nonce( bad_subject.value().nonce() + 1 );
        std::string bad_payload;
        ASSERT_TRUE( bad_subject.value().SerializeToString( &bad_payload ) );
        cert.mutable_proposal()->mutable_subject()->set_payload( bad_payload );

        ConsensusManagerTestAccess::HandleCertificate( manager, cert );
        EXPECT_TRUE( ConsensusManagerTestAccess::HasProposal( manager, proposal_result.value().proposal_id() ) );
    }

    TEST_F( ConsensusCertificateTest, NewRejectsInvalidInputs )
    {
        auto account = MakeAccount( getPathString() );
        ASSERT_TRUE( account );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( registry );

        EXPECT_EQ( ConsensusManager::New(
                       nullptr,
                       db_,
                       pubs_,
                       [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); },
                       account->GetAddress() ),
                   nullptr );
        EXPECT_EQ( ConsensusManager::New(
                       registry,
                       nullptr,
                       pubs_,
                       [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); },
                       account->GetAddress() ),
                   nullptr );
        EXPECT_EQ( ConsensusManager::New(
                       registry,
                       db_,
                       nullptr,
                       [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); },
                       account->GetAddress() ),
                   nullptr );
        EXPECT_EQ( ConsensusManager::New( registry, db_, pubs_, nullptr, account->GetAddress() ), nullptr );
        EXPECT_EQ( ConsensusManager::New(
                       registry,
                       db_,
                       pubs_,
                       [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); },
                       "" ),
                   nullptr );
    }

    TEST_F( ConsensusCertificateTest, RegisterAndUnregisterHandlers )
    {
        auto account = MakeAccount( getPathString() );
        ASSERT_TRUE( account );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( registry );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );

        EXPECT_TRUE( manager->RegisterSubjectHandler( NONCE_SUBJECT_TYPE,
                                                      []( const ConsensusManager::Subject & )
                                                      { return ConsensusManager::ValidationResult::Approve(); } ) );
        EXPECT_TRUE(
            manager->RegisterCertificateHandler( NONCE_SUBJECT_TYPE,
                                                 []( const std::string &, const ConsensusManager::Certificate & )
                                                 { return outcome::success( ConsensusManager::Check::Approve ); } ) );
        auto type_hash = ConsensusManager::ComputeSubjectTypeHash( NONCE_SUBJECT_TYPE );
        ASSERT_TRUE( type_hash.has_value() );
        EXPECT_TRUE( ConsensusManagerTestAccess::HasSubjectHandler( manager, type_hash.value() ) );
        EXPECT_TRUE( ConsensusManagerTestAccess::HasCertificateSubjectHandler( manager, type_hash.value() ) );

        manager->UnregisterSubjectHandler( NONCE_SUBJECT_TYPE );
        manager->UnregisterCertificateHandler( NONCE_SUBJECT_TYPE );
        EXPECT_FALSE( ConsensusManagerTestAccess::HasSubjectHandler( manager, type_hash.value() ) );
        EXPECT_FALSE( ConsensusManagerTestAccess::HasCertificateSubjectHandler( manager, type_hash.value() ) );
    }

    TEST_F( ConsensusCertificateTest, CreateVoteBundleAndSigningBytes )
    {
        auto account = MakeAccount( getPathString() );
        ASSERT_TRUE( account );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( registry );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );

        auto subject_result = ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                    2,
                                                                    "0x0a0b0c",
                                                                    std::string{},
                                                                    std::vector<uint8_t>{},
                                                                    MakeTestCommitment(),
                                                                    MakeTestWitness() );
        ASSERT_TRUE( subject_result.has_value() );

        auto proposal_result = manager->CreateProposal( subject_result.value(),
                                                        account->GetAddress(),
                                                        registry->GetRegistryCid(),
                                                        registry->GetRegistryEpoch() );
        ASSERT_TRUE( proposal_result.has_value() );

        auto vote_result = manager->CreateVote( proposal_result.value().proposal_id(),
                                                account->GetAddress(),
                                                true,
                                                [account]( std::vector<uint8_t> payload )
                                                { return account->Sign( std::move( payload ) ); } );
        ASSERT_TRUE( vote_result.has_value() );

        auto bundle_result = manager->CreateVoteBundle( proposal_result.value().proposal_id(),
                                                        account->GetAddress(),
                                                        { vote_result.value() },
                                                        [account]( std::vector<uint8_t> payload )
                                                        { return account->Sign( std::move( payload ) ); } );
        ASSERT_TRUE( bundle_result.has_value() );
        EXPECT_EQ( bundle_result.value().votes_size(), 1 );

        auto proposal_bytes = ConsensusManager::ProposalSigningBytes( proposal_result.value() );
        ASSERT_TRUE( proposal_bytes.has_value() );
        EXPECT_FALSE( proposal_bytes.value().empty() );

        auto vote_bytes = ConsensusManager::VoteSigningBytes( vote_result.value() );
        ASSERT_TRUE( vote_bytes.has_value() );
        EXPECT_FALSE( vote_bytes.value().empty() );

        auto bundle_bytes = ConsensusManager::VoteBundleSigningBytes( bundle_result.value() );
        ASSERT_TRUE( bundle_bytes.has_value() );
        EXPECT_FALSE( bundle_bytes.value().empty() );
    }

    TEST_F( ConsensusCertificateTest, CreateTaskResultSubjectAndComputeSubjectId )
    {
        auto account = MakeAccount( getPathString() );
        ASSERT_TRUE( account );
        auto subject_result = ConsensusManager::CreateTaskResultSubject( account->GetAddress(),
                                                                         "escrow/path",
                                                                         "0xdeadbeef",
                                                                         12 );
        ASSERT_TRUE( subject_result.has_value() );
        ASSERT_TRUE( subject_result.value().has_subject_type_hash() );
        auto type_hash = ConsensusManager::ComputeSubjectTypeHash( TASK_RESULT_SUBJECT_TYPE );
        ASSERT_TRUE( type_hash.has_value() );
        EXPECT_EQ( type_hash.value().toString(), subject_result.value().subject_type_hash().hash() );

        auto computed = ConsensusManager::ComputeSubjectId( subject_result.value() );
        ASSERT_TRUE( computed.has_value() );
        EXPECT_FALSE( computed.value().empty() );
    }

    TEST_F( ConsensusCertificateTest, TallyVotesWithRegistry )
    {
        auto account  = MakeAccount( getPathString() );
        auto account2 = MakeAccount( getPathString() + "/acc2", kTestPrivateKey2 );
        ASSERT_TRUE( account );
        ASSERT_TRUE( account2 );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( registry );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );

        auto subject_result = ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                    3,
                                                                    "0x111213",
                                                                    std::string{},
                                                                    std::vector<uint8_t>{},
                                                                    MakeTestCommitment(),
                                                                    MakeTestWitness() );
        ASSERT_TRUE( subject_result.has_value() );

        auto proposal_result = manager->CreateProposal( subject_result.value(),
                                                        account->GetAddress(),
                                                        registry->GetRegistryCid(),
                                                        registry->GetRegistryEpoch() );
        ASSERT_TRUE( proposal_result.has_value() );

        auto vote_result = manager->CreateVote( proposal_result.value().proposal_id(),
                                                account->GetAddress(),
                                                true,
                                                [account]( std::vector<uint8_t> payload )
                                                { return account->Sign( std::move( payload ) ); } );
        ASSERT_TRUE( vote_result.has_value() );

        auto vote2_result = manager->CreateVote( proposal_result.value().proposal_id(),
                                                 account2->GetAddress(),
                                                 true,
                                                 [account2]( std::vector<uint8_t> payload )
                                                 { return account2->Sign( std::move( payload ) ); } );
        ASSERT_TRUE( vote2_result.has_value() );

        auto registry_result = registry->LoadCurrentRegistry();
        ASSERT_TRUE( registry_result.has_value() );

        auto tally = manager->TallyVotes( proposal_result.value(),
                                          { vote_result.value(), vote2_result.value() },
                                          registry_result.value(),
                                          registry->GetRegistryCid() );
        ASSERT_TRUE( tally.has_value() );
        EXPECT_TRUE( tally.value().has_quorum );
        EXPECT_EQ( tally.value().total_weight, ValidatorRegistry::TotalWeight( registry_result.value() ) );
        auto *validator = ValidatorRegistry::FindValidator( registry_result.value(), account->GetAddress() );
        ASSERT_TRUE( validator );
        EXPECT_EQ( tally.value().approved_weight, validator->weight() );

        auto tally_mismatch = manager->TallyVotes( proposal_result.value(),
                                                   { vote_result.value() },
                                                   registry_result.value(),
                                                   "bad-cid" );
        EXPECT_TRUE( tally_mismatch.has_error() );
    }

    TEST_F( ConsensusCertificateTest, SubmitProposalVoteCertificateAndProcess )
    {
        auto account = MakeAccount( getPathString() );
        ASSERT_TRUE( account );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( registry );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );

        auto subject_result = ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                    4,
                                                                    "0x222324",
                                                                    std::string{},
                                                                    std::vector<uint8_t>{},
                                                                    MakeTestCommitment(),
                                                                    MakeTestWitness() );
        ASSERT_TRUE( subject_result.has_value() );

        auto proposal_result = manager->CreateProposal( subject_result.value(),
                                                        account->GetAddress(),
                                                        registry->GetRegistryCid(),
                                                        registry->GetRegistryEpoch() );
        ASSERT_TRUE( proposal_result.has_value() );

        manager->RegisterSubjectHandler( NONCE_SUBJECT_TYPE,
                                         []( const ConsensusManager::Subject & )
                                         { return ConsensusManager::ValidationResult::Approve(); } );

        auto submit_prop = manager->SubmitProposal( proposal_result.value(), false );
        EXPECT_FALSE( submit_prop.has_error() );
        EXPECT_TRUE( ConsensusManagerTestAccess::HasProposal( manager, proposal_result.value().proposal_id() ) );

        auto vote_result = manager->CreateVote( proposal_result.value().proposal_id(),
                                                account->GetAddress(),
                                                true,
                                                [account]( std::vector<uint8_t> payload )
                                                { return account->Sign( std::move( payload ) ); } );
        ASSERT_TRUE( vote_result.has_value() );

        auto submit_vote = manager->SubmitVote( vote_result.value() );
        EXPECT_FALSE( submit_vote.has_error() );

        ConsensusManagerTestAccess::HandleProposal( manager, proposal_result.value() );
        ConsensusManagerTestAccess::HandleVote( manager, vote_result.value() );
        EXPECT_TRUE(
            ConsensusManagerTestAccess::IsProposalQuorumReached( manager, proposal_result.value().proposal_id() ) );

        manager->ProcessCertificates();
        EXPECT_FALSE( ConsensusManagerTestAccess::HasProposal( manager, proposal_result.value().proposal_id() ) );
    }

    TEST_F( ConsensusCertificateTest, ResumeProposalHandlingFromPending )
    {
        auto account = MakeAccount( getPathString() );
        ASSERT_TRUE( account );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( registry );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );

        auto subject_result = ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                    5,
                                                                    "0x333435",
                                                                    std::string{},
                                                                    std::vector<uint8_t>{},
                                                                    MakeTestCommitment(),
                                                                    MakeTestWitness() );
        ASSERT_TRUE( subject_result.has_value() );

        auto proposal_result = manager->CreateProposal( subject_result.value(),
                                                        account->GetAddress(),
                                                        registry->GetRegistryCid(),
                                                        registry->GetRegistryEpoch() );
        ASSERT_TRUE( proposal_result.has_value() );

        manager->RegisterSubjectHandler( NONCE_SUBJECT_TYPE,
                                         []( const ConsensusManager::Subject & )
                                         { return ConsensusManager::ValidationResult::Pending(); } );
        ConsensusManagerTestAccess::HandleProposal( manager, proposal_result.value() );
        EXPECT_TRUE( ConsensusManagerTestAccess::HasPendingProposal( manager, proposal_result.value().proposal_id() ) );

        manager->RegisterSubjectHandler( NONCE_SUBJECT_TYPE,
                                         []( const ConsensusManager::Subject & )
                                         { return ConsensusManager::ValidationResult::Approve(); } );

        auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject_result.value() );
        ASSERT_TRUE( nonce_subject.has_value() );
        auto resume = manager->ResumeProposalHandling( nonce_subject.value().tx_hash() );
        EXPECT_FALSE( resume.has_error() );
        EXPECT_FALSE(
            ConsensusManagerTestAccess::HasPendingProposal( manager, proposal_result.value().proposal_id() ) );
        EXPECT_TRUE( ConsensusManagerTestAccess::HasProposal( manager, proposal_result.value().proposal_id() ) );
    }

    TEST_F( ConsensusCertificateTest, SubmitCertificateStoresInCrdt )
    {
        auto account = MakeAccount( getPathString() );
        ASSERT_TRUE( account );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( registry );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );

        std::string tx_hash        = "0x444546";
        auto        subject_result = ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                           6,
                                                                           tx_hash,
                                                                           std::string{},
                                                                           std::vector<uint8_t>{},
                                                                           MakeTestCommitment(),
                                                                           MakeTestWitness() );
        ASSERT_TRUE( subject_result.has_value() );

        auto proposal_result = manager->CreateProposal( subject_result.value(),
                                                        account->GetAddress(),
                                                        registry->GetRegistryCid(),
                                                        registry->GetRegistryEpoch() );
        ASSERT_TRUE( proposal_result.has_value() );

        auto vote_result = manager->CreateVote( proposal_result.value().proposal_id(),
                                                account->GetAddress(),
                                                true,
                                                [account]( std::vector<uint8_t> payload )
                                                { return account->Sign( std::move( payload ) ); } );
        ASSERT_TRUE( vote_result.has_value() );

        auto cert_result = manager->CreateCertificate( proposal_result.value(), { vote_result.value() } );
        ASSERT_TRUE( cert_result.has_value() );

        std::atomic<bool> handler_called{ false };
        manager->RegisterCertificateHandler(
            NONCE_SUBJECT_TYPE,
            [&handler_called, &tx_hash]( const std::string &subject_hash, const ConsensusManager::Certificate & )
            {
                if ( subject_hash == tx_hash )
                {
                    handler_called.store( true );
                }
                return outcome::success( ConsensusManager::Check::Approve );
            } );

        auto submit_result = manager->SubmitCertificate( cert_result.value() );
        EXPECT_FALSE( submit_result.has_error() );

        crdt::HierarchicalKey cert_key( "/cert/" + tx_hash );
        auto                  cert_get = db_->Get( cert_key );
        EXPECT_TRUE( cert_get.has_value() );

        ASSERT_WAIT_FOR_CONDITION( [&handler_called]() { return handler_called.load(); },
                                   std::chrono::milliseconds( 2000 ),
                                   "certificate handler",
                                   nullptr );
    }

    TEST_F( ConsensusCertificateTest, ValidateSubjectRejectsTamperedSubjectIdBinding )
    {
        auto account = MakeAccount( getPathString() );
        ASSERT_TRUE( account );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( registry );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );

        auto subject_result = ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                    11,
                                                                    "0xabc123",
                                                                    std::string{},
                                                                    std::vector<uint8_t>{},
                                                                    MakeTestCommitment(),
                                                                    MakeTestWitness() );
        ASSERT_TRUE( subject_result.has_value() );
        auto subject = subject_result.value();

        ASSERT_TRUE( ConsensusManagerTestAccess::ValidateSubject( subject ) );

        auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject );
        ASSERT_TRUE( nonce_subject.has_value() );
        nonce_subject.value().set_nonce( nonce_subject.value().nonce() + 1 );
        std::string bad_payload;
        ASSERT_TRUE( nonce_subject.value().SerializeToString( &bad_payload ) );
        subject.set_payload( bad_payload );
        EXPECT_FALSE( ConsensusManagerTestAccess::ValidateSubject( subject ) );
    }

    TEST_F( ConsensusCertificateTest, ValidateSubjectRejectsTamperedSubjectTypeHash )
    {
        auto account = MakeAccount( getPathString() );
        ASSERT_TRUE( account );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( registry );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );

        auto subject_result = ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                    12,
                                                                    "0xabc124",
                                                                    std::string{},
                                                                    std::vector<uint8_t>{},
                                                                    MakeTestCommitment(),
                                                                    MakeTestWitness() );
        ASSERT_TRUE( subject_result.has_value() );
        auto subject = subject_result.value();

        ASSERT_TRUE( ConsensusManagerTestAccess::ValidateSubject( subject ) );

        subject.mutable_subject_type_hash()->set_hash( std::string( 32, '\x7f' ) );
        EXPECT_FALSE( ConsensusManagerTestAccess::ValidateSubject( subject ) );
    }

    TEST_F( ConsensusCertificateTest, ValidateSubjectRejectsTamperedWitnessWithStaleSubjectId )
    {
        auto account = MakeAccount( getPathString() );
        ASSERT_TRUE( account );
        auto registry = MakeRegistry( db_, account );
        ASSERT_TRUE( registry );
        auto manager = MakeManager( registry, db_, pubs_, account );
        ASSERT_TRUE( manager );

        UTXOTransitionCommitment commitment;
        auto                    *consumed = commitment.add_consumed_outpoints();
        consumed->set_tx_id_hash( std::string( 32, '\x01' ) );
        consumed->set_output_index( 0 );
        commitment.set_consumed_outpoints_root( std::string( 32, '\x02' ) );
        commitment.set_produced_outputs_root( std::string( 32, '\x03' ) );

        UTXOWitness witness;
        auto       *proof = witness.add_consumed_inputs();
        proof->set_tx_id_hash( std::string( 32, '\x03' ) );
        proof->set_output_index( 0 );
        proof->set_leaf_payload( "leaf" );

        auto subject_result = ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                    1,
                                                                    "0xdeadbeef",
                                                                    std::string{},
                                                                    std::vector<uint8_t>{},
                                                                    commitment,
                                                                    witness );
        ASSERT_TRUE( subject_result.has_value() );
        auto subject = subject_result.value();

        ASSERT_TRUE( ConsensusManagerTestAccess::ValidateSubject( subject ) );

        auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject );
        ASSERT_TRUE( nonce_subject.has_value() );
        auto *tampered = nonce_subject.value().mutable_utxo_witness()->mutable_consumed_inputs( 0 );
        tampered->set_output_index( 9 );
        std::string bad_payload;
        ASSERT_TRUE( nonce_subject.value().SerializeToString( &bad_payload ) );
        subject.set_payload( bad_payload );
        EXPECT_FALSE( ConsensusManagerTestAccess::ValidateSubject( subject ) );
    }
} // namespace sgns::test
