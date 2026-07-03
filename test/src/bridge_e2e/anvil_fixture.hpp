/**
 * @file       anvil_fixture.hpp
 * @brief      Reusable local-Anvil helper for bridge E2E tests (Phase 04.1).
 * @date       2026-07-02
 * @author     Super Genius (info@gnus.ai)
 *
 * Header-only helper that manages a local Anvil subprocess forking Sepolia
 * state, plus the Anvil-default-key constants and GNUS funding utilities.
 * No OS preprocessor guards are introduced here beyond the reused
 * OpenCommandPipe/CloseCommandPipe wrapper (mirrors the Phase 4 test-side
 * popen/_popen pattern). Everything lives in namespace @ref sgns::test::anvil.
 */

#ifndef SUPERGENIUS_TEST_BRIDGE_E2E_ANVIL_FIXTURE_HPP
#define SUPERGENIUS_TEST_BRIDGE_E2E_ANVIL_FIXTURE_HPP

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>

#include <spdlog/spdlog.h>

#include "base/util.hpp"

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

    /** @brief Readiness poll budget for the Anvil JSON-RPC endpoint (D-04). */
    inline constexpr unsigned int kAnvilReadyTimeoutMs = 15000u;

    /** @brief Sepolia numeric chain ID as a string (matches MintTokens chainid param type). */
    inline constexpr const char *kSepoliaChainId = "11155111";

    /** @brief Sepolia bridge contract address, lowercase hex with 0x prefix (D-02). */
    inline constexpr const char *kSepoliaBridgeContractLower = "0x9af8050220d8c355ca3c6dc00a78b474cd3e3c70";

    /** @brief Bridge BridgeSourceBurned event topic0 (hex with 0x prefix). */
    inline constexpr const char *kBridgeEventTopic0 = "0xc3d58168c5ae7397731d063d5bbf3d657854427343f4c083240f7aacaa2d0f62";

    /** @brief Default public Sepolia RPC endpoint used as the Anvil --fork-url source (D-03). */
    inline constexpr const char *kSepoliaRpcPublicnode = "https://ethereum-sepolia-rpc.publicnode.com";

    /** @brief Controlled Sepolia GNUS holder used to fund Anvil account #0 via impersonation (D-08/D-09). */
    inline constexpr const char *kGnusHolderSepolia = "0x910bAa33DeB0D614Aa9d80e38b7f0BF87549c2fC";

    /** @brief Poll interval for Anvil readiness checks. */
    inline constexpr unsigned int kAnvilPollIntervalMs = 100u;

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
        char line_buf[1024] = {};
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
                                   " 1000000000000000000 --from " + kGnusHolderSepolia +
                                   " --rpc-url " + anvil_rpc_url + " --json 2>&1";
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
        // Trim whitespace and check for non-zero digit presence.
        bool has_nonzero = false;
        for ( char c : balance_out )
        {
            if ( c >= '1' && c <= '9' )
            {
                has_nonzero = true;
                break;
            }
        }
        if ( !has_nonzero )
        {
            spdlog::warn( "anvil_fixture: account #0 GNUS balance still zero after funding: {}", balance_out );
            return false;
        }
        spdlog::info( "anvil_fixture: account #0 funded, balanceOf={}", balance_out );
        return true;
    }

    // =========================================================================
    // AnvilProcess — subprocess lifecycle + readiness polling (D-01, D-04, D-15)
    // =========================================================================

    /**
     * @brief RAII manager for a local Anvil subprocess forking Sepolia state.
     *
     * Start() forks+execs `anvil --fork-url <fork_url> --port <port> --mnemonic <mnemonic>`.
     * WaitForReady() polls eth_blockNumber via `cast block-number` using
     * sgns::waitForCondition (no sleep_for in test code). Stop() SIGTERMs and
     * reaps the child. The destructor calls Stop() if still started, so a
     * test crash still cleans up the Anvil process.
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
         * @param[in] preferred_port  TCP port for Anvil's JSON-RPC server (default kAnvilStartPort).
         * @return true if fork()+execlp() succeeded; false on fork or exec failure.
         */
        bool Start( const std::string &fork_url, unsigned int preferred_port = kAnvilStartPort )
        {
            if ( started_ )
            {
                spdlog::warn( "anvil_fixture: Start() called on already-started AnvilProcess" );
                return false;
            }
            port_     = preferred_port;
            rpc_url_  = "http://127.0.0.1:" + std::to_string( port_ );
            port_str_ = std::to_string( port_ );

            pid_t pid = fork();
            if ( pid < 0 )
            {
                spdlog::error( "anvil_fixture: fork() failed" );
                return false;
            }
            if ( pid == 0 )
            {
                // Child: redirect stdin/stdout/stderr to /dev/null.
                FILE *devnull = std::fopen( "/dev/null", "r" );
                if ( devnull )
                {
                    std::freopen( "/dev/null", "r", stdin );
                    std::freopen( "/dev/null", "w", stdout );
                    std::freopen( "/dev/null", "w", stderr );
                }
                execlp( "anvil",
                        "anvil",
                        "--fork-url",
                        fork_url.c_str(),
                        "--port",
                        port_str_.c_str(),
                        "--mnemonic",
                        kAnvilMnemonic,
                        static_cast<char *>( nullptr ) );
                // execlp only returns on failure.
                _exit( 127 );
            }
            // Parent
            anvil_pid_ = pid;
            started_   = true;
            spdlog::info( "anvil_fixture: started anvil pid={} port={} fork_url={}", anvil_pid_, port_, fork_url );
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
            std::string     *ready = new std::string();
            bool             result = sgns::waitForCondition(
                [&cmd, ready]()
                {
                    int         exit_code = -1;
                    std::string out       = RunShellCapture( cmd, exit_code );
                    if ( exit_code == 0 && !out.empty() )
                    {
                        *ready = out;
                        return true;
                    }
                    return false;
                },
                timeout,
                nullptr,
                std::chrono::milliseconds( kAnvilPollIntervalMs ) );
            delete ready;
            if ( result )
            {
                spdlog::info( "anvil_fixture: anvil ready at {}", rpc );
            }
            else
            {
                spdlog::warn( "anvil_fixture: anvil did not become ready at {} within {}ms",
                              rpc,
                              static_cast<long long>( timeout.count() ) );
            }
            return result;
        }

        /**
         * @brief Stops the Anvil subprocess via SIGTERM + waitpid (RAII safety net).
         */
        void Stop()
        {
            if ( anvil_pid_ > 0 )
            {
                std::kill( anvil_pid_, SIGTERM );
                int status = 0;
                waitpid( anvil_pid_, &status, 0 );
                spdlog::info( "anvil_fixture: stopped anvil pid={} status={}", anvil_pid_, status );
                anvil_pid_ = -1;
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
        pid_t         anvil_pid_ = -1;   ///< Anvil child PID, or -1 when not running.
        unsigned int  port_      = 0u;   ///< Anvil TCP port.
        std::string   rpc_url_;          ///< "http://127.0.0.1:<port>".
        std::string   port_str_;         ///< String form of port_ (lifetime anchor for execlp).
        bool          started_   = false; ///< Whether Start() has succeeded.
    };

} // namespace sgns::test::anvil

#endif // SUPERGENIUS_TEST_BRIDGE_E2E_ANVIL_FIXTURE_HPP
