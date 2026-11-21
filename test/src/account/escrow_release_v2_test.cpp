/**
 * @file escrow_release_v2_test.cpp
 * @brief Release protocol wire, codec and canonical identity regressions.
 */

#include <gtest/gtest.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/dynamic_message.h>
#include <chrono>
#include <limits>

#include "account/GeniusInputValidator.hpp"
#include "account/UTXOManager.hpp"
#include "account/UTXOMerkle.hpp"
#include "account/RegistrationTransaction.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "transaction/TransactionManager.hpp"
#include "transaction/TransactionConsensusHandler.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "account/GeniusTransaction.hpp"
#include "account/EscrowReleaseTransactionV2.hpp"
#include "account/EscrowReleaseTransaction.hpp"
#include "account/EscrowTransaction.hpp"
#include "account/TransferTransaction.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "crypto/hasher.hpp"
#include "base/hexutil.hpp"
#include "escrow_release_legacy_fixture.hpp"

namespace sgns
{
    /// @brief Exercises the existing admission seam; complete V2 manager dispatch belongs to plan 04.
    class LegacyMintAdmissionTestAccess
    {
    public:
        static ConsensusManager::ValidationResult TypeRules( TransactionManager &manager, const GeniusTransaction &tx )
        {
            return manager.consensus_m_->CheckTransactionTypeRules( tx );
        }

        static bool Authorized( TransactionManager &manager, const GeniusTransaction &tx )
        {
            return manager.CheckTransactionAuthorization( tx );
        }

        static outcome::result<void> Parse( TransactionManager &manager, const GeniusTransaction &tx )
        {
            return manager.ParseTransaction( tx );
        }

        static outcome::result<void> Revert( TransactionManager &manager, const GeniusTransaction &tx )
        {
            return manager.RevertTransaction( tx );
        }

        static std::vector<std::shared_ptr<GeniusTransaction>> Queued( TransactionManager &manager )
        {
            std::lock_guard                                 lock( manager.mutex_m );
            std::vector<std::shared_ptr<GeniusTransaction>> result;
            for ( const auto &item : manager.tx_queue_m )
            {
                for ( const auto &entry : item.first )
                {
                    result.push_back( entry.first );
                }
            }
            return result;
        }
    };
}

namespace
{
    using namespace sgns;

    using Verdict = IInputValidator::WitnessVerdict;

    class EscrowReleaseV2ValidationTest : public ::test::CRDTFixture
    {
    protected:
        EscrowReleaseV2ValidationTest() : CRDTFixture( "escrow_release_v2_validation" )
        {
        }

        void SetUp() override
        {
            previous_decoders_ = GeniusTransaction::GetDeSerializers();
            account_           = GeniusAccount::NewEphemeral( TokenID::FromBytes( { 0 } ) );
            processor_         = GeniusAccount::NewEphemeral( TokenID::FromBytes( { 0 } ) );
            ASSERT_NE( account_, nullptr );
            ASSERT_NE( processor_, nullptr );
            ASSERT_TRUE( account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() ).has_value() );
            previous_authority_ = Blockchain::GetAuthorizedFullNodeAddress();
            Blockchain::SetAuthorizedFullNodeAddress( account_->GetAddress() );
            blockchain_ = Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );
            ASSERT_NE( blockchain_, nullptr );
            manager_ = TransactionManager::New( db_,
                                                io_,
                                                account_,
                                                blockchain_,
                                                NodeType::Light,
                                                0,
                                                std::chrono::minutes( 5 ),
                                                std::chrono::minutes( 10 ) );
            ASSERT_NE( manager_, nullptr );
            const auto registry = blockchain_->GetValidatorRegistry();
            ASSERT_TRUE( registry
                             ->StoreGenesisRegistry( { account_->GetAddress() },
                                                     [account = account_]( std::vector<uint8_t> bytes )
                                                     { return account->Sign( std::move( bytes ) ); } )
                             .has_value() );
            ASSERT_TRUE( ::waitForCondition(
                [&registry]()
                { return registry->LoadCurrentRegistry().has_value() && !registry->GetRegistryCid().empty(); },
                std::chrono::seconds( 2 ),
                nullptr ) );
        }

        void TearDown() override
        {
            if ( blockchain_ )
            {
                (void) blockchain_->Stop();
            }
            manager_.reset();
            blockchain_.reset();
            account_.reset();
            processor_.reset();
            Blockchain::SetAuthorizedFullNodeAddress( previous_authority_ );
            GeniusTransaction::GetDeSerializers() = previous_decoders_;
        }

        SGTransaction::DAGStruct Dag( const GeniusAccount &signer, uint64_t nonce = 0 ) const
        {
            SGTransaction::DAGStruct dag;
            dag.set_source_addr( signer.GetAddress() );
            dag.set_nonce( nonce );
            dag.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch() )
                                   .count() );
            return dag;
        }

        std::shared_ptr<EscrowTransaction> Hold( const std::string &owner = "0x" + std::string( 64, 'a' ) )
        {
            InputUTXOInfo funding{ base::Hash256{}, 0, {} };
            funding.signature_ = account_->Sign( funding.SerializeForSigning() );
            auto hold          = std::make_shared<EscrowTransaction>(
                EscrowTransaction::New( { { funding }, { { 17, owner, TokenID::FromBytes( { 0 } ) } } },
                                        17,
                                        Dag( *account_ ) ) );
            hold->MakeSignature( *account_ );
            return hold;
        }

        EscrowReleaseTransactionV2 Release( const GeniusTransaction &producer,
                                            GeniusAccount           &signer,
                                            uint32_t                 index = 0 )
        {
            InputUTXOInfo input{ base::Hash256::fromHex( producer.GetHash() ).value(), index, {} };
            input.signature_ = signer.Sign( input.SerializeForSigning() );
            auto dag         = Dag( signer );
            dag.set_uncle_hash( "untrusted-text" );
            auto release = EscrowReleaseTransactionV2::New(
                { input },
                { { 17, account_->GetAddress(), TokenID::FromBytes( { 0 } ) } },
                std::move( dag ) );
            release.MakeSignature( signer );
            return release;
        }

        void Persist( const GeniusTransaction &transaction, const std::string &path = {} )
        {
            base::Buffer value;
            value.put( transaction.SerializeByteVector() );
            ASSERT_TRUE(
                db_->Put( { path.empty() ? TransactionManager::GetTransactionPath( transaction ) : path }, value, {} )
                    .has_value() );
        }

        outcome::result<ConsensusCertificate> Certificate(
            const GeniusTransaction                &transaction,
            std::optional<UTXOTransitionCommitment> commitment = std::nullopt )
        {
            const auto                  registry = blockchain_->GetValidatorRegistry();
            TransactionConsensusHandler handler( *manager_, base::createLogger( "ReleaseValidationTest" ) );
            BOOST_OUTCOME_TRY( auto subject,
                               ConsensusManager::CreateNonceSubject(
                                   transaction.GetSrcAddress(),
                                   transaction.GetNonce(),
                                   transaction.GetHash(),
                                   transaction.SerializeToEmbeddedTransaction(),
                                   commitment ? commitment : handler.BuildUTXOTransitionCommitment( transaction ),
                                   std::nullopt ) );
            const auto consensus = blockchain_->GetConsensusManager();
            BOOST_OUTCOME_TRY( auto proposal,
                               consensus->CreateProposal( subject,
                                                          account_->GetAddress(),
                                                          registry->GetRegistryCid(),
                                                          registry->GetRegistryEpoch() ) );
            BOOST_OUTCOME_TRY( auto vote,
                               consensus->CreateVote( proposal.proposal_id(),
                                                      account_->GetAddress(),
                                                      true,
                                                      [account = account_]( std::vector<uint8_t> bytes )
                                                      { return account->Sign( std::move( bytes ) ); } ) );
            return consensus->CreateCertificate( proposal, { vote } );
        }

        void PersistCertificate( const GeniusTransaction &transaction, const ConsensusCertificate &certificate )
        {
            base::Buffer value;
            value.put( certificate.SerializeAsString() );
            ASSERT_TRUE( db_->Put( { "/cert/" + transaction.GetSlotID() }, value, {} ).has_value() );
        }

        void Certify( const GeniusTransaction &transaction )
        {
            Persist( transaction );
            const auto certificate = Certificate( transaction );
            ASSERT_TRUE( certificate.has_value() );
            PersistCertificate( transaction, certificate.value() );
            ASSERT_TRUE( blockchain_->GetCertificateBySlot( transaction.GetSlotID() ).has_value() );
        }

        void TrackOutputs( const GeniusTransaction &producer )
        {
            const auto outputs = producer.GetProducedUTXOs();
            if ( !outputs )
            {
                FAIL() << "Producer must expose its actual outputs";
            }
            for ( const auto &output : *outputs )
            {
                ASSERT_TRUE( account_->GetUTXOManager().PutUTXO( output, output.GetOwnerAddress() ).has_value() );
            }
        }

        Verdict Local( const GeniusTransaction &transaction ) const
        {
            const auto params = transaction.GetUTXOParametersOpt();
            if ( !params )
            {
                return Verdict::kInvalid;
            }
            return validator_.ValidateTransactionUTXOParameters( transaction,
                                                                 *params,
                                                                 transaction.GetSrcAddress(),
                                                                 account_->GetUTXOManager(),
                                                                 *blockchain_ );
        }

        EscrowReleaseTransactionV2 Payment( const UTXOTxParameters &params, GeniusAccount &signer )
        {
            auto payment = EscrowReleaseTransactionV2::New( params.first, params.second, Dag( signer ) );
            payment.MakeSignature( signer );
            return payment;
        }

        std::vector<UTXOTxParameters> InvalidPayments( const UTXOTxParameters &good )
        {
            std::vector<UTXOTxParameters> cases;
            auto                          invalid = good;
            invalid.first.clear();
            cases.push_back( invalid );
            invalid = good;
            invalid.first.push_back( invalid.first.front() );
            cases.push_back( invalid );
            invalid = good;
            invalid.first.push_back( { base::Hash256{}, 0, {} } );
            cases.push_back( invalid );
            invalid                           = good;
            invalid.first.front().output_idx_ = 1;
            invalid.first.front().signature_  = processor_->Sign( invalid.first.front().SerializeForSigning() );
            cases.push_back( invalid );
            invalid                          = good;
            invalid.first.front().signature_ = account_->Sign( invalid.first.front().SerializeForSigning() );
            cases.push_back( invalid );
            invalid = good;
            invalid.second.clear();
            cases.push_back( invalid );
            invalid                                 = good;
            invalid.second.front().encrypted_amount = 18;
            cases.push_back( invalid );
            invalid                         = good;
            invalid.second.front().token_id = TokenID();
            cases.push_back( invalid );
            invalid        = good;
            invalid.second = {
                { std::numeric_limits<uint64_t>::max(), account_->GetAddress(), TokenID::FromBytes( { 0 } ) },
                { 18, account_->GetAddress(), TokenID::FromBytes( { 0 } ) } };
            cases.push_back( invalid );
            return cases;
        }

        UTXOWitness Proof( const GeniusTransaction &producer ) const
        {
            UTXOWitness witness;
            const auto  outputs = producer.GetProducedUTXOs();
            if ( !outputs || outputs->empty() )
            {
                ADD_FAILURE() << "Certified producer must have an output";
                return witness;
            }
            const auto &output = outputs->front();
            const auto  hash   = output.GetTxID();
            auto       *proof  = witness.add_consumed_inputs();
            proof->set_tx_id_hash( hash.data(), hash.size() );
            proof->set_output_index( output.GetOutputIdx() );
            const auto payload = utxo_merkle::SerializeUTXOLeafPayload( output );
            proof->set_leaf_payload( payload.data(), payload.size() );
            return witness;
        }

        ConsensusSubject Subject( const GeniusTransaction &transaction, const UTXOWitness &witness ) const
        {
            const TransactionConsensusHandler handler( *manager_, base::createLogger( "ReleaseValidationTest" ) );
            const auto                        subject = ConsensusManager::CreateNonceSubject(
                transaction.GetSrcAddress(),
                transaction.GetNonce(),
                transaction.GetHash(),
                transaction.SerializeToEmbeddedTransaction(),
                handler.BuildUTXOTransitionCommitment( transaction ),
                witness );
            if ( subject.has_error() )
            {
                ADD_FAILURE() << "Nonce subject construction failed";
                return {};
            }
            return subject.value();
        }

        TransactionConsensusHandler::WitnessValidationResult Witness( const GeniusTransaction &transaction,
                                                                      const UTXOWitness       &witness ) const
        {
            const TransactionConsensusHandler handler( *manager_, base::createLogger( "ReleaseValidationTest" ) );
            return handler.ValidateWitnessForConsensus( Subject( transaction, witness ), transaction );
        }

        TransferTransaction OwnedProducer( const std::string &owner )
        {
            // Explicitly certified historical seed: funding is not submitted through live mint admission.
            InputUTXOInfo funding{ base::Hash256{}, 1, {} };
            funding.signature_ = account_->Sign( funding.SerializeForSigning() );
            auto producer      = TransferTransaction::New( { funding },
                                                           { { 17, owner, TokenID::FromBytes( { 0 } ) } },
                                                      Dag( *account_, 7 ) );
            producer.MakeSignature( *account_ );
            return producer;
        }

        TransferTransaction Transfer( const GeniusTransaction &producer,
                                      const GeniusAccount     &source,
                                      GeniusAccount           &signer )
        {
            const auto payment = Release( producer, signer );
            auto transfer = TransferTransaction::New( payment.GetInputInfos(), payment.GetDstInfos(), Dag( source ) );
            transfer.MakeSignature( signer );
            return transfer;
        }

        GeniusInputValidator                                                         validator_;
        std::shared_ptr<GeniusAccount>                                               account_;
        std::shared_ptr<GeniusAccount>                                               processor_;
        std::shared_ptr<Blockchain>                                                  blockchain_;
        std::shared_ptr<TransactionManager>                                          manager_;
        std::string                                                                  previous_authority_;
        std::unordered_map<std::string, GeniusTransaction::TransactionDeserializeFn> previous_decoders_;
    };

    using EscrowReleaseV2PaymentTest = EscrowReleaseV2ValidationTest;

    TEST_F( EscrowReleaseV2PaymentTest, PayEscrowCreatesOnePaymentAndNoMarker )
    {
        const auto hold = Hold();
        Certify( *hold );
        TrackOutputs( *hold );
        const auto path = TransactionManager::GetTransactionPath( *hold );
        EXPECT_TRUE( manager_->PayEscrow( path, {}, nullptr ).has_error() );
        SGProcessing::TaskResult results;
        auto                    *result = results.add_subtask_results();
        result->set_subtaskid( "complete-subtask" );
        result->set_node_address( processor_->GetAddress() );
        result->set_developer_address( account_->GetAddress() );
        result->set_developer_cut( 500000 );
        const auto token = TokenID::FromBytes( { 0 } );
        result->set_token_id( token.bytes().data(), token.size() );
        const auto first = manager_->PayEscrow( path, results, nullptr );
        ASSERT_TRUE( first.has_value() );
        const auto queued = LegacyMintAdmissionTestAccess::Queued( *manager_ );
        ASSERT_EQ( queued.size(), 1U );
        const auto payment = std::dynamic_pointer_cast<EscrowReleaseTransactionV2>( queued.front() );
        ASSERT_NE( payment, nullptr );
        EXPECT_EQ( payment->GetHash(), first.value() );
        EXPECT_TRUE( payment->CheckHash() && payment->CheckSignature() );
        const auto params = payment->GetUTXOParameters();
        ASSERT_EQ( params.first.size(), 1U );
        EXPECT_TRUE( GeniusAccount::VerifySignature( account_->GetAddress(),
                                                     params.first.front().signature_,
                                                     params.first.front().SerializeForSigning() ) );
        EXPECT_EQ( params.first.front().txid_hash_.toReadableString(), hold->GetHash() );
        EXPECT_EQ( payment->GetTopics().count( processor_->GetAddress() ), 1U );
        EXPECT_EQ( payment->GetTopics().count( account_->GetAddress() ), 1U );
        auto       other  = TransactionManager::New( db_, io_, processor_, blockchain_, NodeType::Light );
        const auto second = other->PayEscrow( path, results, nullptr );
        ASSERT_TRUE( second.has_value() );
        const auto other_queued = LegacyMintAdmissionTestAccess::Queued( *other );
        ASSERT_EQ( other_queued.size(), 1U );
        const auto other_payment = std::dynamic_pointer_cast<EscrowReleaseTransactionV2>( other_queued.front() );
        ASSERT_NE( other_payment, nullptr );
        EXPECT_NE( other_payment->GetSrcAddress(), payment->GetSrcAddress() );
        EXPECT_EQ( other_payment->GetSlotID(), payment->GetSlotID() );
        EXPECT_TRUE( other_payment->CheckSignature() );
        other->Stop();
    }

    TEST_F( EscrowReleaseV2ValidationTest, ManagerDispatchReconstructsPaymentType )
    {
        const auto hold    = Hold();
        auto       release = Release( *hold, *processor_ );
        const auto raw     = release.SerializeByteVector();
        const auto decoded = TransactionManager::DeSerializeTransaction( { raw.begin(), raw.end() } );
        ASSERT_TRUE( decoded.has_value() );
        ASSERT_NE( std::dynamic_pointer_cast<EscrowReleaseTransactionV2>( decoded.value() ), nullptr );
        EXPECT_TRUE( decoded.value()->CheckHash() );
        EXPECT_TRUE( decoded.value()->CheckSignature() );
        EXPECT_EQ( decoded.value()->GetSlotID(), release.GetSlotID() );
        const auto embedded = TransactionManager::DeSerializeEmbeddedTransaction(
            release.SerializeToEmbeddedTransaction() );
        ASSERT_TRUE( embedded.has_value() );
        ASSERT_NE( std::dynamic_pointer_cast<EscrowReleaseTransactionV2>( embedded.value() ), nullptr );
        EXPECT_EQ( embedded.value()->SerializeByteVector(), raw );
    }

    TEST_F( EscrowReleaseV2ValidationTest, ManagerDispatchPreservesMarkerWithoutFundsEffects )
    {
        const auto hold = Hold();
        TrackOutputs( *hold );
        auto      &ledger = account_->GetUTXOManager();
        const auto before = ledger.GetBalance( hold->GetUTXOParameters().second.front().dest_address );
        const auto raw    = base::unhex( escrow_release_legacy_fixture::FIXTURES.front().raw_hex );
        ASSERT_TRUE( raw.has_value() );
        const std::string bytes( raw.value().begin(), raw.value().end() );
        const auto        decoded = TransactionManager::DeSerializeTransaction( bytes );
        ASSERT_TRUE( decoded.has_value() );
        const auto marker = std::dynamic_pointer_cast<EscrowReleaseTransaction>( decoded.value() );
        ASSERT_NE( marker, nullptr );
        const auto embedded = TransactionManager::DeSerializeEmbeddedTransaction(
            marker->SerializeToEmbeddedTransaction() );
        ASSERT_TRUE( embedded.has_value() );
        ASSERT_NE( std::dynamic_pointer_cast<EscrowReleaseTransaction>( embedded.value() ), nullptr );
        EXPECT_EQ( embedded.value()->SerializeByteVector(), decoded.value()->SerializeByteVector() );
        EXPECT_EQ( marker->GetTopics().count( marker->GetEscrowSource() ), 1U );
        ASSERT_TRUE( LegacyMintAdmissionTestAccess::Parse( *manager_, *marker ).has_value() );
        ASSERT_TRUE( LegacyMintAdmissionTestAccess::Revert( *manager_, *marker ).has_value() );
        EXPECT_EQ( ledger.GetBalance( hold->GetUTXOParameters().second.front().dest_address ), before );
        EXPECT_EQ( ledger.GetBalance( marker->GetReleaseAddress() ), 0U );
        EXPECT_FALSE( ledger.IsOutPointConsumed( base::Hash256::fromHex( hold->GetHash() ).value(), 0 ) );
        EXPECT_FALSE( ledger.GetOutPointState( base::Hash256::fromHex( marker->GetHash() ).value(), 0 ).has_value() );
        EXPECT_EQ( LegacyMintAdmissionTestAccess::TypeRules( *manager_, *marker ).check,
                   ConsensusManager::Check::Reject );
    }

    TEST_F( EscrowReleaseV2ValidationTest, V2FullIngressUsesCertifiedHoldEvidence )
    {
        const auto                  hold    = Hold();
        auto                        release = Release( *hold, *processor_ );
        TransactionConsensusHandler handler( *manager_, base::createLogger( "ReleaseIngressTest" ) );
        auto                        result = handler.HandleNonceConsensusSubject( Subject( release, Proof( *hold ) ) );
        ASSERT_TRUE( result.has_value() );
        EXPECT_EQ( result.value().check, ConsensusManager::Check::Pending );
        Persist( *hold );
        result = handler.HandleNonceConsensusSubject( Subject( release, Proof( *hold ) ) );
        ASSERT_TRUE( result.has_value() );
        EXPECT_EQ( result.value().check, ConsensusManager::Check::Pending );
        Certify( *hold );
        TrackOutputs( *hold );
        result = handler.HandleNonceConsensusSubject( Subject( release, Proof( *hold ) ) );
        ASSERT_TRUE( result.has_value() );
        EXPECT_EQ( result.value().check, ConsensusManager::Check::Approve );
        auto invalid                            = release.GetUTXOParameters();
        invalid.second.front().encrypted_amount = 18;
        auto bad                                = Payment( invalid, *processor_ );
        result                                  = handler.HandleNonceConsensusSubject( Subject( bad, Proof( *hold ) ) );
        ASSERT_TRUE( result.has_value() );
        EXPECT_EQ( result.value().check, ConsensusManager::Check::Reject );
    }

    TEST_F( EscrowReleaseV2ValidationTest, V2ResolverBindsCertifiedHold )
    {
        const auto hold = Hold();
        Certify( *hold );
        const auto release  = Release( *hold, *processor_ );
        const auto resolved = validator_.ResolveEscrowReleaseInput( release, *blockchain_ );
        ASSERT_EQ( resolved.verdict, Verdict::kValid );
        ASSERT_NE( resolved.producer, nullptr );
        EXPECT_EQ( resolved.producer->GetHash(), hold->GetHash() );
        EXPECT_EQ( resolved.producer->GetUTXOParameters().second.front().dest_address,
                   hold->GetUTXOParameters().second.front().dest_address );
    }

    TEST_F( EscrowReleaseV2ValidationTest, V2ResolverRejectsFalseHold )
    {
        const auto normal = TransferTransaction::New(
            {},
            { { 17, "0x" + std::string( 64, 'a' ), TokenID::FromBytes( { 0 } ) } },
            Dag( *account_ ) );
        Persist( normal );
        EXPECT_EQ( validator_.ResolveEscrowReleaseInput( Release( normal, *processor_ ), *blockchain_ ).verdict,
                   Verdict::kInvalid );
        const auto wrong_owner = Hold( account_->GetAddress() );
        Persist( *wrong_owner );
        EXPECT_EQ( validator_.ResolveEscrowReleaseInput( Release( *wrong_owner, *processor_ ), *blockchain_ ).verdict,
                   Verdict::kInvalid );
        const auto hold = Hold();
        Certify( *hold );
        EXPECT_EQ( validator_.ResolveEscrowReleaseInput( Release( *hold, *processor_, 1 ), *blockchain_ ).verdict,
                   Verdict::kInvalid );
        const auto   release = Release( *hold, *processor_ );
        base::Buffer malformed;
        malformed.put( std::vector<uint8_t>{ 0xff } );
        ASSERT_TRUE( db_->Put( { TransactionManager::GetTransactionPath( *hold ) }, malformed, {} ).has_value() );
        EXPECT_EQ( validator_.ResolveEscrowReleaseInput( release, *blockchain_ ).verdict, Verdict::kInvalid );
        auto other = Hold( "0x" + std::string( 64, 'b' ) );
        Persist( *other, TransactionManager::GetTransactionPath( *hold ) );
        EXPECT_EQ( validator_.ResolveEscrowReleaseInput( release, *blockchain_ ).verdict, Verdict::kInvalid );
        Persist( *hold );
        const auto wrong_certificate = Certificate( *other );
        ASSERT_TRUE( wrong_certificate.has_value() );
        PersistCertificate( *hold, wrong_certificate.value() );
        EXPECT_EQ( validator_.ResolveEscrowReleaseInput( release, *blockchain_ ).verdict, Verdict::kInvalid );
        ASSERT_TRUE( db_->Put( { "/cert/" + hold->GetSlotID() }, malformed, {} ).has_value() );
        EXPECT_EQ( validator_.ResolveEscrowReleaseInput( release, *blockchain_ ).verdict, Verdict::kInvalid );
    }

    TEST_F( EscrowReleaseV2ValidationTest, V2ResolverMissingEvidenceIsPending )
    {
        const auto hold    = Hold();
        const auto release = Release( *hold, *processor_ );
        EXPECT_EQ( validator_.ResolveEscrowReleaseInput( release, *blockchain_ ).verdict, Verdict::kNotSynced );
        Persist( *hold );
        EXPECT_EQ( validator_.ResolveEscrowReleaseInput( release, *blockchain_ ).verdict, Verdict::kNotSynced );
    }

    TEST_F( EscrowReleaseV2ValidationTest, V2LocalAcceptsAnyValidProcessor )
    {
        const auto hold = Hold();
        Certify( *hold );
        TrackOutputs( *hold );
        for ( const auto &signer : { account_, processor_ } )
        {
            const auto release = Release( *hold, *signer );
            EXPECT_EQ( Local( release ), Verdict::kValid );
            EXPECT_EQ( validator_.ValidateTransactionUTXOParameters( release,
                                                                     release.GetUTXOParameters(),
                                                                     "different-source",
                                                                     account_->GetUTXOManager(),
                                                                     *blockchain_ ),
                       Verdict::kInvalid );
        }
        const auto release = Release( *hold, *processor_ );
        auto       params  = release.GetUTXOParameters();
        params.second      = { { 6, account_->GetAddress(), TokenID::FromBytes( { 0 } ) },
                               { 11, processor_->GetAddress(), TokenID::FromBytes( { 0x7f } ) } };
        EXPECT_EQ( Local( Payment( params, *processor_ ) ), Verdict::kValid );
    }

    TEST_F( EscrowReleaseV2ValidationTest, V2LocalRejectsInvalidPaymentInputs )
    {
        const auto hold = Hold();
        Certify( *hold );
        TrackOutputs( *hold );
        const auto release = Release( *hold, *processor_ );
        const auto good    = release.GetUTXOParameters();
        EXPECT_EQ( Local( release ), Verdict::kValid );
        for ( const auto &invalid : InvalidPayments( good ) )
        {
            EXPECT_EQ( Local( Payment( invalid, *processor_ ) ), Verdict::kInvalid );
        }
        auto bad_account_signature = release;
        bad_account_signature.dag_st.set_signature( "invalid" );
        EXPECT_EQ( Local( bad_account_signature ), Verdict::kInvalid );
        ASSERT_TRUE( account_->GetUTXOManager()
                         .ConsumeUTXOs( good.first, hold->GetUTXOParameters().second.front().dest_address )
                         .has_value() );
        EXPECT_EQ( Local( release ), Verdict::kInvalid );
        for ( uint8_t scenario = 0; scenario < 3; ++scenario )
        {
            const auto actual = Hold( "0x" + std::string( 64, static_cast<char>( 'b' + scenario ) ) );
            Certify( *actual );
            const auto hash   = base::Hash256::fromHex( actual->GetHash() ).value();
            const auto owner  = scenario == 0 ? processor_->GetAddress()
                                              : actual->GetUTXOParameters().second.front().dest_address;
            const auto amount = scenario == 1 ? 18U : 17U;
            const auto token  = TokenID::FromBytes( { static_cast<uint8_t>( scenario == 2 ? 1 : 0 ) } );
            ASSERT_TRUE(
                account_->GetUTXOManager().PutUTXO( GeniusUTXO( hash, 0, amount, token, owner ), owner ).has_value() );
            EXPECT_EQ( Local( Release( *actual, *processor_ ) ), Verdict::kInvalid ) << static_cast<int>( scenario );
        }
    }

    TEST_F( EscrowReleaseV2ValidationTest, FreshTransferEscrowBypassRejectedLocally )
    {
        const auto hold = Hold();
        Certify( *hold );
        TrackOutputs( *hold );
        const auto release = Release( *hold, *processor_ );
        const auto params  = release.GetUTXOParameters();
        for ( const auto &uncle :
              { hold->GetUTXOParameters().second.front().dest_address, "0x" + std::string( 64, 'f' ) } )
        {
            for ( const bool backdated : { false, true } )
            {
                auto dag = Dag( *processor_ );
                dag.set_uncle_hash( uncle );
                if ( backdated )
                {
                    dag.set_timestamp( 1 );
                }
                auto transfer = TransferTransaction::New( params.first, params.second, std::move( dag ) );
                transfer.MakeSignature( *processor_ );
                EXPECT_EQ( Local( transfer ), Verdict::kInvalid );
            }
        }
        auto owned = TransferTransaction::New( {},
                                               { { 17, processor_->GetAddress(), TokenID::FromBytes( { 0 } ) } },
                                               Dag( *account_, 7 ) );
        owned.MakeSignature( *account_ );
        TrackOutputs( owned );
        const auto spend = Release( owned, *processor_ );
        auto ordinary    = TransferTransaction::New( spend.GetInputInfos(), spend.GetDstInfos(), Dag( *processor_ ) );
        ordinary.MakeSignature( *processor_ );
        EXPECT_EQ( Local( ordinary ), Verdict::kValid );
    }

    TEST_F( EscrowReleaseV2ValidationTest, V2WitnessAcceptsCertifiedHold )
    {
        using Result    = TransactionConsensusHandler::WitnessValidationResult;
        const auto hold = Hold();
        Certify( *hold );
        TrackOutputs( *hold );
        for ( const auto &signer : { account_, processor_ } )
        {
            const auto release = Release( *hold, *signer );
            EXPECT_EQ( Witness( release, Proof( *hold ) ), Result::VALID );
            EXPECT_EQ( LegacyMintAdmissionTestAccess::TypeRules( *manager_, release ).check,
                       ConsensusManager::Check::Approve );
        }
    }

    TEST_F( EscrowReleaseV2ValidationTest, V2WitnessRejectsInvalidPaymentInputs )
    {
        using Result    = TransactionConsensusHandler::WitnessValidationResult;
        const auto hold = Hold();
        Certify( *hold );
        TrackOutputs( *hold );
        const auto release = Release( *hold, *processor_ );
        const auto good    = release.GetUTXOParameters();
        const auto proof   = Proof( *hold );
        EXPECT_EQ( Witness( release, proof ), Result::VALID );
        for ( const auto &invalid : InvalidPayments( good ) )
        {
            EXPECT_EQ( Witness( Payment( invalid, *processor_ ), proof ), Result::INVALID );
        }
        auto bad_signature = release;
        bad_signature.dag_st.set_signature( "invalid" );
        EXPECT_EQ( Witness( bad_signature, proof ), Result::INVALID );
        auto tampered = proof;
        tampered.mutable_consumed_inputs( 0 )->add_produced_branch()->set_sibling_hash( std::string( 32, '\0' ) );
        EXPECT_EQ( Witness( release, tampered ), Result::INVALID );
        const auto hash    = base::Hash256::fromHex( hold->GetHash() ).value();
        const auto payload = utxo_merkle::SerializeUTXOLeafPayload(
            GeniusUTXO( hash, 0, 17, TokenID::FromBytes( { 0 } ), processor_->GetAddress() ) );
        tampered = proof;
        tampered.mutable_consumed_inputs( 0 )->set_leaf_payload( payload.data(), payload.size() );
        EXPECT_EQ( Witness( release, tampered ), Result::INVALID );

        const TransactionConsensusHandler handler( *manager_, base::createLogger( "ReleaseValidationTest" ) );
        auto                              bad_commitment = handler.BuildUTXOTransitionCommitment( *hold );
        if ( !bad_commitment )
        {
            FAIL() << "Hold must have a real transition commitment";
        }
        bad_commitment->set_produced_outputs_root( std::string( 32, '\0' ) );
        const auto certificate = Certificate( *hold, bad_commitment );
        ASSERT_TRUE( certificate.has_value() );
        PersistCertificate( *hold, certificate.value() );
        ASSERT_TRUE( blockchain_->GetCertificateBySlot( hold->GetSlotID() ).has_value() );
        EXPECT_EQ( Witness( release, proof ), Result::INVALID );
        Certify( *hold );

        auto bad_subject = Subject( release, proof );
        auto nonce       = ConsensusManager::DecodeNonceSubject( bad_subject );
        ASSERT_TRUE( nonce.has_value() );
        nonce.value().mutable_utxo_commitment()->set_consumed_outpoints_root( std::string( 32, '\0' ) );
        const auto forged = ConsensusManager::CreateNonceSubject( release.GetSrcAddress(),
                                                                  release.GetNonce(),
                                                                  release.GetHash(),
                                                                  release.SerializeToEmbeddedTransaction(),
                                                                  nonce.value().utxo_commitment(),
                                                                  proof );
        ASSERT_TRUE( forged.has_value() );
        EXPECT_EQ( handler.ValidateWitnessForConsensus( forged.value(), release ), Result::INVALID );
        const auto other             = Hold( "0x" + std::string( 64, 'f' ) );
        const auto wrong_certificate = Certificate( *other );
        ASSERT_TRUE( wrong_certificate.has_value() );
        PersistCertificate( *hold, wrong_certificate.value() );
        EXPECT_EQ( Witness( release, proof ), Result::INVALID );
        Certify( *hold );
        ASSERT_TRUE( account_->GetUTXOManager()
                         .ConsumeUTXOs( good.first, hold->GetUTXOParameters().second.front().dest_address )
                         .has_value() );
        EXPECT_EQ( LegacyMintAdmissionTestAccess::TypeRules( *manager_, release ).check,
                   ConsensusManager::Check::Reject );
    }

    TEST_F( EscrowReleaseV2ValidationTest, FreshTransferEscrowBypassRejectedByWitness )
    {
        using Result    = TransactionConsensusHandler::WitnessValidationResult;
        const auto hold = Hold();
        Certify( *hold );
        TrackOutputs( *hold );
        for ( const auto &uncle :
              { hold->GetUTXOParameters().second.front().dest_address, "0x" + std::string( 64, 'f' ) } )
        {
            for ( const bool backdated : { false, true } )
            {
                auto transfer = Transfer( *hold, *processor_, *processor_ );
                transfer.dag_st.set_uncle_hash( uncle );
                if ( backdated )
                {
                    transfer.dag_st.set_timestamp( 1 );
                }
                transfer.FillHash();
                transfer.MakeSignature( *processor_ );
                EXPECT_EQ( Witness( transfer, Proof( *hold ) ), Result::INVALID );
            }
        }
    }

    TEST_F( EscrowReleaseV2ValidationTest, V2MissingEvidencePendsAdmission )
    {
        using Result    = TransactionConsensusHandler::WitnessValidationResult;
        const auto hold = Hold();
        TrackOutputs( *hold );
        const auto release = Release( *hold, *processor_ );
        const auto proof   = Proof( *hold );
        for ( const bool persist_producer : { false, true } )
        {
            if ( persist_producer )
            {
                Persist( *hold );
            }
            EXPECT_EQ( Witness( release, proof ), Result::PENDING );
            const auto admission = LegacyMintAdmissionTestAccess::TypeRules( *manager_, release );
            EXPECT_EQ( admission.check, ConsensusManager::Check::Pending );
            ASSERT_EQ( admission.dependencies.size(), 1 );
            EXPECT_EQ( admission.dependencies.front(),
                       ConsensusManager::PendingDependencyKey::Certificate( hold->GetHash() ) );
        }
        Certify( *hold );
        EXPECT_EQ( Witness( release, proof ), Result::VALID );
        auto rejected = Certificate( *hold );
        ASSERT_TRUE( rejected.has_value() );
        rejected.value().mutable_votes( 0 )->set_signature( "invalid" );
        PersistCertificate( *hold, rejected.value() );
        EXPECT_EQ( validator_.ResolveEscrowReleaseInput( release, *blockchain_ ).verdict, Verdict::kNotSynced );
        EXPECT_EQ( LegacyMintAdmissionTestAccess::TypeRules( *manager_, release ).check,
                   ConsensusManager::Check::Pending );

        // Exercise full existing manager ingress with ordinary Transfer while V2 dispatch is still plan 04.
        const auto owned = OwnedProducer( processor_->GetAddress() );
        TrackOutputs( owned );
        Persist( owned );
        const auto                  transfer = Transfer( owned, *processor_, *processor_ );
        TransactionConsensusHandler handler( *manager_, base::createLogger( "ReleaseValidationTest" ) );
        const auto admission = handler.HandleNonceConsensusSubject( Subject( transfer, Proof( owned ) ) );
        ASSERT_TRUE( admission.has_value() );
        EXPECT_EQ( admission.value().check, ConsensusManager::Check::Pending );
    }

    TEST_F( EscrowReleaseV2ValidationTest, OrdinaryAndCertifiedParentTransfersRemainValid )
    {
        using Result     = TransactionConsensusHandler::WitnessValidationResult;
        const auto owned = OwnedProducer( processor_->GetAddress() );
        Certify( owned );
        TrackOutputs( owned );
        const auto ordinary = Transfer( owned, *processor_, *processor_ );
        EXPECT_EQ( Witness( ordinary, Proof( owned ) ), Result::VALID );
        TransactionConsensusHandler handler( *manager_, base::createLogger( "ReleaseValidationTest" ) );
        const auto admission = handler.HandleNonceConsensusSubject( Subject( ordinary, Proof( owned ) ) );
        ASSERT_TRUE( admission.has_value() );
        EXPECT_EQ( admission.value().check, ConsensusManager::Check::Approve );

        auto registration = RegistrationTransaction::New( account_->GetAddress(), 1, {}, Dag( *processor_ ) );
        registration.MakeSignature( *processor_ );
        Certify( registration );
        Persist( registration, TransactionManager::GetBlockChainBase() + "reg/" + processor_->GetAddress() );
        ASSERT_EQ( blockchain_->CheckCertifiedParent( processor_->GetAddress() ), account_->GetAddress() );
        const auto delegated = Transfer( owned, *processor_, *account_ );
        EXPECT_FALSE( delegated.CheckSignature() );
        EXPECT_TRUE( LegacyMintAdmissionTestAccess::Authorized( *manager_, delegated ) );
        EXPECT_EQ( Witness( delegated, Proof( owned ) ), Result::VALID );
    }

    template <typename Message>
    std::vector<uint8_t> MessageBytes( const Message &message )
    {
        const auto bytes = message.SerializeAsString();
        return { bytes.begin(), bytes.end() };
    }

    std::shared_ptr<GeniusTransaction> DecodeRelease( const std::string &type, const std::vector<uint8_t> &bytes )
    {
        const auto &decoders = GeniusTransaction::GetDeSerializers();
        const auto  decoder  = decoders.find( type );
        return decoder == decoders.end() ? nullptr : decoder->second( bytes );
    }

    std::shared_ptr<GeniusTransaction> DecodeReleaseV2( const std::vector<uint8_t> &bytes )
    {
        return DecodeRelease( "escrow-release-v2", bytes );
    }

    SGTransaction::EscrowReleaseTxV2 SignedPaymentWire( GeniusAccount     &account,
                                                        uint64_t           nonce,
                                                        const std::string &hold_hash = std::string( 64, 'a' ),
                                                        uint64_t           amount    = 17 )
    {
        SGTransaction::EscrowReleaseTxV2 wire;
        auto                            *dag = wire.mutable_dag_struct();
        dag->set_type( "escrow-release-v2" );
        dag->set_source_addr( account.GetAddress() );
        dag->set_nonce( nonce );
        dag->set_timestamp( 123456 );
        dag->set_previous_hash( "previous" );
        dag->set_uncle_hash( "untrusted-uncle-text" );
        auto *input = wire.mutable_utxo_params()->add_inputs();
        input->set_tx_id_hash( hold_hash );
        input->set_output_index( 0 );
        const auto input_signature = account.Sign( { 1, 2, 3 } );
        input->set_signature( input_signature.data(), input_signature.size() );
        auto *output = wire.mutable_utxo_params()->add_outputs();
        output->set_encrypted_amount( amount );
        output->set_dest_addr( "recipient" );
        output->set_token_id( std::string( TokenID::ByteArray{}.size(), '\x42' ) );
        dag->set_data_hash( crypto::blake2b_256( MessageBytes( wire ) ).toReadableString() );
        const auto signature = account.Sign( MessageBytes( wire ) );
        dag->set_signature( signature.data(), signature.size() );
        return wire;
    }

    class EscrowReleaseV2CodecTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            first_  = GeniusAccount::NewEphemeral( TokenID::FromBytes( { 0 } ) );
            second_ = GeniusAccount::NewEphemeral( TokenID::FromBytes( { 0 } ) );
            ASSERT_NE( first_, nullptr );
            ASSERT_NE( second_, nullptr );
            ASSERT_NE( first_->GetAddress(), second_->GetAddress() );
        }

        std::shared_ptr<GeniusAccount> first_;
        std::shared_ptr<GeniusAccount> second_;
    };

    TEST_F( EscrowReleaseV2CodecTest, RawAndEmbeddedRoundTripPreserveSignedPayment )
    {
        for ( const auto &account : { first_, second_ } )
        {
            const auto wire    = SignedPaymentWire( *account, 9 );
            const auto bytes   = MessageBytes( wire );
            const auto payment = DecodeReleaseV2( bytes );
            ASSERT_NE( payment, nullptr );
            EXPECT_EQ( payment->SerializeByteVector(), bytes );
            EXPECT_EQ( payment->GetType(), "escrow-release-v2" );
            EXPECT_EQ( payment->GetTransactionSpecificPath(), "escrow-release-v2" );
            EXPECT_EQ( payment->GetSrcAddress(), account->GetAddress() );
            EXPECT_EQ( payment->GetHash(), wire.dag_struct().data_hash() );
            EXPECT_EQ( payment->dag_st.SerializeAsString(), wire.dag_struct().SerializeAsString() );
            EXPECT_TRUE( payment->CheckHash() );
            EXPECT_TRUE( payment->CheckSignature() );
            const auto typed = std::dynamic_pointer_cast<EscrowReleaseTransactionV2>( payment );
            ASSERT_NE( typed, nullptr );
            EXPECT_TRUE( payment->HasUTXOParameters() );
            const auto params = payment->GetUTXOParametersOpt();
            if ( !params )
            {
                FAIL() << "V2 must expose its payment parameters";
            }
            ASSERT_EQ( params->first.size(), 1 );
            EXPECT_EQ( params->first.front().txid_hash_.toReadableString(), std::string( 64, 'a' ) );
            EXPECT_EQ( params->first.front().output_idx_, 0 );
            EXPECT_EQ( params->first.front().signature_, account->Sign( { 1, 2, 3 } ) );
            ASSERT_EQ( params->second.size(), 1 );
            EXPECT_EQ( params->second.front().encrypted_amount, 17 );
            EXPECT_EQ( params->second.front().dest_address, "recipient" );
            const auto &token_bytes = wire.utxo_params().outputs( 0 ).token_id();
            EXPECT_EQ( params->second.front().token_id, TokenID::FromBytes( token_bytes.data(), token_bytes.size() ) );
            auto created = EscrowReleaseTransactionV2::New( typed->GetInputInfos(),
                                                            typed->GetDstInfos(),
                                                            payment->dag_st );
            created.MakeSignature( *account );
            EXPECT_EQ( created.SerializeByteVector(), bytes );
            EXPECT_EQ( MessageBytes( created.SerializeToEmbeddedTransaction().escrow_release_v2() ), bytes );
            EXPECT_EQ( typed->GetUTXOParameters().first.size(), 1 );
            EXPECT_EQ( payment->GetTopics(),
                       ( std::unordered_set<std::string>{ account->GetAddress(), "recipient" } ) );
            const auto embedded = payment->SerializeToEmbeddedTransaction();
            ASSERT_EQ( embedded.transaction_case(), EmbeddedTransaction::kEscrowReleaseV2 );
            EXPECT_EQ( MessageBytes( embedded.escrow_release_v2() ), bytes );
            EmbeddedTransaction restored_envelope;
            ASSERT_TRUE( restored_envelope.ParseFromString( embedded.SerializeAsString() ) );
            const auto restored = DecodeReleaseV2( MessageBytes( restored_envelope.escrow_release_v2() ) );
            ASSERT_NE( restored, nullptr );
            EXPECT_EQ( restored->SerializeByteVector(), bytes );
            EXPECT_EQ( restored->GetSlotID(), payment->GetSlotID() );
            EXPECT_TRUE( restored->CheckSignature() );
        }
    }

    TEST_F( EscrowReleaseV2CodecTest, SameHoldAcrossPeersHasOneSlot )
    {
        const auto first_wire  = SignedPaymentWire( *first_, 1 );
        const auto second_wire = SignedPaymentWire( *second_, 99, std::string( 64, 'a' ), 43 );
        EXPECT_NE( first_wire.dag_struct().data_hash(), second_wire.dag_struct().data_hash() );
        EXPECT_NE( first_wire.dag_struct().signature(), second_wire.dag_struct().signature() );
        const auto first  = DecodeReleaseV2( MessageBytes( first_wire ) );
        const auto second = DecodeReleaseV2( MessageBytes( second_wire ) );
        ASSERT_NE( first, nullptr );
        ASSERT_NE( second, nullptr );
        EXPECT_EQ( first->GetSlotID(), "escrow-release-v2:" + std::string( 64, 'a' ) + ":0" );
        EXPECT_EQ( first->GetSlotID(), second->GetSlotID() );
        EXPECT_TRUE( first->CheckSignature() );
        EXPECT_TRUE( second->CheckSignature() );
    }

    TEST_F( EscrowReleaseV2CodecTest, DifferentHoldHasDifferentSlot )
    {
        const auto first  = DecodeReleaseV2( MessageBytes( SignedPaymentWire( *first_, 1 ) ) );
        const auto second = DecodeReleaseV2(
            MessageBytes( SignedPaymentWire( *second_, 99, std::string( 64, 'b' ) ) ) );
        ASSERT_NE( first, nullptr );
        ASSERT_NE( second, nullptr );
        EXPECT_NE( first->GetSlotID(), second->GetSlotID() );
    }

    TEST_F( EscrowReleaseV2CodecTest, RejectsMalformedPaymentEncoding )
    {
        const auto good = SignedPaymentWire( *first_, 1 );
        ASSERT_NE( DecodeReleaseV2( MessageBytes( good ) ), nullptr );
        EXPECT_EQ( DecodeReleaseV2( { 0xff } ), nullptr );
        auto invalid = good;
        invalid.mutable_dag_struct()->set_type( "transfer" );
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        invalid.clear_dag_struct();
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        invalid = good;
        invalid.clear_utxo_params();
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        invalid = good;
        invalid.mutable_utxo_params()->clear_inputs();
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        invalid                                      = good;
        *invalid.mutable_utxo_params()->add_inputs() = good.utxo_params().inputs( 0 );
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        invalid = good;
        invalid.mutable_utxo_params()->mutable_inputs( 0 )->set_output_index( 1 );
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        invalid = good;
        invalid.mutable_utxo_params()->mutable_inputs( 0 )->set_tx_id_hash( "invalid-hash" );
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        for ( const auto &bad_hash : { std::string( 64, 'z' ),
                                       std::string( 65, 'a' ),
                                       "1g" + std::string( 62, 'a' ),
                                       std::string( 64, 'A' ) } )
        {
            invalid = good;
            invalid.mutable_utxo_params()->mutable_inputs( 0 )->set_tx_id_hash( bad_hash );
            EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        }
        for ( const auto size : { 0, 1, 31, 33 } )
        {
            invalid = good;
            invalid.mutable_utxo_params()->mutable_outputs( 0 )->set_token_id( std::string( size, '\0' ) );
            EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr ) << size;
        }
        invalid = good;
        invalid.mutable_utxo_params()->clear_outputs();
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        invalid = good;
        invalid.mutable_utxo_params()->mutable_outputs( 0 )->clear_dest_addr();
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );
        invalid = good;
        invalid.GetReflection()->MutableUnknownFields( &invalid )->AddVarint( 99, 1 );
        EXPECT_EQ( DecodeReleaseV2( MessageBytes( invalid ) ), nullptr );

        const auto typed = EscrowReleaseTransactionV2::DeSerializeByteVector( MessageBytes( good ) );
        ASSERT_NE( typed, nullptr );
        auto inputs = typed->GetInputInfos();
        EXPECT_TRUE( EscrowReleaseTransactionV2::New( {}, typed->GetDstInfos(), typed->dag_st ).GetSlotID().empty() );
        inputs.push_back( inputs.front() );
        EXPECT_TRUE(
            EscrowReleaseTransactionV2::New( inputs, typed->GetDstInfos(), typed->dag_st ).GetSlotID().empty() );
        inputs.pop_back();
        inputs.front().output_idx_ = 1;
        EXPECT_TRUE(
            EscrowReleaseTransactionV2::New( inputs, typed->GetDstInfos(), typed->dag_st ).GetSlotID().empty() );
    }

    TEST( EscrowReleaseWireContractTest, AdditiveTagTenPreservesLegacyTagSeven )
    {
        const auto                    *legacy        = SGTransaction::EscrowReleaseTx::descriptor();
        const std::vector<std::string> legacy_fields = { "dag_struct",
                                                         "utxo_params",
                                                         "release_amount",
                                                         "release_address",
                                                         "escrow_source",
                                                         "original_escrow_hash" };
        ASSERT_EQ( legacy->field_count(), legacy_fields.size() );
        int field_number = 0;
        for ( const auto &name : legacy_fields )
        {
            const auto *field = legacy->FindFieldByName( name );
            ASSERT_NE( field, nullptr );
            EXPECT_EQ( field->number(), ++field_number );
        }

        const auto                    *embedded  = sgns::EmbeddedTransaction::descriptor();
        const std::vector<std::string> old_cases = { "transfer",
                                                     "mint_v2",
                                                     "mint",
                                                     "processing",
                                                     "migration",
                                                     "escrow",
                                                     "escrow_release",
                                                     "registration",
                                                     "revoke" };
        field_number                             = 0;
        for ( const auto &name : old_cases )
        {
            const auto *field = embedded->FindFieldByName( name );
            ASSERT_NE( field, nullptr );
            EXPECT_EQ( field->number(), ++field_number );
        }
        const auto *v2_case = embedded->FindFieldByName( "escrow_release_v2" );
        ASSERT_NE( v2_case, nullptr );
        EXPECT_EQ( v2_case->number(), 10 );
        EXPECT_EQ( v2_case->containing_oneof(), embedded->FindOneofByName( "transaction" ) );
        const auto *v2 = v2_case->message_type();
        EXPECT_EQ( v2->full_name(), "SGTransaction.EscrowReleaseTxV2" );
        ASSERT_EQ( v2->field_count(), 2 );
        EXPECT_EQ( v2->FindFieldByName( "dag_struct" )->number(), 1 );
        EXPECT_EQ( v2->FindFieldByName( "utxo_params" )->number(), 2 );

        SGTransaction::EscrowReleaseTx marker;
        marker.mutable_dag_struct()->set_type( "escrow-release" );
        marker.set_release_amount( 17 );
        marker.set_release_address( "recipient" );
        SGTransaction::EscrowReleaseTx restored_marker;
        ASSERT_TRUE( restored_marker.ParseFromString( marker.SerializeAsString() ) );
        EXPECT_EQ( restored_marker.release_amount(), 17 );
        EXPECT_EQ( restored_marker.release_address(), "recipient" );

        google::protobuf::DynamicMessageFactory    factory;
        std::unique_ptr<google::protobuf::Message> payment( factory.GetPrototype( v2 )->New() );
        auto *dag = payment->GetReflection()->MutableMessage( payment.get(), v2->FindFieldByName( "dag_struct" ) );
        dag->GetReflection()->SetString( dag, dag->GetDescriptor()->FindFieldByName( "type" ), "escrow-release-v2" );
        std::unique_ptr<google::protobuf::Message> restored_payment( payment->New() );
        ASSERT_TRUE( restored_payment->ParseFromString( payment->SerializeAsString() ) );
        const auto &restored_dag = restored_payment->GetReflection()->GetMessage( *restored_payment,
                                                                                  v2->FindFieldByName( "dag_struct" ) );
        EXPECT_EQ( restored_dag.GetReflection()->GetString( restored_dag,
                                                            restored_dag.GetDescriptor()->FindFieldByName( "type" ) ),
                   "escrow-release-v2" );
    }

    TEST( LegacyEscrowReleaseCodecTest, FixedHistoricalBytesPreserveMarkerFields )
    {
        namespace fixture = escrow_release_legacy_fixture;
        for ( const auto &expected : fixture::FIXTURES )
        {
            const auto raw = base::unhex( expected.raw_hex );
            if ( !raw )
            {
                FAIL() << "Historical fixture hex must decode";
            }
            const auto marker = DecodeRelease( "escrow-release", raw.value() );
            ASSERT_NE( marker, nullptr );
            const auto typed = std::dynamic_pointer_cast<EscrowReleaseTransaction>( marker );
            ASSERT_NE( typed, nullptr );
            EXPECT_EQ( marker->SerializeByteVector(), raw.value() );
            EXPECT_EQ( marker->GetType(), "escrow-release" );
            EXPECT_EQ( marker->GetTransactionSpecificPath(), "escrow-release" );
            EXPECT_EQ( marker->GetHash(), expected.hash );
            EXPECT_TRUE( marker->CheckHash() );
            EXPECT_TRUE( marker->CheckSignature() );
            EXPECT_FALSE( marker->HasUTXOParameters() );
            EXPECT_FALSE( marker->GetUTXOParametersOpt().has_value() );
            EXPECT_FALSE( marker->GetProducedUTXOs().has_value() );

            const auto &dag = marker->dag_st;
            EXPECT_EQ( dag.type(), "escrow-release" );
            EXPECT_EQ( dag.source_addr(), fixture::SOURCE );
            EXPECT_EQ( dag.previous_hash(), std::string( 64, '1' ) );
            EXPECT_EQ( dag.uncle_hash(), std::string( 64, '2' ) );
            EXPECT_EQ( dag.nonce(), 17 );
            EXPECT_EQ( dag.timestamp(), 1712345678 );
            const std::vector<uint8_t> signature( dag.signature().begin(), dag.signature().end() );
            EXPECT_EQ( base::hex_lower( signature ), expected.signature_hex );

            SGTransaction::EscrowReleaseTx wire;
            ASSERT_TRUE( wire.ParseFromArray( raw.value().data(), static_cast<int>( raw.value().size() ) ) );
            EXPECT_EQ( wire.release_amount(), 123456789 );
            EXPECT_EQ( wire.release_address(), "historical-release-address" );
            EXPECT_EQ( wire.escrow_source(), std::string( 128, 'b' ) );
            EXPECT_EQ( wire.original_escrow_hash(), std::string( 64, 'c' ) );
            EXPECT_EQ( typed->GetReleaseAmount(), wire.release_amount() );
            EXPECT_EQ( typed->GetReleaseAddress(), wire.release_address() );
            EXPECT_EQ( typed->GetEscrowSource(), wire.escrow_source() );
            EXPECT_EQ( typed->GetOriginalEscrowHash(), wire.original_escrow_hash() );
            EXPECT_EQ( typed->GetTopics(),
                       ( std::unordered_set<std::string>{ std::string( fixture::SOURCE ), std::string( 128, 'b' ) } ) );
            const auto created = EscrowReleaseTransaction::New( typed->GetUTXOParameters(),
                                                                typed->GetReleaseAmount(),
                                                                typed->GetReleaseAddress(),
                                                                typed->GetEscrowSource(),
                                                                typed->GetOriginalEscrowHash(),
                                                                typed->dag_st );
            EXPECT_EQ( created.SerializeByteVector(), raw.value() );
            ASSERT_EQ( wire.utxo_params().inputs_size(), 1 );
            EXPECT_EQ( wire.utxo_params().inputs( 0 ).tx_id_hash(), std::string( 64, 'a' ) );
            EXPECT_EQ( wire.utxo_params().inputs( 0 ).output_index(), 3 );
            EXPECT_EQ( wire.utxo_params().inputs( 0 ).signature(), std::string( "input\0signature", 15 ) );
            ASSERT_EQ( wire.utxo_params().outputs_size(), 1 );
            EXPECT_EQ( wire.utxo_params().outputs( 0 ).encrypted_amount(), 987654321 );
            EXPECT_EQ( wire.utxo_params().outputs( 0 ).dest_addr(), "historical-output-destination" );
            EXPECT_EQ( wire.utxo_params().outputs( 0 ).token_id().empty(), expected.omitted_token );
            if ( !expected.omitted_token )
            {
                EXPECT_EQ( wire.utxo_params().outputs( 0 ).token_id(), std::string( 30, '\0' ) + "\x12\x34" );
            }
            const auto embedded_bytes = base::unhex( expected.embedded_hex );
            if ( !embedded_bytes )
            {
                FAIL() << "Embedded historical fixture hex must decode";
            }
            EmbeddedTransaction embedded;
            ASSERT_TRUE( embedded.ParseFromArray( embedded_bytes.value().data(),
                                                  static_cast<int>( embedded_bytes.value().size() ) ) );
            ASSERT_EQ( embedded.transaction_case(), EmbeddedTransaction::kEscrowRelease );
            EXPECT_EQ( MessageBytes( marker->SerializeToEmbeddedTransaction() ), embedded_bytes.value() );
            const auto restored = DecodeRelease( "escrow-release", MessageBytes( embedded.escrow_release() ) );
            ASSERT_NE( restored, nullptr );
            EXPECT_EQ( restored->SerializeByteVector(), raw.value() );
            EXPECT_TRUE( restored->CheckSignature() );

            wire.set_release_amount( wire.release_amount() + 1 );
            const auto tampered = DecodeRelease( "escrow-release", MessageBytes( wire ) );
            ASSERT_NE( tampered, nullptr );
            EXPECT_FALSE( tampered->CheckHash() );
            EXPECT_FALSE( tampered->CheckSignature() );
        }
    }

    TEST( LegacyEscrowReleaseCodecTest, LegacyAndV2RemainDistinct )
    {
        const auto raw = base::unhex( escrow_release_legacy_fixture::FIXTURES.front().raw_hex );
        if ( !raw )
        {
            FAIL() << "Historical fixture hex must decode";
        }
        const auto legacy = DecodeRelease( "escrow-release", raw.value() );
        ASSERT_NE( legacy, nullptr );
        EXPECT_EQ( DecodeReleaseV2( raw.value() ), nullptr );
        const auto account = GeniusAccount::NewEphemeral( TokenID::FromBytes( { 0 } ) );
        ASSERT_NE( account, nullptr );
        const auto payment_bytes = MessageBytes( SignedPaymentWire( *account, 2 ) );
        const auto payment       = DecodeReleaseV2( payment_bytes );
        ASSERT_NE( payment, nullptr );
        EXPECT_NE( std::dynamic_pointer_cast<EscrowReleaseTransaction>( legacy ), nullptr );
        EXPECT_EQ( std::dynamic_pointer_cast<EscrowReleaseTransactionV2>( legacy ), nullptr );
        EXPECT_EQ( std::dynamic_pointer_cast<EscrowTransaction>( legacy ), nullptr );
        EXPECT_EQ( std::dynamic_pointer_cast<TransferTransaction>( legacy ), nullptr );
        EXPECT_NE( std::dynamic_pointer_cast<EscrowReleaseTransactionV2>( payment ), nullptr );
        EXPECT_EQ( std::dynamic_pointer_cast<EscrowReleaseTransaction>( payment ), nullptr );
        EXPECT_EQ( std::dynamic_pointer_cast<EscrowTransaction>( payment ), nullptr );
        EXPECT_EQ( std::dynamic_pointer_cast<TransferTransaction>( payment ), nullptr );
        EXPECT_EQ( DecodeRelease( "escrow-release", payment_bytes ), nullptr );
        EXPECT_EQ( DecodeRelease( "escrow-release", { 0xff } ), nullptr );
        EXPECT_NE( legacy->GetType(), payment->GetType() );
        EXPECT_NE( legacy->GetType(), "escrow-hold" );
        EXPECT_EQ( legacy->SerializeToEmbeddedTransaction().transaction_case(), EmbeddedTransaction::kEscrowRelease );
        EXPECT_EQ( payment->SerializeToEmbeddedTransaction().transaction_case(),
                   EmbeddedTransaction::kEscrowReleaseV2 );
    }
}
