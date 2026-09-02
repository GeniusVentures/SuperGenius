/**
 * @file TrustStartupController.hpp
 * @brief Durable trust bootstrap and restart state machine for GeniusNode.
 */
#ifndef SGNS_ACCOUNT_TRUST_STARTUP_CONTROLLER_HPP
#define SGNS_ACCOUNT_TRUST_STARTUP_CONTROLLER_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "outcome/outcome.hpp"
#include "trustedpeer/GenesisManifest.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

namespace sgns::account
{
    class BurnConfig;

    class TrustStartupController
    {
    public:
        enum class State : uint8_t
        {
            FreshWaitingForGenesis = 0,
            WaitingForInitialBurn,
            ConfirmedReady,
            FatalMismatch,
        };

        enum class EventCode : uint8_t
        {
            TRUST_CONFIG_CONFLICT = 0,
            TRUST_NETWORK_MISMATCH,
            TRUST_LOCAL_STATE_CORRUPT,
            TRUST_CRDT_MISSING,
            TRUST_CRDT_ROLLBACK,
            TRUST_CRDT_FORK,
            TRUST_ACTIVATION_FAILED,
            TRUST_REFRESH_RETRY_SCHEDULED,
            TRUST_REFRESH_RETRY_EXHAUSTED,
        };

        enum class RefreshStage : uint8_t
        {
            DurableState = 0,
            GenesisDiscovery,
            PolicyDiscovery,
            PolicyActivation,
            BurnDiscovery,
            BurnActivation,
            Publication,
        };

        enum class RetryDisposition : uint8_t
        {
            Success = 0,
            Transient,
            Actionable,
            Fatal,
        };

        [[nodiscard]] static const char *EventCodeName( EventCode code ) noexcept;

        struct Event
        {
            EventCode                code;
            std::vector<std::string> fields;
            std::string              persisted_fingerprint;
        };

        using EventCallback = std::function<void( const Event & )>;
        using StateCallback = std::function<void( State )>;

        /**
         * Narrow deterministic seam for refresh-dispatch regressions. Production
         * callers leave this empty and use the real registry methods/timers.
         */
        struct RefreshTestHooks
        {
            using CandidateList = outcome::result<std::vector<sgns::securecrdt::CandidateId>>;

            std::function<CandidateList( sgns::trustedpeer::TrustedPeerRegistry & )> list_policy_candidates;
            std::function<CandidateList( BurnConfig & )>                            list_burn_candidates;
            std::function<void( uint32_t )>                                         observe_attempt;
            std::function<void( std::chrono::milliseconds, std::function<void()> )> schedule_retry;
            std::function<void()>                                                   observe_dispatch_idle;
            std::function<void()>                                                   observe_coalesced_request;
            std::function<void( std::function<void()> )>                            bind_request_refresh;
        };

        static outcome::result<std::shared_ptr<TrustStartupController>> New(
            std::shared_ptr<sgns::securecrdt::SecureCrdt>        secure_crdt,
            std::shared_ptr<sgns::trustedpeer::TrustStateStore>  trust_store,
            std::optional<sgns::trustedpeer::GenesisManifest>    diagnostic_manifest,
            std::string                                          local_signer_address,
            sgns::trustedpeer::TrustedPeerRegistry::SignCallback sign_callback,
            EventCallback                                        event_callback = {},
            StateCallback                                        state_callback = {},
            std::shared_ptr<RefreshTestHooks>                     refresh_test_hooks = {} );

        ~TrustStartupController();

        outcome::result<void> Refresh();
        outcome::result<void> ObserveReplicatedSnapshot(
            const std::optional<sgns::trustedpeer::ConfirmedTrustSnapshot> &replicated );

        [[nodiscard]] State                    GetState() const noexcept;
        [[nodiscard]] bool                     CanApproveSuccessors() const noexcept;
        [[nodiscard]] bool                     IsEconomicallyReady() const noexcept;
        [[nodiscard]] std::vector<std::string> GetCurrentPeers() const;

        [[nodiscard]] std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> registry() const;
        [[nodiscard]] std::shared_ptr<BurnConfig>                             burn_config() const;

    private:
        TrustStartupController() = default;
        struct RefreshDispatchState;

        void SetState( State state );
        void Emit( EventCode code, std::vector<std::string> fields = {} ) const;
        void RequestRefresh();
        void QueuePendingCandidate( const sgns::securecrdt::CandidateId &candidate );
        void MarkCandidateFailed( const sgns::securecrdt::CandidateId &candidate );
        outcome::result<void> RefreshClassified( RefreshStage &stage );
        static RetryDisposition ClassifyRefreshResult( RefreshStage                 stage,
                                                       const outcome::result<void> &result );
        static void RequestDispatch( const std::shared_ptr<RefreshDispatchState> &dispatch );
        static void RunDispatchAttempt( const std::shared_ptr<RefreshDispatchState> &dispatch,
                                        uint32_t                                     attempt );
        static void FinishDispatch( const std::shared_ptr<RefreshDispatchState> &dispatch );

        std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt_;
        std::shared_ptr<sgns::trustedpeer::TrustStateStore>     trust_store_;
        std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> registry_;
        std::shared_ptr<BurnConfig>                             burn_config_;
        std::shared_ptr<RefreshTestHooks>                       refresh_test_hooks_;
        sgns::trustedpeer::GenesisManifest                      manifest_;
        std::string                                             local_signer_address_;
        EventCallback                                           event_callback_;
        StateCallback                                           state_callback_;
        std::atomic<State>                                      state_{ State::FreshWaitingForGenesis };
        int                                                     callback_owner_token_ = 0;
        std::mutex                                              refresh_execution_mutex_;
        std::shared_ptr<RefreshDispatchState>                   refresh_dispatch_;
        mutable std::mutex                                      candidate_mutex_;
        std::vector<sgns::securecrdt::CandidateId>              pending_policy_candidates_;
        std::vector<sgns::securecrdt::CandidateId>              failed_policy_candidates_;
        std::vector<sgns::securecrdt::CandidateId>              pending_burn_candidates_;
        std::vector<sgns::securecrdt::CandidateId>              failed_burn_candidates_;
        std::optional<sgns::securecrdt::CandidateId>            failed_genesis_candidate_;
    };
}

/// Lets an EventCode be passed straight to any spdlog/fmt call: `logger->info( "{}", event.code )`.
template <>
struct fmt::formatter<sgns::account::TrustStartupController::EventCode> : formatter<std::string_view>
{
    format_context::iterator format( const sgns::account::TrustStartupController::EventCode &code,
                                     format_context                                          &ctx ) const
    {
        return formatter<string_view>::format( sgns::account::TrustStartupController::EventCodeName( code ), ctx );
    }
};

#endif
