#include <gtest/gtest.h>

#include <string>

#include "blockchain/Consensus.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "blockchain/impl/proto/ValidatorRegistry.pb.h"

namespace
{
    using sgns::ConsensusManager;
    using sgns::ConsensusProposal;
    using sgns::ConsensusSubject;
    using sgns::EmbeddedTransaction;
    using sgns::MintTxV2;
    using sgns::NonceSubject;
    using sgns::TransferTx;

    // Builds a NonceSubject-wrapped ConsensusProposal carrying the supplied
    // embedded transaction. Uses the production CreateNonceSubject so the
    // subject's payload/type-hash matches what the consensus path produces.
    ConsensusProposal MakeProposalWithTx( const EmbeddedTransaction &tx,
                                          const std::string         &account_id = "acct" )
    {
        ConsensusSubject subject =
            ConsensusManager::CreateNonceSubject( account_id, 1u, std::string( 32, '\xAA' ), tx, std::nullopt, std::nullopt )
                .value();
        ConsensusProposal proposal;
        *proposal.mutable_subject() = subject;
        return proposal;
    }

    EmbeddedTransaction MakeTransferTx()
    {
        EmbeddedTransaction out;
        TransferTx          transfer;
        ( *transfer.mutable_token_id() ) = std::string( "token", 5 );
        *out.mutable_transfer()          = transfer;
        return out;
    }

    EmbeddedTransaction MakeMintV2Tx( const std::string &chain_id )
    {
        EmbeddedTransaction out;
        MintTxV2            mint;
        if ( !chain_id.empty() )
        {
            ( *mint.mutable_chain_id() ) = chain_id;
        }
        ( *mint.mutable_token_id() ) = std::string( "token", 5 );
        mint.set_amount( 7 );
        *out.mutable_mint_v2() = mint;
        return out;
    }

    // Phase 6 bridge-mint discriminator (D-02/D-06): a proposal whose NonceSubject
    // embeds a kMintV2 transaction WITH a non-empty chain_id is a bridge mint.
    TEST( ConsensusBridgeMintSubjectTest, MintV2WithNonEmptyChainIdIsBridgeMint )
    {
        const auto proposal = MakeProposalWithTx( MakeMintV2Tx( "public" ) );
        EXPECT_TRUE( ConsensusManager::IsBridgeMintSubject( proposal ) );
    }

    // A MintV2 with an EMPTY chain_id is NOT a bridge mint -- it is a native
    // mint and uses single-pool quorum (mirror TransactionManager::GetValidationChainId).
    TEST( ConsensusBridgeMintSubjectTest, MintV2WithEmptyChainIdIsNotBridgeMint )
    {
        const auto proposal = MakeProposalWithTx( MakeMintV2Tx( "" ) );
        EXPECT_FALSE( ConsensusManager::IsBridgeMintSubject( proposal ) );
    }

    // A non-MintV2 transaction (TransferTx) is never a bridge mint.
    TEST( ConsensusBridgeMintSubjectTest, TransferTxIsNotBridgeMint )
    {
        const auto proposal = MakeProposalWithTx( MakeTransferTx() );
        EXPECT_FALSE( ConsensusManager::IsBridgeMintSubject( proposal ) );
    }

    // Fail-closed (RESEARCH Pattern 2): a subject that does not decode as a
    // NonceSubject must be treated as non-bridge (single-pool applies). We
    // construct a proposal whose subject has an empty payload.
    TEST( ConsensusBridgeMintSubjectTest, UndecodableSubjectFailsClosedToNonBridge )
    {
        ConsensusProposal proposal;
        // Default-constructed subject -- no payload, no type hash.
        EXPECT_FALSE( ConsensusManager::IsBridgeMintSubject( proposal ) );
    }
} // namespace
