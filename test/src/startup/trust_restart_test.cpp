#include <gtest/gtest.h>

#include "account/TrustStartupController.hpp"

TEST( TrustRestartTest, ExposesStructuredLastEventForRestartDiagnostics )
{
    EXPECT_FALSE( sgns::account::TrustStartupController::LastEventCodeForTesting().has_value() );
}
