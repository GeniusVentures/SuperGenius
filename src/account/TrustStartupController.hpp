/**
 * @file TrustStartupController.hpp
 * @brief Durable trust bootstrap and restart state machine for GeniusNode.
 */
#ifndef SGNS_ACCOUNT_TRUST_STARTUP_CONTROLLER_HPP
#define SGNS_ACCOUNT_TRUST_STARTUP_CONTROLLER_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "outcome/outcome.hpp"
#include "trustedpeer/GenesisManifest.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

namespace sgns::account
{
    class BurnConfig;

    class TrustStartupController : public std::enable_shared_from_this<TrustStartupController>
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
        };

        struct Event
        {
            EventCode                code;
            std::vector<std::string> fields;
            std::string              persisted_fingerprint;
        };

        using EventCallback = std::function<void( const Event & )>;
        using StateCallback = std::function<void( State )>;

        static outcome::result<std::shared_ptr<TrustStartupController>> New(
            std::shared_ptr<sgns::securecrdt::SecureCrdt>        secure_crdt,
            std::shared_ptr<sgns::trustedpeer::TrustStateStore>  trust_store,
            std::optional<sgns::trustedpeer::GenesisManifest>    diagnostic_manifest,
            std::string                                          local_signer_address,
            sgns::trustedpeer::TrustedPeerRegistry::SignCallback sign_callback,
            EventCallback                                        event_callback = {},
            StateCallback                                        state_callback = {} );

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
        void SetState( State state );
        void Emit( EventCode code, std::vector<std::string> fields = {} ) const;
        void RequestRefresh();

        std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt_;
        std::shared_ptr<sgns::trustedpeer::TrustStateStore>     trust_store_;
        std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> registry_;
        std::shared_ptr<BurnConfig>                             burn_config_;
        sgns::trustedpeer::GenesisManifest                      manifest_;
        std::string                                             local_signer_address_;
        EventCallback                                           event_callback_;
        StateCallback                                           state_callback_;
        std::atomic<State>                                      state_{ State::FreshWaitingForGenesis };
        int                                                     callback_owner_token_ = 0;
        std::mutex                                              refresh_execution_mutex_;
        std::mutex                                              refresh_worker_mutex_;
        std::condition_variable                                 refresh_worker_condition_;
        bool                                                    refresh_requested_ = false;
        bool                                                    stop_refresh_worker_ = false;
        std::thread                                             refresh_worker_;
        mutable std::mutex                                      candidate_mutex_;
        bool                                                    burn_candidates_discovered_ = false;
        std::vector<sgns::securecrdt::CandidateId>              pending_burn_candidates_;
        std::vector<sgns::securecrdt::CandidateId>              failed_burn_candidates_;
        std::optional<sgns::securecrdt::CandidateId>            failed_genesis_candidate_;
    };
} // namespace sgns::account

#endif // SGNS_ACCOUNT_TRUST_STARTUP_CONTROLLER_HPP
