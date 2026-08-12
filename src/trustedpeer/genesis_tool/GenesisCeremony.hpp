/**
 * @file GenesisCeremony.hpp
 * @brief One-shot, review-gated trusted-peer genesis ceremony.
 */
#ifndef SGNS_TRUSTEDPEER_GENESIS_TOOL_GENESIS_CEREMONY_HPP
#define SGNS_TRUSTEDPEER_GENESIS_TOOL_GENESIS_CEREMONY_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "outcome/outcome.hpp"
#include "securecrdt/SecureCrdtCandidate.hpp"
#include "trustedpeer/GenesisManifest.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

namespace sgns::trustedpeer
{
    /**
     * @brief Validates, reviews, submits, confirms, and cleans up an ephemeral
     * bootstrap key without involving account storage.
     */
    class GenesisCeremony
    {
    public:
        enum class Error : uint8_t
        {
            SUCCESS = 0,
            INVALID_MANIFEST,
            INVALID_KEY_SOURCE,
            KEY_FILE_IO,
            KEY_FILE_NOT_REGULAR,
            KEY_FILE_SYMLINK,
            KEY_FILE_OWNER,
            KEY_FILE_MODE,
            INVALID_PRIVATE_KEY,
            BOOTSTRAPPER_MISMATCH,
            CONFIRMATION_MISMATCH,
            NETWORK_START_FAILED,
            SUBMISSION_FAILED,
            CONFIRMATION_FAILED,
            CONFIRMATION_TIMEOUT,
            KEY_FILE_UNLINK_FAILED,
        };

        struct KeyFileStatus
        {
            bool     exists = false;
            bool     regular = false;
            bool     symlink = false;
            bool     owner = false;
            uint32_t mode = 0;
        };

        struct Signer
        {
            std::string address;
            TrustedPeerRegistry::SignCallback sign;
        };

        struct Hooks
        {
            std::function<outcome::result<KeyFileStatus>( const std::string & )> inspect_key_file;
            std::function<outcome::result<std::string>( const std::string & )> read_key_file;
            std::function<outcome::result<Signer>( std::string_view )> create_signer;
            std::function<void( void *, size_t )> cleanse;
            std::function<int( const std::string & )> unlink_file;
            std::function<void( std::chrono::milliseconds )> sleep;
        };

        struct Network
        {
            std::function<outcome::result<void>()> start;
            std::function<outcome::result<sgns::securecrdt::CandidateId>(
                const GenesisManifest &,
                const std::vector<uint8_t> &,
                const std::string &,
                TrustedPeerRegistry::SignCallback )>
                submit;
            std::function<outcome::result<std::optional<ConfirmedTrustSnapshot>>()> confirmed;
        };

        struct Request
        {
            GenesisManifest manifest;
            std::optional<std::string> key_file;
            bool key_stdin = false;
            std::chrono::milliseconds confirmation_timeout{ std::chrono::seconds( 30 ) };
            std::chrono::milliseconds poll_interval{ std::chrono::milliseconds( 100 ) };
        };

        GenesisCeremony();
        explicit GenesisCeremony( Hooks hooks );

        static Hooks DefaultHooks();

        outcome::result<void> Run( const Request &request,
                                   Network &network,
                                   std::istream &input,
                                   std::ostream &output,
                                   std::ostream &errors ) const;

    private:
        Hooks hooks_;
    };
} // namespace sgns::trustedpeer

OUTCOME_HPP_DECLARE_ERROR_2( sgns::trustedpeer, GenesisCeremony::Error );

#endif // SGNS_TRUSTEDPEER_GENESIS_TOOL_GENESIS_CEREMONY_HPP
