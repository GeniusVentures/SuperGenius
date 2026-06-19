#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "blockchain/ValidatorRegistry.hpp"
#include "blockchain/impl/proto/ValidatorRegistry.pb.h"

namespace
{
    using sgns::ValidatorRegistry;
    using sgns::validator::Role;
    using sgns::validator::Status;
    using sgns::validator::ValidatorEntry;

    // Builds an in-memory ValidatorEntry with explicit role/weight/penalty.
    // ApplyVoteEffects cannot be unit-tested without a fully wired
    // ValidatorRegistry (GlobalDB + GossipPubSub + block-request callback), so
    // the promotion decision is extracted into EvaluateRegularPromotionStatic,
    // mirroring the EvaluateSlotQuorumStatic pattern from Plan 06-02.
    ValidatorEntry MakeEntry( Role     role,
                              uint64_t weight,
                              uint32_t penalty,
                              Status   status = sgns::validator::ACTIVE )
    {
        ValidatorEntry entry;
        entry.set_validator_id( "v" );
        entry.set_role( role );
        entry.set_weight( weight );
        entry.set_status( status );
        entry.set_penalty_score( penalty );
        entry.set_missed_epochs( 0 );
        return entry;
    }

    // A REGULAR validator that has accumulated weight up to the promotion
    // threshold (default 500) and has a low penalty score must be promoted.
    TEST( ValidatorRegistryPromotionTest, RegularAtThresholdLowPenaltyPromotes )
    {
        ValidatorRegistry::WeightConfig cfg;
        const auto entry = MakeEntry( Role::REGULAR, cfg.full_promotion_weight_, 0 );
        EXPECT_TRUE( ValidatorRegistry::EvaluateRegularPromotionStatic( entry, cfg ) );
    }

    // Just above the threshold must still promote.
    TEST( ValidatorRegistryPromotionTest, RegularAboveThresholdPromotes )
    {
        ValidatorRegistry::WeightConfig cfg;
        const auto entry = MakeEntry( Role::REGULAR, cfg.full_promotion_weight_ + 1, 0 );
        EXPECT_TRUE( ValidatorRegistry::EvaluateRegularPromotionStatic( entry, cfg ) );
    }

    // One below the threshold must NOT promote.
    TEST( ValidatorRegistryPromotionTest, RegularBelowThresholdDoesNotPromote )
    {
        ValidatorRegistry::WeightConfig cfg;
        const auto entry = MakeEntry( Role::REGULAR, cfg.full_promotion_weight_ - 1, 0 );
        EXPECT_FALSE( ValidatorRegistry::EvaluateRegularPromotionStatic( entry, cfg ) );
    }

    // A high-penalty REGULAR validator at the threshold must NOT be promoted.
    TEST( ValidatorRegistryPromotionTest, HighPenaltyBlocksPromotion )
    {
        ValidatorRegistry::WeightConfig cfg;
        const auto entry =
            MakeEntry( Role::REGULAR, cfg.full_promotion_weight_, cfg.penalty_threshold_ );
        EXPECT_FALSE( ValidatorRegistry::EvaluateRegularPromotionStatic( entry, cfg ) );
    }

    // A penalty strictly below the threshold still promotes (boundary).
    TEST( ValidatorRegistryPromotionTest, PenaltyJustBelowThresholdPromotes )
    {
        ValidatorRegistry::WeightConfig cfg;
        const auto entry =
            MakeEntry( Role::REGULAR, cfg.full_promotion_weight_, cfg.penalty_threshold_ - 1 );
        EXPECT_TRUE( ValidatorRegistry::EvaluateRegularPromotionStatic( entry, cfg ) );
    }

    // GENESIS is never demoted to FULL.
    TEST( ValidatorRegistryPromotionTest, GenesisIsNotPromoted )
    {
        ValidatorRegistry::WeightConfig cfg;
        const auto entry = MakeEntry( Role::GENESIS, cfg.full_promotion_weight_, 0 );
        EXPECT_FALSE( ValidatorRegistry::EvaluateRegularPromotionStatic( entry, cfg ) );
    }

    // SHARDED is not promoted by the REGULAR->FULL rule.
    TEST( ValidatorRegistryPromotionTest, ShardedIsNotPromoted )
    {
        ValidatorRegistry::WeightConfig cfg;
        const auto entry = MakeEntry( Role::SHARDED, cfg.full_promotion_weight_, 0 );
        EXPECT_FALSE( ValidatorRegistry::EvaluateRegularPromotionStatic( entry, cfg ) );
    }

    // An already-FULL validator is not re-promoted (idempotent).
    TEST( ValidatorRegistryPromotionTest, FullIsNotRePromoted )
    {
        ValidatorRegistry::WeightConfig cfg;
        const auto entry = MakeEntry( Role::FULL, cfg.full_promotion_weight_, 0 );
        EXPECT_FALSE( ValidatorRegistry::EvaluateRegularPromotionStatic( entry, cfg ) );
    }

    // Determinism: the same inputs always produce the same decision.
    TEST( ValidatorRegistryPromotionTest, DecisionIsDeterministicAcrossCalls )
    {
        ValidatorRegistry::WeightConfig cfg;
        const auto entry = MakeEntry( Role::REGULAR, cfg.full_promotion_weight_ + 100, 3 );
        const bool first  = ValidatorRegistry::EvaluateRegularPromotionStatic( entry, cfg );
        const bool second = ValidatorRegistry::EvaluateRegularPromotionStatic( entry, cfg );
        EXPECT_EQ( first, second );
        EXPECT_TRUE( first );
    }
} // namespace
