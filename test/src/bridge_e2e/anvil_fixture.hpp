/**
 * @file       anvil_fixture.hpp
 * @brief      Reusable local-Anvil helper for bridge E2E tests (Phase 04.1).
 * @date       2026-07-02
 * @author     Super Genius (info@gnus.ai)
 *
 * Header-only helper that manages a local Anvil subprocess forking Sepolia
 * state, plus the Anvil-default-key constants and GNUS funding utilities.
 * Subprocess lifecycle is cross-platform via boost::process (fork/exec on
 * POSIX, CreateProcess on Windows) — the AnvilProcess path uses no OS
 * preprocessor guards. The reused OpenCommandPipe/CloseCommandPipe wrapper
 * (mirrors the Phase 4 test-side popen/_popen pattern) is the only guarded
 * site. Everything lives in namespace @ref sgns::test::anvil.
 */

#ifndef SUPERGENIUS_TEST_BRIDGE_E2E_ANVIL_FIXTURE_HPP
#define SUPERGENIUS_TEST_BRIDGE_E2E_ANVIL_FIXTURE_HPP

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/process.hpp>

#include "base/util.hpp"
#include <base/parse_utility.hpp>         // rlp::base::parse::hex_bytes
#include <eth/abi_decoder.hpp>            // eth::abi::event_signature_hash
#include "account/BridgeEventTypes.hpp"   // sgns::kBridgeOutInitiatedSig (canonical sig string)

namespace sgns::test::anvil
{
    // =========================================================================
    // Public test-fixture constants (D-05, D-07) — inline constexpr per CLAUDE.md
    // =========================================================================

    /** @brief Anvil deterministic default mnemonic (well-known test fixture). */
    inline constexpr const char *kAnvilMnemonic = "test test test test test test test test test test test junk";

    /** @brief Anvil default account #0 address (hex, EIP-55 checksum). */
    inline constexpr const char *kAnvilAccount0Address = "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266";

    /** @brief Anvil default account #0 private key (hex with 0x prefix, public test value). */
    inline constexpr const char *kAnvilAccount0PrivateKey = "0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80";

    /** @brief High port used by the test Anvil instance to avoid default-8545 collisions (D-15). */
    inline constexpr unsigned int kAnvilStartPort = 18545u;

    /**
     * @brief Per-fixture Anvil port bands, kAnvilPortSearchSpan apart so they cannot overlap.
     *
     * FindAvailablePort() binds a probe, closes it, and only then is anvil spawned, so the
     * window between the close and anvil's own bind spans a process spawn plus anvil's fork
     * URL fetch -- seconds. Two fixtures probing the same base both see it free, and the
     * loser's WaitForReady() then talks to the WINNER's anvil (it only checks that the child
     * is running and that some RPC answers), so both test processes transact on one shared
     * chain. Giving each fixture its own band removes the overlap between in-repo fixtures.
     * It does not defend against a foreign process on the same port; for that the probe
     * would have to hold its acceptor until anvil inherits the port.
     */
    inline constexpr unsigned int kAnvilPortBandRace    = kAnvilStartPort;          //!< bridge_race_*
    inline constexpr unsigned int kAnvilPortBandE2E     = kAnvilStartPort + 200u;   //!< bridge_anvil_e2e
    inline constexpr unsigned int kAnvilPortBandCatchup = kAnvilStartPort + 400u;   //!< bridge_anvil_catchup

    /** @brief Number of consecutive ports considered when the preferred Anvil port is occupied. */
    inline constexpr unsigned int kAnvilPortSearchSpan = 100u;

    /** @brief Readiness poll budget for the Anvil JSON-RPC endpoint (D-04). */
    inline constexpr unsigned int kAnvilReadyTimeoutMs = 15000u;

    /** @brief Sepolia numeric chain ID as a string (matches MintTokens chainid param type). */
    inline constexpr const char *kSepoliaChainId = "11155111";

    /** @brief Sepolia bridge contract address, lowercase hex with 0x prefix (D-02). */
    inline constexpr const char *kSepoliaBridgeContractLower = "0x9af8050220d8c355ca3c6dc00a78b474cd3e3c70";

    /**
     * @brief BridgeOutInitiated event topic0 (v2), DERIVED from the canonical signature
     *        via keccak256 — never a hardcoded hash.
     *
     * Computed as eth::abi::event_signature_hash(kBridgeOutInitiatedSig) (the same call the
     * relayer and ChainRpcEndpointProvider use), hex-encoded with the 0x prefix. Both
     * downstream test files build accepted_topic0_hashes from this, so the v2 topic0
     * propagates automatically and cannot drift from the contract/relayer definition.
     *
     * @return 0x-prefixed keccak256 hash hex of the v2 BridgeOutInitiated signature.
     */
    static inline const std::string &BridgeEventTopic0()
    {
        static const std::string kTopic0 = []()
        {
            const auto hash = eth::abi::event_signature_hash( std::string( sgns::kBridgeOutInitiatedSig ) );
            return rlp::base::parse::hex_bytes( hash.data(), hash.size() );
        }();
        return kTopic0;
    }

    /**
     * @brief Destination chain ID used in the bridgeOut() test burn — Ethereum mainnet (1).
     *
     * MUST differ from kSepoliaChainId ("11155111") because GNUSBridge.sol requires
     * destChainID != srcChainID; mainnet is the natural canonical pairing and is
     * never confused with the test's Sepolia fork.
     */
    inline constexpr const char *kDestChainId = "1";

    /** @brief Default public Sepolia RPC endpoint used as the Anvil --fork-url source — archive-capable, no API key (D-03).
     *  drpc dropped Sepolia from its free plan (verified 2026-08-27); publicnode is the working public alternative. */
    inline constexpr const char *kSepoliaRpcPublicnode = "https://ethereum-sepolia-rpc.publicnode.com";

    /** @brief Controlled Sepolia GNUS holder used to fund Anvil account #0 via impersonation (D-08/D-09). */
    inline constexpr const char *kGnusHolderSepolia = "0x910bAa33DeB0D614Aa9d80e38b7f0BF87549c2fC";

    /** @brief Poll interval for Anvil readiness checks. */
    inline constexpr unsigned int kAnvilPollIntervalMs = 100u;

    /** @brief Line-buffer size (bytes) for RunShellCapture's fgets loop. */
    inline constexpr unsigned int kShellLineBufferSize = 1024u;

    /** @brief Maximum Anvil stderr bytes included in a startup-failure diagnostic. */
    inline constexpr size_t kAnvilErrorTailBytes = 4096u;

    /** @brief 1 GNUS expressed in base units (1e18) — funding amount passed to `cast send`. */
    inline constexpr uint64_t kOneGnusInBaseUnits = 1000000000000000000ull;

    // =========================================================================
    // Reused Phase 4 popen/_popen wrapper — copied verbatim as static inline.
    // The original lives in an anonymous namespace in bridge_e2e_test.cpp; the
    // established test-side pattern permits _WIN32 only inside this wrapper.
    // =========================================================================

    /**
     * @brief Opens a command pipe (popen on POSIX, _popen on Windows).
     * @param[in] command  Shell command to execute.
     * @param[in] mode     Pipe mode (typically "r").
     * @return FILE* handle, or nullptr on failure.
     */
    static inline FILE *OpenCommandPipe( const char *command, const char *mode )
    {
#if defined( _WIN32 )
        return _popen( command, mode );
#else
        return popen( command, mode );
#endif
    }

    /**
     * @brief Closes a command pipe opened with OpenCommandPipe.
     * @param[in] pipe  Pipe handle to close.
     * @return Termination status of the command.
     */
    static inline int CloseCommandPipe( FILE *pipe )
    {
#if defined( _WIN32 )
        return _pclose( pipe );
#else
        return pclose( pipe );
#endif
    }

    /**
     * @brief Captures all stdout+stderr from a shell command.
     * @param[in] command  Shell command to execute (caller is responsible for quoting).
     * @param[out] exit_code  Process exit code (valid even on empty output).
     * @return Captured output string (may be empty on failure).
     */
    static inline std::string RunShellCapture( const std::string &command, int &exit_code )
    {
        std::string output;
        FILE       *pipe = OpenCommandPipe( command.c_str(), "r" );
        if ( !pipe )
        {
            exit_code = -1;
            return output;
        }
        char line_buf[kShellLineBufferSize] = {};
        while ( std::fgets( line_buf, sizeof( line_buf ), pipe ) )
        {
            output += line_buf;
        }
        exit_code = CloseCommandPipe( pipe );
        return output;
    }

    // =========================================================================
    // Free helper functions (D-03, D-08, D-09, D-16)
    // =========================================================================

    /**
     * @brief Returns true when the `cast` binary is available on PATH.
     * @return true if `which cast` (or `where cast`) finds the binary.
     */
    static inline bool CastAvailable()
    {
#if defined( _WIN32 )
        constexpr const char *kCmd = "where cast 2>NUL";
#else
        constexpr const char *kCmd = "which cast 2>/dev/null";
#endif
        int         exit_code = -1;
        std::string out       = RunShellCapture( kCmd, exit_code );
        return !out.empty();
    }

    /**
     * @brief Returns true when the `anvil` binary is available on PATH.
     * @return true if `which anvil` (or `where anvil`) finds the binary.
     */
    static inline bool AnvilAvailable()
    {
#if defined( _WIN32 )
        constexpr const char *kCmd = "where anvil 2>NUL";
#else
        constexpr const char *kCmd = "which anvil 2>/dev/null";
#endif
        int         exit_code = -1;
        std::string out       = RunShellCapture( kCmd, exit_code );
        return !out.empty();
    }

    /**
     * @brief Resolves the Sepolia RPC URL for the Anvil --fork-url argument (D-03).
     * @return Value of RPC_SEPOLIA env var if set, else kSepoliaRpcPublicnode.
     */
    static inline std::string SepoliaForkUrl()
    {
        const char *env = std::getenv( "RPC_SEPOLIA" );
        if ( env && env[0] != '\0' )
        {
            return std::string( env );
        }
        return std::string( kSepoliaRpcPublicnode );
    }

    /**
     * @brief Extracts the transactionHash value from `cast send --json` output.
     *
     * Reuses the Phase 4 lookup pattern against the literal
     * `"transactionHash":"0x` marker. No JSON parser dependency.
     *
     * @param[in] cast_output  Raw `cast send --json` stdout.
     * @return Parsed 0x-prefixed transaction hash, or empty string if not found.
     */
    static inline std::string ParseTxHashFromCastJson( const std::string &cast_output )
    {
        constexpr const char *kTxHashPattern = "\"transactionHash\":\"0x";
        size_t                hash_pos       = cast_output.find( kTxHashPattern );
        if ( hash_pos == std::string::npos )
        {
            return {};
        }
        size_t start = hash_pos + std::strlen( kTxHashPattern ) - 2; // include "0x"
        size_t end   = cast_output.find( '"', start );
        if ( end == std::string::npos )
        {
            return {};
        }
        return cast_output.substr( start, end - start );
    }

    /**
     * @brief Derives the bridgeOut() destination args from a node's 128-char SG address.
     *
     * This is the inverse of the relayer's v2 decompression contract
     * (evmrelay/src/eth/secp256k1_utility.cpp::DecompressXOnlyPubkey). The relayer
     * consumes the bytes32 sgnsDestination DIRECTLY as contract_x_bytes (LSB-first
     * in hex), reversing it internally to big-endian before secp256k1 decompression.
     * node->GetAddress() returns the bare 128-char hex X||Y where both halves are
     * already LSB-first contract byte order. Therefore the bytes32 passed to
     * bridgeOut must equal node X in contract (LSB-first) byte order — which is the
     * first 64 chars of GetAddress() UNCHANGED, with a 0x prefix and NO reversal.
     *
     * destinationYOdd is the parity of the Y half. In LSB-first/contract order the
     * FIRST byte of the Y half is its LSB, so its low bit equals Y mod 2 = true
     * parity. secp256k1_utility.cpp:190 maps false->0x02 (even Y), true->0x03 (odd Y).
     *
     * @param[in] sgns_address_128  Bare 128-char hex X||Y returned by node->GetAddress().
     * @return { "0x" + X_half_64chars, destination_y_odd }, or { "", false } on invalid input.
     */
    static inline std::pair<std::string, bool>
    BridgeDestinationFromSgnsAddress( const std::string &sgns_address_128 ) noexcept
    {
        constexpr unsigned int kSgnsAddressHexLen = 128u;
        constexpr unsigned int kHalfLen           = 64u;
        constexpr unsigned int kByteHexChars      = 2u;

        if ( sgns_address_128.size() != kSgnsAddressHexLen )
        {
            return { "", false };
        }
        for ( char c : sgns_address_128 )
        {
            if ( !std::isxdigit( static_cast<unsigned char>( c ) ) )
            {
                return { "", false };
            }
        }
        const std::string x_half         = sgns_address_128.substr( 0, kHalfLen );
        const std::string y_half         = sgns_address_128.substr( kHalfLen, kHalfLen );
        const std::string y_first_byte_hex = y_half.substr( 0, kByteHexChars );

        unsigned int y_first_byte = 0u;
        try
        {
            y_first_byte = static_cast<unsigned int>( std::stoul( y_first_byte_hex, nullptr, 16 ) );
        }
        catch ( ... )
        {
            return { "", false };
        }
        const bool destination_y_odd = ( y_first_byte & 1u ) != 0u;
        return { "0x" + x_half, destination_y_odd };
    }

    /**
     * @brief Validates an Anvil RPC URL against the strict shape used by this fixture.
     *
     * The URL is interpolated into a `cast ... --rpc-url <url> ...` shell command
     * (RunShellCapture invokes /bin/sh -c). Reject any URL containing characters
     * outside the http(s)://[host]:[port] shape so a caller-supplied URL with shell
     * metacharacters (`;`, backtick, `$(...)`, quotes) cannot inject commands. The
     * fixture constructs URLs only as `http://127.0.0.1:<port>`, so the allow-list
     * (letters, digits, `.`, `:`, `/`, `_`, `-`) covers every legitimate value.
     *
     * @param[in] url  Caller-supplied RPC URL to validate.
     * @return true if @p url starts with `http://` or `https://` and contains only
     *         allow-listed characters afterward; false otherwise.
     */
    static inline bool IsValidAnvilRpcUrl( const std::string &url )
    {
        constexpr const char *kHttpScheme  = "http://";
        constexpr const char *kHttpsScheme = "https://";
        const size_t          kHttpLen     = std::strlen( kHttpScheme );
        const size_t          kHttpsLen    = std::strlen( kHttpsScheme );
        bool                   has_scheme = false;
        size_t                 scheme_len  = 0u;
        if ( url.compare( 0u, kHttpLen, kHttpScheme ) == 0 )
        {
            has_scheme  = true;
            scheme_len  = kHttpLen;
        }
        else if ( url.compare( 0u, kHttpsLen, kHttpsScheme ) == 0 )
        {
            has_scheme  = true;
            scheme_len  = kHttpsLen;
        }
        if ( !has_scheme )
        {
            return false;
        }
        for ( size_t i = scheme_len; i < url.size(); ++i )
        {
            const unsigned char c = static_cast<unsigned char>( url[i] );
            const bool          ok = ( c >= '0' && c <= '9' ) ||
                            ( c >= 'a' && c <= 'z' ) ||
                            ( c >= 'A' && c <= 'Z' ) ||
                            c == '.' || c == ':' || c == '/' || c == '_' || c == '-';
            if ( !ok )
            {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Sends a real GNUS bridgeOut() burn on the local Anvil fork.
     *
     * Replaces the prior safeTransferFrom(self,self,0,amount,0x) burn-seeding, which
     * was NOT a burn on the hybrid ERC-20/ERC-1155 GNUS contract and reverted at gas
     * estimate. This helper exercises the production burn path consumed by
     * BridgeRelayer's v2 parsing branch (eth::DecompressXOnlyPubkey). Uses --unlocked
     * + --from account #0 to match the verified-correct FundAccount0WithGnus funding
     * pattern (Anvil default mnemonic unlocks account #0; --private-key is not needed).
     *
     * @param[in] anvil_rpc_url       HTTP RPC URL of the local Anvil instance.
     * @param[in] amount              Burn amount in base units.
     * @param[in] sgns_destination_128  Bare 128-char hex X||Y from node->GetAddress().
     * @return Parsed 0x-prefixed burn tx hash, or empty string on failure.
     */
    static inline std::string SendBridgeOutBurn( const std::string &anvil_rpc_url,
                                                 uint64_t           amount,
                                                 const std::string &sgns_destination_128 )
    {
        constexpr unsigned int kGnusTokenId = 0u;

        // Defensive: anvil_rpc_url is interpolated into a shell command. Reject
        // anything outside the strict http(s)://[host]:[port] shape so a future
        // caller passing a URL with shell metacharacters cannot inject commands.
        if ( !IsValidAnvilRpcUrl( anvil_rpc_url ) )
        {
            spdlog::error( "SendBridgeOutBurn: rejected anvil_rpc_url with disallowed characters: {}", anvil_rpc_url );
            return {};
        }

        const auto dest = BridgeDestinationFromSgnsAddress( sgns_destination_128 );
        if ( dest.first.empty() )
        {
            spdlog::error( "SendBridgeOutBurn: invalid sgns destination address (len={})",
                           sgns_destination_128.size() );
            return {};
        }
        const std::string dest_bytes32 = dest.first;
        const std::string dest_y_odd   = dest.second ? "true" : "false";

        const std::string cast_cmd =
            std::string( "cast send " ) + kSepoliaBridgeContractLower +
            " \"bridgeOut(uint256,uint256,uint256,bytes32,bool)\" " + std::to_string( amount ) + " " +
            std::to_string( kGnusTokenId ) + " " + kDestChainId + " " + dest_bytes32 + " " + dest_y_odd +
            " --unlocked --from " + kAnvilAccount0Address + " --rpc-url " + anvil_rpc_url + " --json 2>&1";

        int         rc     = -1;
        std::string output = RunShellCapture( cast_cmd, rc );
        if ( rc != 0 )
        {
            spdlog::error( "SendBridgeOutBurn: cast send failed rc={} output={}", rc, output );
            return {};
        }
        const std::string tx_hash = ParseTxHashFromCastJson( output );
        if ( tx_hash.empty() )
        {
            spdlog::error( "SendBridgeOutBurn: could not parse transactionHash from cast output: {}", output );
            return {};
        }
        spdlog::info( "SendBridgeOutBurn: burn tx hash = {}", tx_hash );
        return tx_hash;
    }

    /**
     * @brief Funds Anvil account #0 with 1 GNUS (1e18 wei) via anvil_impersonateAccount (D-08/D-09).
     *
     * Impersonates a known Sepolia GNUS holder, sends an ERC-20 transfer(address,uint256)
     * of 1 GNUS to kAnvilAccount0Address on the local Anvil fork, then stops
     * impersonating and verifies the resulting balance is non-zero. Impersonation
     * moves no real funds on Sepolia — it is a local-fork-only operation.
     *
     * @param[in] anvil_rpc_url  HTTP RPC URL of the local Anvil instance.
     * @return true if funding succeeded and balanceOf returns non-zero.
     */
    static inline bool FundAccount0WithGnus( const std::string &anvil_rpc_url )
    {
        // Defensive: anvil_rpc_url is interpolated into shell commands below.
        // Reject anything outside the strict http(s)://[host]:[port] shape so a
        // future caller passing a URL with shell metacharacters cannot inject
        // commands (WR-03 — same guard as SendBridgeOutBurn).
        if ( !IsValidAnvilRpcUrl( anvil_rpc_url ) )
        {
            spdlog::error( "FundAccount0WithGnus: rejected anvil_rpc_url with disallowed characters: {}",
                           anvil_rpc_url );
            return false;
        }

        // Step 1: impersonate the Sepolia GNUS holder.
        std::string impersonate_cmd = std::string( "cast rpc anvil_impersonateAccount " ) + kGnusHolderSepolia +
                                      " --rpc-url " + anvil_rpc_url + " 2>&1";
        int impersonate_rc = -1;
        RunShellCapture( impersonate_cmd, impersonate_rc );
        if ( impersonate_rc != 0 )
        {
            spdlog::warn( "anvil_fixture: anvil_impersonateAccount failed (rc={})", impersonate_rc );
            return false;
        }

        // Step 2: ERC-20 transfer(address,uint256) of 1 GNUS (1e18) to account #0.
        std::string transfer_cmd = std::string( "cast send " ) + kSepoliaBridgeContractLower +
                                   " \"transfer(address,uint256)\" " + kAnvilAccount0Address +
                                   " " + std::to_string( kOneGnusInBaseUnits ) + " --from " + kGnusHolderSepolia +
                                   " --unlocked --rpc-url " + anvil_rpc_url + " --json 2>&1";
        int transfer_rc = -1;
        RunShellCapture( transfer_cmd, transfer_rc );
        if ( transfer_rc != 0 )
        {
            spdlog::warn( "anvil_fixture: GNUS transfer failed (rc={})", transfer_rc );
            // Still attempt to stop impersonating for cleanliness.
            std::string stop_cmd = std::string( "cast rpc anvil_stopImpersonatingAccount " ) + kGnusHolderSepolia +
                                   " --rpc-url " + anvil_rpc_url + " 2>&1";
            int stop_rc = -1;
            RunShellCapture( stop_cmd, stop_rc );
            return false;
        }

        // Step 3: stop impersonating the holder.
        std::string stop_cmd = std::string( "cast rpc anvil_stopImpersonatingAccount " ) + kGnusHolderSepolia +
                               " --rpc-url " + anvil_rpc_url + " 2>&1";
        int stop_rc = -1;
        RunShellCapture( stop_cmd, stop_rc );

        // Step 4: verify non-zero GNUS balance on account #0.
        std::string balance_cmd = std::string( "cast call " ) + kSepoliaBridgeContractLower +
                                  " \"balanceOf(address)(uint256)\" " + kAnvilAccount0Address +
                                  " --rpc-url " + anvil_rpc_url + " 2>&1";
        int          balance_rc = -1;
        std::string  balance_out = RunShellCapture( balance_cmd, balance_rc );
        if ( balance_rc != 0 )
        {
            spdlog::warn( "anvil_fixture: balanceOf call failed (rc={})", balance_rc );
            return false;
        }
        // Non-empty numeric output (and not "0") means funding worked.
        if ( balance_out.empty() )
        {
            spdlog::warn( "anvil_fixture: balanceOf returned empty output" );
            return false;
        }
        // Parse the output as an unsigned integer. cast call ... (uint256) returns
        // a plain decimal integer (possibly with surrounding whitespace). A byte-
        // scan for '1'..'9' would accept error text containing a stray digit (e.g.
        // "Error: ... code -32000") as a valid non-zero balance; std::stoull
        // rejects non-numeric input instead.
        std::string trimmed = balance_out;
        while ( !trimmed.empty() && std::isspace( static_cast<unsigned char>( trimmed.front() ) ) )
        {
            trimmed.erase( trimmed.begin() );
        }
        while ( !trimmed.empty() && std::isspace( static_cast<unsigned char>( trimmed.back() ) ) )
        {
            trimmed.pop_back();
        }
        // Strip cast call hex suffix: "2000000000000000000 [2e18]" → "2000000000000000000"
        auto bracket_pos = trimmed.find( '[' );
        if ( bracket_pos != std::string::npos )
        {
            trimmed.resize( bracket_pos );
            // Clean up space before the bracket
            while ( !trimmed.empty() && std::isspace( static_cast<unsigned char>( trimmed.back() ) ) )
            {
                trimmed.pop_back();
            }
        }
        uint64_t balance = 0u;
        bool     parsed  = false;
        try
        {
            size_t consumed = 0u;
            balance = static_cast<uint64_t>( std::stoull( trimmed, &consumed, 10 ) );
            parsed = ( consumed == trimmed.size() );
        }
        catch ( ... )
        {
            parsed = false;
        }
        if ( !parsed )
        {
            spdlog::warn( "anvil_fixture: balanceOf returned non-numeric output: {}", trimmed );
            return false;
        }
        if ( balance == 0u )
        {
            spdlog::warn( "anvil_fixture: account #0 GNUS balance still zero after funding: {}", trimmed );
            return false;
        }
        spdlog::info( "anvil_fixture: account #0 funded, balanceOf={}", trimmed );
        return true;
    }

    // =========================================================================
    // AnvilProcess — subprocess lifecycle + readiness polling (D-01, D-04, D-15)
    // =========================================================================

    /** @brief Alias for boost::process — cross-platform subprocess API (POSIX fork/exec, Windows CreateProcess). */
    namespace bp = boost::process;

    /**
     * @brief RAII manager for a local Anvil subprocess forking Sepolia state.
     *
     * Start() spawns `anvil --fork-url <fork_url> --port <port> --mnemonic <mnemonic>`
     * via boost::process (fork/exec on POSIX, CreateProcess on Windows) with
     * stdin/stdout/stderr redirected to null. WaitForReady() polls eth_blockNumber
     * via `cast block-number` using sgns::waitForCondition (no sleep_for in test
     * code). Stop() force-terminates and reaps the child (SIGKILL /
     * TerminateProcess) within a bounded grace window so a misbehaving Anvil
     * cannot hang teardown. The destructor calls Stop() if still started.
     */
    class AnvilProcess
    {
    public:
        /**
         * @brief Constructs an unstarted AnvilProcess.
         */
        AnvilProcess() = default;

        /** @brief AnvilProcess is move-only (owns a subprocess). */
        AnvilProcess( const AnvilProcess & )            = delete;
        AnvilProcess &operator=( const AnvilProcess & ) = delete;
        AnvilProcess( AnvilProcess && )                 = default;
        AnvilProcess &operator=( AnvilProcess && )      = default;

        /**
         * @brief Destructor — ensures the Anvil subprocess is terminated.
         */
        ~AnvilProcess()
        {
            if ( started_ )
            {
                Stop();
            }
        }

        /**
         * @brief Starts the Anvil subprocess forking Sepolia state (D-01).
         * @param[in] fork_url       Sepolia RPC URL passed to `--fork-url`.
         * @param[in] preferred_port  First TCP port considered for Anvil's JSON-RPC server
         *                            (default kAnvilStartPort).
         * @return true if the Anvil child spawned successfully; false if `anvil` is not on PATH or spawn failed.
         */
        bool Start( const std::string &fork_url, unsigned int preferred_port = kAnvilStartPort )
        {
            if ( started_ )
            {
                spdlog::warn( "anvil_fixture: Start() called on already-started AnvilProcess" );
                return false;
            }

            port_ = FindAvailablePort( preferred_port );
            if ( port_ == 0u )
            {
                spdlog::error( "anvil_fixture: no available TCP port in range {}-{}",
                               preferred_port,
                               preferred_port + kAnvilPortSearchSpan - 1u );
                return false;
            }
            if ( port_ != preferred_port )
            {
                spdlog::warn( "anvil_fixture: preferred port {} is occupied; using {}", preferred_port, port_ );
            }

            rpc_url_  = "http://127.0.0.1:" + std::to_string( port_ );
            port_str_ = std::to_string( port_ );
            anvil_stderr_path_ =
                ( std::filesystem::temp_directory_path() /
                  ( "supergenius-anvil-" + port_str_ + ".stderr.log" ) ).string();
            std::error_code remove_ec;
            std::filesystem::remove( anvil_stderr_path_, remove_ec );

            // boost::process resolves `anvil` via PATH (POSIX) / %PATH% (Windows),
            // spawns it cross-platform, redirects the child's stdin/stdout to null,
            // and captures stderr for startup diagnostics. search_path returns an
            // empty path when the binary is missing,
            // and the bp::child constructor then throws system_error — caught here
            // and reported, unlike the old execlp() path which silently _exit(127)'d.
            try
            {
                anvil_child_ = std::make_unique<bp::child>(
                    bp::search_path( "anvil" ),
                    "--fork-url",
                    fork_url,
                    "--port",
                    port_str_,
                    "--mnemonic",
                    kAnvilMnemonic,
                    bp::std_in < bp::null,
                    bp::std_out > bp::null,
                    bp::std_err > anvil_stderr_path_ );
            }
            catch ( const std::system_error &e )
            {
                spdlog::error( "anvil_fixture: failed to spawn anvil on PATH: {}", e.what() );
                std::error_code cleanup_ec;
                std::filesystem::remove( anvil_stderr_path_, cleanup_ec );
                anvil_stderr_path_.clear();
                return false;
            }
            started_ = true;
            spdlog::info( "anvil_fixture: started anvil port={} fork_url={}", port_, fork_url );
            return true;
        }

        /**
         * @brief Polls Anvil readiness via `cast block-number` (D-04).
         *
         * Uses sgns::waitForCondition with a kAnvilPollIntervalMs cadence. No
         * sleep_for in test code paths — the polling primitive handles cadence.
         *
         * @param[in] timeout  Maximum time to wait for readiness.
         * @return true if `cast block-number` returns a non-empty value within @p timeout.
         */
        bool WaitForReady( std::chrono::milliseconds timeout = std::chrono::milliseconds( kAnvilReadyTimeoutMs ) )
        {
            if ( !started_ )
            {
                return false;
            }
            std::string     rpc    = rpc_url_;
            std::string     cmd    = "cast block-number --rpc-url " + rpc + " 2>/dev/null";
            // Stack-allocated capture: the value is never read after the loop, so a
            // heap new/delete pair is gratuitous and leaks if waitForCondition (or
            // the lambda) ever throws.
            std::string     ready;
            bool            result = waitForCondition(
                [this, &cmd, &ready]()
                {
                    std::error_code child_ec;
                    if ( !anvil_child_ || !anvil_child_->running( child_ec ) || child_ec )
                    {
                        return false;
                    }

                    int         exit_code = -1;
                    std::string out       = RunShellCapture( cmd, exit_code );
                    if ( exit_code == 0 && !out.empty() )
                    {
                        if ( !anvil_child_->running( child_ec ) || child_ec )
                        {
                            return false;
                        }
                        ready = std::move( out );
                        return true;
                    }
                    return false;
                },
                timeout,
                nullptr,
                std::chrono::milliseconds( kAnvilPollIntervalMs ) );
            if ( result )
            {
                spdlog::info( "anvil_fixture: anvil ready at {}", rpc );
            }
            else
            {
                spdlog::warn( "anvil_fixture: anvil did not become ready at {} within {}ms",
                              rpc,
                              static_cast<long long>( timeout.count() ) );
                const std::string stderr_tail = ReadFileTail( anvil_stderr_path_, kAnvilErrorTailBytes );
                if ( !stderr_tail.empty() )
                {
                    spdlog::error( "anvil_fixture: anvil stderr:\n{}", stderr_tail );
                }
            }
            return result;
        }

        /**
         * @brief Stops the Anvil subprocess via forceful termination + reap (RAII safety net).
         *
         * boost::process::child::terminate() sends SIGKILL on POSIX / TerminateProcess
         * on Windows — uncatchable, so the child is guaranteed to exit and the subsequent
         * blocking wait() reaps it without being able to hang. All overloads take
         * std::error_code so Stop() cannot throw out of the destructor.
         */
        void Stop()
        {
            if ( anvil_child_ )
            {
                std::error_code ec;
                anvil_child_->terminate( ec ); // SIGKILL / TerminateProcess
                anvil_child_->wait( ec );      // reap — bounded, SIGKILL is fatal
                spdlog::info( "anvil_fixture: stopped anvil exit_code={}", anvil_child_->exit_code() );
                anvil_child_.reset();
            }
            if ( !anvil_stderr_path_.empty() )
            {
                std::error_code remove_ec;
                std::filesystem::remove( anvil_stderr_path_, remove_ec );
                anvil_stderr_path_.clear();
            }
            started_ = false;
        }

        /** @brief HTTP RPC URL of the local Anvil instance (empty before Start()). */
        const std::string &RpcUrl() const
        {
            return rpc_url_;
        }

        /** @brief TCP port Anvil is listening on (0 before Start()). */
        unsigned int Port() const
        {
            return port_;
        }

        /** @brief True if Start() succeeded and Stop() has not yet been called. */
        bool IsStarted() const
        {
            return started_;
        }

    private:
        static std::string ReadFileTail( const std::string &path, size_t max_bytes )
        {
            std::ifstream input( path, std::ios::binary );
            if ( !input )
            {
                return {};
            }

            input.seekg( 0, std::ios::end );
            const auto end = static_cast<std::streamoff>( input.tellg() );
            if ( end <= 0 )
            {
                return {};
            }
            const auto bytes  = static_cast<std::streamoff>( max_bytes );
            const auto offset = std::max( std::streamoff{ 0 }, end - bytes );
            input.seekg( offset, std::ios::beg );
            return std::string( std::istreambuf_iterator<char>( input ),
                                std::istreambuf_iterator<char>() );
        }

        /**
         * @brief Finds a bindable TCP port without reusing an existing listener.
         *
         * Checking the port before spawning prevents WaitForReady() from accepting a
         * stale Anvil process left on the fixture's preferred port. The child-liveness
         * checks in WaitForReady() cover the remaining bind/spawn race.
         */
        static unsigned int FindAvailablePort( unsigned int preferred_port )
        {
            static constexpr unsigned int kMaxTcpPort = 65535u;

            for ( unsigned int offset = 0u; offset < kAnvilPortSearchSpan; ++offset )
            {
                if ( preferred_port > kMaxTcpPort - offset )
                {
                    break;
                }

                const unsigned int candidate = preferred_port + offset;
                boost::asio::io_context io_context;
                boost::asio::ip::tcp::acceptor acceptor( io_context );
                boost::system::error_code ec;

                acceptor.open( boost::asio::ip::tcp::v4(), ec );
                if ( ec )
                {
                    continue;
                }
                acceptor.bind( { boost::asio::ip::tcp::v4(), static_cast<unsigned short>( candidate ) }, ec );
                if ( !ec )
                {
                    acceptor.close( ec );
                    return candidate;
                }
            }
            return 0u;
        }

        std::unique_ptr<bp::child> anvil_child_;        ///< boost::process child handle, null when not running.
        unsigned int               port_      = 0u;      ///< Anvil TCP port.
        std::string                rpc_url_;             ///< "http://127.0.0.1:<port>".
        std::string                port_str_;            ///< String form of port_ (passed to bp::child args).
        std::string                anvil_stderr_path_;   ///< Temporary child stderr capture for diagnostics.
        bool                       started_   = false;   ///< Whether Start() has succeeded.
    };

} // namespace sgns::test::anvil

#endif // SUPERGENIUS_TEST_BRIDGE_E2E_ANVIL_FIXTURE_HPP
