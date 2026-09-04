/**
 * @file       TestMintInputValidator.hpp
 * @brief      Test-only input validator that accepts mint transactions for chain_id="test"
 *             without requiring RPC burn verification.
 * @date       2026-06-09
 */
#ifndef TESTUTIL_TEST_MINT_INPUT_VALIDATOR_HPP
#define TESTUTIL_TEST_MINT_INPUT_VALIDATOR_HPP

#include "account/InputValidators.hpp"

namespace sgns::test
{
    class TestMintInputValidator final : public IInputValidator
    {
    public:
        static bool RegisterTestValidator()
        {
            static TestMintInputValidator instance;
            IInputValidator::Register( "test", &instance );
            IInputValidator::Register( "0", &instance );
            return true;
        }

        bool ValidateUTXOParameters( const UTXOTxParameters & /*params*/,
                                     const std::string & /*address*/,
                                     const UTXOManager & /*utxo_manager*/ ) const override
        {
            return true;
        }

        bool ValidateWitness( const ConsensusSubject & /*subject*/,
                              const GeniusTransaction & /*tx*/,
                              const UTXOTxParameters & /*params*/,
                              const Blockchain & /*blockchain*/ ) const override
        {
            return true;
        }

        bool RequiresConsensusUTXOData() const override
        {
            return false;
        }
    };
} // namespace sgns::test

static inline bool kTestMintValidatorRegistered = sgns::test::TestMintInputValidator::RegisterTestValidator();

#endif // TESTUTIL_TEST_MINT_INPUT_VALIDATOR_HPP
