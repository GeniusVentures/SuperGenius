

#ifndef SUPERGENIUS_SRC_VERIFICATION_FINALITY_IMPL_LAUNCHERIMPL_HPP
#define SUPERGENIUS_SRC_VERIFICATION_FINALITY_IMPL_LAUNCHERIMPL_HPP

#include "blockchain/block_tree.hpp"
// #include "base/logger.hpp"
#include "verification/finality/completed_round.hpp"
#include "verification/finality/environment.hpp"
#include "verification/finality/launcher.hpp"
#include "verification/finality/voter_set.hpp"
#include "verification/finality/voting_round.hpp"
#include "crypto/ed25519_provider.hpp"
#include "network/gossiper.hpp"
#include "runtime/finality.hpp"
#include "storage/buffer_map_types.hpp"
#include "base/logger.hpp"
namespace sgns::verification::finality {

  class LauncherImpl : public Launcher,
                       public std::enable_shared_from_this<LauncherImpl> {
   public:
    ~LauncherImpl() override = default;

    LauncherImpl(std::shared_ptr<Environment> environment,
                 std::shared_ptr<storage::BufferStorage> storage,
                 std::shared_ptr<crypto::ED25519Provider> crypto_provider,
                 std::shared_ptr<runtime::Finality> finality_api,
                 const crypto::ED25519Keypair &keypair,
                 std::shared_ptr<Clock> clock,
                 std::shared_ptr<boost::asio::io_context> io_context);

    void start() override;

    /**
     * TODO (PRE-371): kamilsa remove this method when finality issue resolved
     *
     * Start timer which constantly checks if finality rounds are running. If not
     * relaunches finality
     */
    void startLivenessChecker();

    void onVoteMessage(const VoteMessage &msg) override;

    void onFinalize(const Fin &f) override;

    void executeNextRound();

   private:
    outcome::result<std::shared_ptr<VoterSet>> getVoters() const;
    outcome::result<CompletedRound> getLastCompletedRound() const;

    std::shared_ptr<VotingRound> current_round_;

    std::shared_ptr<Environment> environment_;
    std::shared_ptr<storage::BufferStorage> storage_;
    std::shared_ptr<crypto::ED25519Provider> crypto_provider_;
    std::shared_ptr<runtime::Finality> finality_api_;
    crypto::ED25519Keypair keypair_;
    std::shared_ptr<Clock> clock_;
    std::shared_ptr<boost::asio::io_context> io_context_;
    Timer liveness_checker_;

    base::Logger logger_ = base::createLogger("Finality launcher");
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_SRC_VERIFICATION_FINALITY_IMPL_LAUNCHERIMPL_HPP
