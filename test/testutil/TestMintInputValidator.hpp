/**
 * @file       TestMintInputValidator.hpp
 * @brief      Test-only input validator that accepts synthetic mint transactions
 *             without requiring RPC burn verification.
 * @date       2026-06-09
 */
#ifndef TESTUTIL_TEST_MINT_INPUT_VALIDATOR_HPP
#define TESTUTIL_TEST_MINT_INPUT_VALIDATOR_HPP

#include "account/InputValidators.hpp"

namespace sgns::test
{
    /// Canonical numeric chain id reserved by tests for RPC-free synthetic mints.
    inline constexpr char kTestMintChainId[] = "0";

    class TestMintInputValidator final : public IInputValidator
    {
    public:
        static bool RegisterTestValidator()
        {
            static TestMintInputValidator instance;
            IInputValidator::Register( kTestMintChainId, &instance );
            return true;
        }

        bool ValidateUTXOParameters( const UTXOTxParameters & /*params*/,
                                     const std::string & /*address*/,
                                     const UTXOManager & /*utxo_manager*/ ) const override
        {
            return true;
        }

        bool ValidateWitness( const ConsensusSubject & /*subject*/,
                              const std::shared_ptr<GeniusTransaction> & /*tx*/,
                              const UTXOTxParameters & /*params*/,
                              const std::shared_ptr<Blockchain> & /*blockchain*/ ) const override
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
