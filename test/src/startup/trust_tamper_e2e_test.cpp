#include <gtest/gtest.h>

#include "account/TrustStartupController.hpp"

TEST( TrustTamperE2ETest, ExposesStructuredLastEventForTamperDiagnostics )
{
    EXPECT_FALSE( sgns::account::TrustStartupController::LastEventCodeForTesting().has_value() );
}
