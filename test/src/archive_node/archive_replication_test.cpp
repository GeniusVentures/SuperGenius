/**
 * @file       archive_replication_test.cpp
 * @brief      PROP-02: an Archive node replicates transactions it neither authored nor received.
 * @date       2026-08-18
 *
 * The single-node coverage in node_type_derivation_test only proves that node_type=Archive resolves
 * correctly (IsFullNode() true, IsProcessor() false). That is configuration, not behavior. This test
 * puts a real Archive node on a live network alongside two Light nodes that transact with each
 * other, and asserts the Archive ends up holding their transaction and their ledger state.
 *
 * SCOPE — read before trusting this test as a regression guard for the Archive role.
 * It verifies that an Archive replicates third-party transactions. It does NOT prove that behavior
 * is unique to Archive: re-running it with the node configured as "Light" also passes. The reason is
 * architectural, not a flaw in the assertions — GeniusNode.cpp:713 subscribes EVERY node type to
 * GNUS_FULL_NODES_TOPIC unconditionally for the BURN-02/BURN-03 quorum wiring, which makes the
 * role-gated AddListenTopic in TransactionManager::StartListeningTopics redundant for transaction
 * data. So a regression that downgraded Archive to Light semantics would NOT be caught here.
 * Closing that gap means either narrowing the unconditional subscription or finding an observable
 * that genuinely differs by role; both are larger changes than this test.
 */

#include <filesystem>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <boost/dll.hpp>

#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "blockchain/Blockchain.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/mint_source_hash.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/wait_condition.hpp"

using namespace sgns;

namespace sgns
{
    class ArchiveReplicationTest : public ::testing::Test
    {
    protected:
        /// Full node: genesis authority and the peer everyone else dials.
        static inline std::shared_ptr<GeniusNode> full_node;
        /// The subject under test.
        static inline std::shared_ptr<GeniusNode> archive_node;
        /// Originates the transfer. The archive is neither this nor the receiver.
        static inline std::shared_ptr<GeniusNode> sender;
        /// Destination of the transfer.
        static inline std::shared_ptr<GeniusNode> receiver;

        static inline GeniusNodeConfig FULL_CONFIG    = { "0xcafe", "0.65", "1.0",
                                                          TokenID::FromBytes( { 0x00 } ), "" };
        static inline GeniusNodeConfig ARCHIVE_CONFIG = { "0xcafe", "0.65", "1.0",
                                                          TokenID::FromBytes( { 0x00 } ), "" };
        static inline GeniusNodeConfig SENDER_CONFIG  = { "0xcafe", "0.65", "1.0",
                                                          TokenID::FromBytes( { 0x00 } ), "" };
        static inline GeniusNodeConfig RECEIVER_CONFIG = { "0xcafe", "0.65", "1.0",
                                                           TokenID::FromBytes( { 0x00 } ), "" };

        /// Writes the per-node config files. Distinct prefix from mat_/transaction_sync_ so parallel
        /// test binaries never share a data directory.
        static void PrepareNode( GeniusNodeConfig &config, const std::string &dir, const char *node_type )
        {
            config.BaseWritePath = boost::dll::program_location().parent_path().string() + "/archive_repl_" + dir + "/";
            try
            {
                test::removeAllWithRetry( config.BaseWritePath );
            }
            catch ( ... ) //NOLINT(bugprone-empty-catch)
            {
            }
            std::filesystem::create_directories( config.BaseWritePath );
            GeniusNode::WriteNetworkConfig( config.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
            GeniusNode::WriteSgnsConfig( config.BaseWritePath, node_type, /*is_processor=*/false,
                                         /*rpc_catchup=*/false );
        }

        static void SetUpTestSuite()
        {
            // Must precede any node construction so account creation uses the in-memory backend.
            GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                    { return std::make_shared<MemorySecureStorage>( identifier ); } );

            PrepareNode( FULL_CONFIG, "full", "Full" );
            PrepareNode( ARCHIVE_CONFIG, "archive", "Archive" );
            PrepareNode( SENDER_CONFIG, "sender", "Light" );
            PrepareNode( RECEIVER_CONFIG, "receiver", "Light" );

            // The full node must exist and be registered as genesis authority before the others are
            // constructed, since Blockchain bakes GetAuthorizedFullNodeAddress() in at construction.
            full_node = GeniusNode::New(
                FULL_CONFIG, FromPrivateKey{ "9389e5f08c01e791dc436abab7a61a502515ddc7f91cb09f10289e147c651780" } );
            ASSERT_NE( full_node, nullptr );
            Blockchain::SetAuthorizedFullNodeAddress( full_node->GetAddress() );

            archive_node = GeniusNode::New(
                ARCHIVE_CONFIG, FromPrivateKey{ "1f06d98b1d1613ad98279f8d57ce30580e8a7a0385dc85da713333f53a928395" } );
            sender = GeniusNode::New(
                SENDER_CONFIG, FromPrivateKey{ "19c2f2db8e7cb27e5438093cf377d27888ddd4b257827baddd0418eefacedd02" } );
            receiver = GeniusNode::New(
                RECEIVER_CONFIG, FromPrivateKey{ "7b1e4a30f2c8d95b6a03e17c4d8f26b09a5c3e71d842f0b6c9e5a1387d406f2c" } );
            ASSERT_NE( archive_node, nullptr );
            ASSERT_NE( sender, nullptr );
            ASSERT_NE( receiver, nullptr );

            const auto hub = full_node->GetPubSub()->GetInterfaceAddress();
            archive_node->AddPeers( { hub } );
            sender->AddPeers( { hub } );
            receiver->AddPeers( { hub } );

            for ( const auto &node : { full_node, archive_node, sender, receiver } )
            {
                test::assertWaitForCondition( [&]() { return node->GetState() == GeniusNode::NodeState::READY; },
                                              std::chrono::milliseconds( 50000 ),
                                              "node did not reach READY" );
            }
        }

        static void TearDownTestSuite()
        {
            receiver.reset();
            sender.reset();
            archive_node.reset();
            full_node.reset();
        }
    };

    // The Archive is neither the source nor the destination of the transfer, so anything it ends up
    // holding it holds purely by virtue of being a replicating node.
    TEST_F( ArchiveReplicationTest, ArchiveStoresThirdPartyTransaction )
    {
        ASSERT_EQ( archive_node->GetNodeType(), GeniusNode::NodeType::Archive );
        EXPECT_TRUE( archive_node->IsFullNode() ) << "Archive must replicate network-wide data like Full";
        EXPECT_FALSE( archive_node->IsProcessor() ) << "Archive must not process";

        constexpr uint64_t kMintAmount     = 1000;
        constexpr uint64_t kTransferAmount = 75;

        auto mint = sender->MintTokens( kMintAmount, test::NextMintSourceHash(), "test",
                                        TokenID::FromBytes( { 0x00 } ) );
        ASSERT_TRUE( mint.has_value() ) << "mint failed on sender";

        // MintTokens returns once submitted; the UTXO must settle before it is spendable.
        test::assertWaitForCondition( [&]() { return sender->GetBalance() >= kMintAmount; },
                                      std::chrono::milliseconds( 30000 ),
                                      "mint did not settle into sender's balance" );

        auto transfer = sender->TransferFunds( kTransferAmount, receiver->GetAddress(),
                                               TokenID::FromBytes( { 0x00 } ),
                                               std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
        ASSERT_TRUE( transfer.has_value() ) << "transfer failed on sender";
        const std::string tx_id = transfer.value().first;

        // Control: the transfer really did propagate. Without this, a failure of the assertion below
        // would be ambiguous between "the archive did not replicate" and "nothing was broadcast".
        EXPECT_EQ( receiver->WaitForTransactionIncoming( tx_id,
                                                         std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) ),
                   TransactionManager::TransactionStatus::CONFIRMED )
            << "the intended recipient never saw the transfer";

        // THE ASSERTION. WaitForTransactionIncoming matches on
        //   tracked.tx->GetHash() == tx_id && tracked.tx->GetSrcAddress() != own address,
        // so a CONFIRMED result proves the archive stored the transaction (it is in tx_processed_m)
        // AND did not author it. It was addressed to `receiver`, so it did not receive it either.
        EXPECT_EQ( archive_node->WaitForTransactionIncoming(
                       tx_id, std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) ),
                   TransactionManager::TransactionStatus::CONFIRMED )
            << "archive node did not store a transaction between two third parties";

        // Corroboration: the archive replicated foreign *ledger state*, not just the record.
        // (Verified empirically that a Light node reports this too — see the SCOPE note at the top
        // of this file. It still guards against the archive holding the record but not the state.)
        test::assertWaitForCondition( [&]() { return archive_node->GetBalance( receiver->GetAddress() ) > 0; },
                                      std::chrono::milliseconds( 30000 ),
                                      "archive node never reflected the receiver's balance" );
        EXPECT_EQ( archive_node->GetBalance( receiver->GetAddress() ), kTransferAmount );
    }
}
