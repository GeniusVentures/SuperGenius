#include "account/GeniusNode.hpp"
#include "account/TokenID.hpp"
#include "blockchain/Blockchain.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/genius_node_test_access.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/wait_condition.hpp"
#include <algorithm>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

using namespace sgns;

namespace
{
    // Same private key across every scene -> same account address -> deterministic ports.
    // The only variable between scenes is the config file content.
    constexpr const char *TEST_PRIVATE_KEY = "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa";

    // Valid private_network_id per the Phase-15 Task-2 encoding decision (0x-hex-32B):
    // "0x" + exactly 64 non-zero hex digits = 66 characters.
    constexpr const char *VALID_PRIVATE_NETWORK_ID =
        "0x3c978f8d1e2a4b6f9c0d5e7a8b1c3d5f7a9b1c3d5e7f9a1b3c5d7e9f1b3d5a7c";

    // Valid network_key (pnet PSK) in the file-safe plain base16 32-byte encoding accepted by
    // Psk::fromBase16String. Intentionally a DIFFERENT value than the id (D-02).
    constexpr const char *VALID_NETWORK_KEY_BASE16 = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

    constexpr const char *BOOTSTRAP_PEER_ONE = "QmYyQSo1c1Ym7orWxLYvCrM2EmxFTANf8wXmmE7DWjhx5N";
    constexpr const char *BOOTSTRAP_PEER_TWO = "QmV1b7QdXbxcdRdLhxZsPVDKZsE3TNxrNcv3L4wmydV3Yr";

    GeniusNodeConfig MakeDevConfig( const boost::filesystem::path &base )
    {
        return { "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), base.generic_string() + '/' };
    }

    boost::filesystem::path MakeTempDir( const std::string &name )
    {
        auto path = boost::dll::program_location().parent_path() / name;
        try
        {
            test::removeAllWithRetry( path.string() );
        }
        catch ( ... ) //NOLINT(bugprone-empty-catch)
        {
        }
        boost::filesystem::create_directories( path );
        return path;
    }

    // In-memory secure storage (no file/platform keychain access) — must be set before any
    // GeniusNode construction so account creation uses the test backend.
    void UseMemorySecureStorage()
    {
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );
    }

    // Post-closeout base note: a no-genesis (no trusted_peers) node fail-closes during trust
    // startup (TrustedPeerRegistry::New rejects the empty signer set below the majority-safety
    // floor), so READY is unreachable for these config-only scenes on this base — the same
    // pre-existing condition times out network_config_precedence_test's WaitForReady scenes.
    // Every identity assertion in this suite is about LoadNetworkConfig/InitNetwork, which run
    // synchronously inside New(), so none of them depend on READY. Wait only until the
    // asynchronous initialization job has started, then let it quiesce briefly before teardown.
    void WaitForStartupSettled( const std::shared_ptr<GeniusNode> &node )
    {
        test::assertWaitForCondition( [&]() { return node->GetState() != GeniusNode::NodeState::CREATING; },
                                      std::chrono::seconds( 50 ),
                                      "node initialization did not start" );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    }

    std::string ReadConfigFile( const std::string &base_path )
    {
        std::ifstream     config( base_path + "network_config.json" );
        std::stringstream buffer;
        buffer << config.rdbuf();
        return buffer.str();
    }

    // Writes a network_config.json whose object is the public-node base scene plus
    // `extra_members` (raw JSON member text including its leading comma, e.g.
    // ", \"private_network_id\": \"0x...\""). Same hand-rolled-JSON approach as
    // network_config_precedence_test's WriteNetworkConfigWithMultiplier.
    void WriteRawNetworkConfig( const std::string &base_path, const std::string &extra_members )
    {
        std::ofstream config( base_path + "network_config.json" );
        ASSERT_TRUE( config.good() );
        // Escaped string (not a raw string): a raw literal here reads fine without its
        // closing paren and silently swallows the rest of the file into the string.
        config << "{ \"port_seed\": 0, \"auto_dht\": false, \"upnp_enabled\": false" << extra_members << " }";
    }

    // Full scene: base network config + extra members, sgns config, then New().
    std::shared_ptr<GeniusNode> NodeFromRawConfig( const std::string &dir, const std::string &extra_members )
    {
        UseMemorySecureStorage();
        auto       base       = MakeTempDir( dir );
        const auto dev_config = MakeDevConfig( base );
        WriteRawNetworkConfig( dev_config.BaseWritePath, extra_members );
        sgns::GeniusNode::WriteSgnsConfig( dev_config.BaseWritePath,
                                           /*node_type=*/"Full",
                                           /*is_processor=*/true,
                                           /*rpc_catchup=*/false );
        return sgns::GeniusNode::New( dev_config, sgns::FromPrivateKey{ TEST_PRIVATE_KEY } );
    }
} // namespace

// Absent identity keys keep the exact public-node behavior: the node starts, retains an empty
// private_network_id_, and WriteNetworkConfig's defaults emit no identity keys at all.
TEST( NetworkConfigPrivateNetwork, AbsentKeysKeepPublicNodeBehavior )
{
    UseMemorySecureStorage();
    auto       base       = MakeTempDir( "ncpn_absent" );
    const auto dev_config = MakeDevConfig( base );
    sgns::GeniusNode::WriteNetworkConfig( dev_config.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
    sgns::GeniusNode::WriteSgnsConfig( dev_config.BaseWritePath,
                                       /*node_type=*/"Full",
                                       /*is_processor=*/true,
                                       /*rpc_catchup=*/false );

    // Default arguments write no identity material: no key names, no placeholders.
    const auto config_text = ReadConfigFile( dev_config.BaseWritePath );
    EXPECT_EQ( config_text.find( "private_network_id" ), std::string::npos );
    EXPECT_EQ( config_text.find( "network_bootstrap_peers" ), std::string::npos );
    EXPECT_EQ( config_text.find( "network_key" ), std::string::npos );

    auto node = sgns::GeniusNode::New( dev_config, sgns::FromPrivateKey{ TEST_PRIVATE_KEY } );
    ASSERT_NE( node, nullptr );
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );

    EXPECT_TRUE( GeniusNodeTestAccess::PrivateNetworkId( node ).empty() );
    EXPECT_TRUE( GeniusNodeTestAccess::NetworkBootstrapPeers( node ).empty() );
    ASSERT_NO_FATAL_FAILURE( WaitForStartupSettled( node ) );
}

// A fully-provisioned pair (D-01): the valid public id is parsed, retained by InitNetwork,
// round-trips through WriteNetworkConfig, and stays distinct from the network_key secret (D-02).
// The offline-provisioned bootstrap membership is retained alongside it.
TEST( NetworkConfigPrivateNetwork, ValidIdentityPairIsRetainedAndDistinctFromKey )
{
    UseMemorySecureStorage();
    auto       base       = MakeTempDir( "ncpn_valid_pair" );
    const auto dev_config = MakeDevConfig( base );
    sgns::GeniusNode::WriteNetworkConfig( dev_config.BaseWritePath,
                                          /*port_seed=*/0,
                                          /*auto_dht=*/false,
                                          VALID_NETWORK_KEY_BASE16,
                                          VALID_PRIVATE_NETWORK_ID,
                                          { BOOTSTRAP_PEER_ONE, BOOTSTRAP_PEER_TWO } );
    sgns::GeniusNode::WriteSgnsConfig( dev_config.BaseWritePath,
                                       /*node_type=*/"Full",
                                       /*is_processor=*/true,
                                       /*rpc_catchup=*/false );

    // Round-trip step 1 — the writer emits both identity keys and the bootstrap array.
    const auto config_text = ReadConfigFile( dev_config.BaseWritePath );
    EXPECT_NE( config_text.find( "\"private_network_id\": \"" + std::string( VALID_PRIVATE_NETWORK_ID ) + "\"" ),
               std::string::npos );
    EXPECT_NE( config_text.find( "\"network_bootstrap_peers\": [\"" + std::string( BOOTSTRAP_PEER_ONE ) + "\", \"" +
                                 BOOTSTRAP_PEER_TWO + "\"]" ),
               std::string::npos );

    // Round-trip step 2 — LoadNetworkConfig (inside New) re-reads and retains them.
    auto node = sgns::GeniusNode::New( dev_config, sgns::FromPrivateKey{ TEST_PRIVATE_KEY } );
    ASSERT_NE( node, nullptr );
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );

    EXPECT_EQ( GeniusNodeTestAccess::PrivateNetworkId( node ), VALID_PRIVATE_NETWORK_ID );
    // D-02: the public id is NOT the pnet secret (and never a substitute for it).
    EXPECT_NE( GeniusNodeTestAccess::PrivateNetworkId( node ), VALID_NETWORK_KEY_BASE16 );

    const auto peers = GeniusNodeTestAccess::NetworkBootstrapPeers( node );
    ASSERT_EQ( peers.size(), 2u );
    EXPECT_EQ( peers[0], BOOTSTRAP_PEER_ONE );
    EXPECT_EQ( peers[1], BOOTSTRAP_PEER_TWO );
    ASSERT_NO_FATAL_FAILURE( WaitForStartupSettled( node ) );
}

// The encoding contract is case-insensitive hex digits in the body ([0-9a-fA-F]) with a
// lowercase 0x prefix — an uppercase-hex-body id is valid.
TEST( NetworkConfigPrivateNetwork, UppercaseHexBodyIdentityAccepted )
{
    const std::string uppercase_id = "0xABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789";
    ASSERT_EQ( uppercase_id.size(), 66u );

    auto node = NodeFromRawConfig( "ncpn_uppercase",
                                   ", \"network_key\": \"" + std::string( VALID_NETWORK_KEY_BASE16 ) +
                                       "\", \"private_network_id\": \"" + uppercase_id + "\"" );
    ASSERT_NE( node, nullptr );
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );
    EXPECT_EQ( GeniusNodeTestAccess::PrivateNetworkId( node ), uppercase_id );
    ASSERT_NO_FATAL_FAILURE( WaitForStartupSettled( node ) );
}

// Malformed private_network_id values (Task-2 encoding: 0x-hex-32B) each fail the
// configuration load — GeniusNode::New returns nullptr and no node starts. The pnet key is
// provisioned alongside so only the id malformation can be the trigger.
TEST( NetworkConfigPrivateNetwork, MalformedIdentityFailsNodeStart )
{
    const std::string valid = VALID_PRIVATE_NETWORK_ID;

    const std::vector<std::pair<std::string, std::string>> malformed = {
        { "63 hex digits", valid.substr( 0, valid.size() - 1 ) },
        { "65 hex digits", valid + "a" },
        { "non-hex digit", std::string( valid ).replace( 10, 1, "z" ) },
        { "all-zero id", "0x" + std::string( 64, '0' ) },
        { "uppercase 0X prefix", "0X" + valid.substr( 2 ) },
        { "missing 0x prefix", valid.substr( 2 ) },
    };

    for ( const auto &[reason, id] : malformed )
    {
        auto node = NodeFromRawConfig( "ncpn_malformed",
                                       ", \"network_key\": \"" + std::string( VALID_NETWORK_KEY_BASE16 ) +
                                           "\", \"private_network_id\": \"" + id + "\"" );
        EXPECT_EQ( node, nullptr ) << "malformed private_network_id must fail the load: " << reason << " (" << id
                                   << ")";
    }
}

// D-01 provisioning pair: exactly one of private_network_id / network_key set fails the load.
// network_key alone would run a PSK-isolated node writing private-intent data into PUBLIC CRDT
// paths; the id alone claims a private namespace with no transport protection.
TEST( NetworkConfigPrivateNetwork, HalfProvisionedIdentityPairFailsNodeStart )
{
    {
        auto node = NodeFromRawConfig( "ncpn_key_without_id",
                                       ", \"network_key\": \"" + std::string( VALID_NETWORK_KEY_BASE16 ) + "\"" );
        EXPECT_EQ( node, nullptr ) << "network_key without private_network_id must fail the load";
    }
    {
        auto node = NodeFromRawConfig(
            "ncpn_id_without_key",
            ", \"private_network_id\": \"" + std::string( VALID_PRIVATE_NETWORK_ID ) + "\"" );
        EXPECT_EQ( node, nullptr ) << "private_network_id without network_key must fail the load";
    }
}

// CR-01 regression: the canonical go-ipfs swarm-key text (SWARM_KEY_PNET shape with embedded
// literal newline bytes - the format Psk::fromSwarmKeyText parses) must survive a
// WriteNetworkConfig -> GeniusNode::New round-trip. Pre-fix the writer emitted raw newline
// bytes (illegal in JSON strings), the reload hit the parse-error branch, and the node
// silently booted PUBLIC with an empty private_network_id.
TEST( NetworkConfigPrivateNetwork, SwarmKeyTextWithNewlinesRoundTrips )
{
    UseMemorySecureStorage();
    auto       base       = MakeTempDir( "ncpn_swarm_key_text" );
    const auto dev_config = MakeDevConfig( base );

    // Canonical swarm-key text: literal '\n' bytes embedded in the string.
    const std::string swarm_key_text =
        "/key/swarm/psk/1.0.0/\n/base16/" + std::string( VALID_NETWORK_KEY_BASE16 ) + "\n";
    sgns::GeniusNode::WriteNetworkConfig( dev_config.BaseWritePath,
                                          /*port_seed=*/0,
                                          /*auto_dht=*/false,
                                          swarm_key_text,
                                          VALID_PRIVATE_NETWORK_ID,
                                          { BOOTSTRAP_PEER_ONE, BOOTSTRAP_PEER_TWO } );
    sgns::GeniusNode::WriteSgnsConfig( dev_config.BaseWritePath,
                                       /*node_type=*/"Full",
                                       /*is_processor=*/true,
                                       /*rpc_catchup=*/false );

    // The file must contain the ESCAPED two-character sequence backslash+'n' - never a raw
    // newline byte (the writer emits no trailing newline, so the whole file is one line).
    const auto config_text = ReadConfigFile( dev_config.BaseWritePath );
    EXPECT_NE( config_text.find( "/key/swarm/psk/1.0.0/\\n/base16/" ), std::string::npos );
    EXPECT_EQ( std::count( config_text.begin(), config_text.end(), '\n' ), 0 );

    // The reload retains the private-network identity (pre-fix: parse error silently
    // swallowed, node booted public with an EMPTY id - the retained id discriminates).
    auto node = sgns::GeniusNode::New( dev_config, sgns::FromPrivateKey{ TEST_PRIVATE_KEY } );
    ASSERT_NE( node, nullptr );
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );
    EXPECT_EQ( GeniusNodeTestAccess::PrivateNetworkId( node ), VALID_PRIVATE_NETWORK_ID );
    ASSERT_NO_FATAL_FAILURE( WaitForStartupSettled( node ) );
}

// WR-01 regression: a network_config.json that EXISTS but cannot be parsed must fail the node
// load (GeniusNode::New returns nullptr). Pre-fix the parse-error branch returned valid
// settings and the node started PUBLIC - bypassing every identity validation, which only runs
// after a successful parse.
TEST( NetworkConfigPrivateNetwork, CorruptConfigFailsNodeStart )
{
    auto node = NodeFromRawConfig( "ncpn_corrupt", ", \"broken\": [unclosed" );
    EXPECT_EQ( node, nullptr ) << "an existing-but-unparseable network_config.json must fail the load";
}
