

#ifndef SUPERGENIUS_SRC_VERIFICATION_FINALITY_IMPL_FINALITYIMPL
#define SUPERGENIUS_SRC_VERIFICATION_FINALITY_IMPL_FINALITYIMPL

#include "verification/finality/finality.hpp"

#include "application/app_state_manager.hpp"
#include "base/logger.hpp"
#include "verification/authority/authority_manager.hpp"
#include "verification/finality/completed_round.hpp"
#include "verification/finality/environment.hpp"
#include "verification/finality/impl/voting_round_impl.hpp"
#include "verification/finality/voter_set.hpp"
#include "crypto/ed25519_provider.hpp"
#include "storage/buffer_map_types.hpp"

namespace sgns::verification::finality {

  class FinalityImpl : public Finality,
                      public std::enable_shared_from_this<FinalityImpl> {
   public:
    ~FinalityImpl() override = default;
    //TODO - Check if finalityAPI needed. Removed due to binaryen
    FinalityImpl( std::shared_ptr<application::AppStateManager> app_state_manager,
                  std::shared_ptr<Environment>                  environment,
                  std::shared_ptr<storage::BufferStorage>       storage,
                  std::shared_ptr<crypto::ED25519Provider>      crypto_provider,
                  crypto::ED25519Keypair                        keypair,
                  //std::shared_ptr<runtime::FinalityApi> finality_api,
                  std::shared_ptr<Clock>                       clock,
                  std::shared_ptr<boost::asio::io_context>     io_context,
                  std::shared_ptr<authority::AuthorityManager> authority_manager );

    /** @see AppStateManager::takeControl */
    bool prepare();

    /** @see AppStateManager::takeControl */
    bool start();

    /** @see AppStateManager::takeControl */
    void stop();

    void executeNextRound() override;

    void onVoteMessage(const VoteMessage &msg) override;

    void onFinalize(const Fin &f) override;

    std::string GetName() override
    {
      return "FinalityImpl";
    }

   private:
    std::shared_ptr<VotingRound> selectRound(RoundNumber round_number);
    outcome::result<std::shared_ptr<VoterSet>> getVoters() const;
    outcome::result<CompletedRound> getLastCompletedRound() const;

    std::shared_ptr<VotingRound> makeInitialRound(
        RoundNumber previous_round_number,
        std::shared_ptr<const RoundState> previous_round_state);

    std::shared_ptr<VotingRound> makeNextRound(
        const std::shared_ptr<VotingRound> &previous_round);

    void onCompletedRound(outcome::result<CompletedRound> completed_round_res);

    std::shared_ptr<VotingRound> previous_round_;
    std::shared_ptr<VotingRound> current_round_;

    std::shared_ptr<application::AppStateManager> app_state_manager_;
    std::shared_ptr<Environment> environment_;
    std::shared_ptr<storage::BufferStorage> storage_;
    std::shared_ptr<crypto::ED25519Provider> crypto_provider_;
    //std::shared_ptr<runtime::FinalityApi> finality_api_;
    crypto::ED25519Keypair keypair_;
    std::shared_ptr<Clock> clock_;
    std::shared_ptr<boost::asio::io_context> io_context_;
    std::shared_ptr<authority::AuthorityManager> authority_manager_;

    base::Logger logger_ = base::createLogger("Finality");
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_SRC_VERIFICATION_FINALITY_IMPL_FINALITYIMPL
