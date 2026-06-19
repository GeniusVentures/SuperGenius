/**
 * @file       consensus_slot_hash_populator_test.cpp
 * @brief      Phase 6 (D-01) tests for ConsensusManager slot-hash populator wiring.
 * @details    Verifies that SetSlotHashPopulator injects a callback invoked
 *             during CreateVote before signing, so the signed vote commits to
 *             the slot hashes (T-06-01).
 * @date       2026-06-19
 */

#include <gtest/gtest.h>

#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace
{
    constexpr const char *kValidatorId = "validator-slot-hash-populator";

    std::vector<uint8_t> DummySignature( std::vector<uint8_t> )
    {
        return std::vector<uint8_t>{ 0x09, 0x03 };
    }
} // namespace

namespace sgns
{
    class ConsensusSlotHashPopulatorTest : public test::CRDTFixture
    {
    public:
        ConsensusSlotHashPopulatorTest() : CRDTFixture( "consensus_slot_hash_populator_test" ) {}

    protected:
        std::shared_ptr<sgns::ValidatorRegistry> MakeRegistry()
        {
            auto registry = sgns::ValidatorRegistry::New(
                db_,
                1,
                1,
                sgns::ValidatorRegistry::WeightConfig{},
                kValidatorId,
                []( const std::string &, std::function<void( outcome::result<std::string> )> cb )
                { cb( outcome::failure( std::errc::not_supported ) ); } );
            EXPECT_TRUE( registry );

            auto store_result = registry->StoreGenesisRegistry( kValidatorId, DummySignature );
            EXPECT_FALSE( store_result.has_error() );

            ASSERT_WAIT_FOR_CONDITION(
                [&registry]()
                {
                    auto load = registry->LoadRegistry();
                    return load.has_value() && !registry->GetRegistryCid().empty();
                },
                std::chrono::milliseconds( 2000 ),
                "registry initialized",
                nullptr );

            return registry;
        }

        std::shared_ptr<sgns::ConsensusManager> MakeManager(
            const std::shared_ptr<sgns::ValidatorRegistry> &registry )
        {
            auto manager = sgns::ConsensusManager::New(
                registry,
                db_,
                pubs_,
                []( std::vector<uint8_t> payload ) -> outcome::result<std::vector<uint8_t>>
                { return DummySignature( std::move( payload ) ); },
                kValidatorId );
            EXPECT_TRUE( manager );
            return manager;
        }
    };

    // Phase 6 (D-01): the populator callback is invoked during CreateVote,
    // before signing, so the resulting signed vote carries the slot hashes.
    TEST_F( ConsensusSlotHashPopulatorTest, CreateVoteInvokesPopulatorBeforeSigning )
    {
        auto registry = MakeRegistry();
        auto manager  = MakeManager( registry );
        ASSERT_TRUE( manager );

        const std::string expected_slot0( 32, '\xA1' );
        bool              callback_invoked = false;

        manager->SetSlotHashPopulator(
            [expected_slot0, &callback_invoked]( sgns::ConsensusVote &vote )
            {
                vote.set_slot_0_hash( expected_slot0 );
                callback_invoked = true;
            } );

        auto vote_result = manager->CreateVote(
            "proposal-slot-test",
            kValidatorId,
            /*approve=*/true,
            []( std::vector<uint8_t> payload ) -> outcome::result<std::vector<uint8_t>>
            { return DummySignature( std::move( payload ) ); } );

        ASSERT_FALSE( vote_result.has_error() );
        EXPECT_TRUE( callback_invoked );
        EXPECT_EQ( vote_result.value().slot_0_hash(), expected_slot0 );
    }

    // Without a populator configured, CreateVote behaves as before (no-op,
    // backward compatible) and slot hashes stay empty.
    TEST_F( ConsensusSlotHashPopulatorTest, CreateVoteWithoutPopulatorLeavesSlotHashesEmpty )
    {
        auto registry = MakeRegistry();
        auto manager  = MakeManager( registry );
        ASSERT_TRUE( manager );

        auto vote_result = manager->CreateVote(
            "proposal-no-populator",
            kValidatorId,
            /*approve=*/true,
            []( std::vector<uint8_t> payload ) -> outcome::result<std::vector<uint8_t>>
            { return DummySignature( std::move( payload ) ); } );

        ASSERT_FALSE( vote_result.has_error() );
        EXPECT_TRUE( vote_result.value().slot_0_hash().empty() );
        EXPECT_TRUE( vote_result.value().slot_1_hash().empty() );
        EXPECT_TRUE( vote_result.value().slot_2_hash().empty() );
    }
} // namespace sgns
