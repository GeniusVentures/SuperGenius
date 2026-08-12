#include <gtest/gtest.h>

#include "account/TrustStartupController.hpp"

namespace
{
    using sgns::account::TrustStartupController;

    TEST( TrustFirstBootE2ETest, FreshStateIsRestrictedUntilBothDurableGenesisStages )
    {
        EXPECT_EQ( TrustStartupController::State::FreshWaitingForGenesis,
                   TrustStartupController::State::FreshWaitingForGenesis );
        EXPECT_NE( TrustStartupController::State::FreshWaitingForGenesis,
                   TrustStartupController::State::ConfirmedReady );
    }
} // namespace
