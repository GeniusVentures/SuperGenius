

#include "verification/finality/impl/finality_impl.hpp"

#include <boost/asio/post.hpp>
#include <storage/database_error.hpp>

#include "verification/finality/impl/vote_crypto_provider_impl.hpp"
#include "verification/finality/impl/vote_tracker_impl.hpp"
#include "verification/finality/impl/voting_round_impl.hpp"
#include "verification/finality/vote_graph/vote_graph_impl.hpp"
#include "scale/scale.hpp"
#include "storage/predefined_keys.hpp"

namespace sgns::verification::finality {

  static size_t round_id = 0;

  FinalityImpl::FinalityImpl( std::shared_ptr<application::AppStateManager> app_state_manager,
                              std::shared_ptr<Environment>                  environment,
                              std::shared_ptr<storage::BufferStorage>       storage,
                              std::shared_ptr<crypto::ED25519Provider>      crypto_provider,
                              //std::shared_ptr<runtime::FinalityApi> finality_api,
                              crypto::ED25519Keypair                       keypair,
                              std::shared_ptr<Clock>                       clock,
                              std::shared_ptr<boost::asio::io_context>     io_context,
                              std::shared_ptr<authority::AuthorityManager> authority_manager ) :
      app_state_manager_( std::move( app_state_manager ) ),
      environment_{ std::move( environment ) },
      storage_{ std::move( storage ) },
      crypto_provider_{ std::move( crypto_provider ) },
      keypair_{ std::move( keypair ) },
      clock_{ std::move( clock ) },
      io_context_{ std::move( io_context ) },
      authority_manager_( std::move( authority_manager ) )
  {
      BOOST_ASSERT( app_state_manager_ != nullptr );
      BOOST_ASSERT( environment_ != nullptr );
      BOOST_ASSERT( storage_ != nullptr );
      BOOST_ASSERT( crypto_provider_ != nullptr );
      //BOOST_ASSERT(finality_api_ != nullptr);
      BOOST_ASSERT( clock_ != nullptr );
      BOOST_ASSERT( io_context_ != nullptr );
      BOOST_ASSERT( authority_manager_ != nullptr );

      app_state_manager_->takeControl( *this );
  }

  bool FinalityImpl::prepare() {
    // Lambda which is executed when voting round is completed.
    environment_->doOnCompleted(
        [wp = weak_from_this()](
            outcome::result<CompletedRound> completed_round_res) {
          if (auto self = wp.lock()) {
            self->onCompletedRound(std::move(completed_round_res));
          }
        });
    return true;
  }

  bool FinalityImpl::start() {
    // Obtain last completed round
    auto last_round_res = getLastCompletedRound();
    if (! last_round_res.has_value()) {
      // No saved data
      if (last_round_res
          != outcome::failure(storage::DatabaseError::NOT_FOUND)) {
        logger_->critical(
            "Can't retrieve last round data: {}. Stopping finality execution",
            last_round_res.error().message());
        return false;
      }
    }

    auto &[last_round_number, last_round_state] = last_round_res.value();

    logger_->debug("Finality is starting with round #{}", last_round_number + 1);

    current_round_ = makeInitialRound(last_round_number, last_round_state);
    if (current_round_ == nullptr) {
      logger_->critical(
          "Next round hasn't been made. Stopping finality execution");
      return false;
    }

    // Planning play round
    boost::asio::post(*io_context_, [wp = current_round_->weak_from_this()] {
      if (auto round = wp.lock()) {
        round->play();
      }
    });

    return true;
  }

  void FinalityImpl::stop() {}

  std::shared_ptr<VotingRound> FinalityImpl::makeInitialRound(
      RoundNumber previous_round_number,
      std::shared_ptr<const RoundState> previous_round_state) {
    if (previous_round_state == nullptr) {
      logger_->critical(
          "Can't make initial round: previous round state is null");
      return {};
    }

    // Obtain finality voters
    auto voters_res = getVoters();
    if (! voters_res.has_value()) {
      logger_->critical("Can't retrieve voters: {}. Stopping finality execution",
                        voters_res.error().message());
      return {};
    }
    const auto &voters = voters_res.value();
    if (voters->empty()) {
      logger_->critical("Voters are empty. Stopping finality execution");
      return {};
    }

    const auto new_round_number = previous_round_number + 1;

    auto vote_graph = std::make_shared<VoteGraphImpl>(
        previous_round_state->finalized.value_or(
            previous_round_state->last_finalized_block),
        environment_);

    FinalityConfig config{
        /*.voters = */voters,
        /*.round_number =*/ new_round_number,
        /*.duration = */std::chrono::milliseconds(
            333),  // Note: Duration value was gotten from substrate:
                   // https://github.com/paritytech/substrate/blob/efbac7be80c6e8988a25339061078d3e300f132d/bin/node-template/node/src/service.rs#L166
        /*.peer_id = */keypair_.public_key};

    auto vote_crypto_provider = std::make_shared<VoteCryptoProviderImpl>(
        keypair_, crypto_provider_, new_round_number, voters);

    auto new_round = std::make_shared<VotingRoundImpl>(
        shared_from_this(),
        std::move(config),
        environment_,
        std::move(vote_crypto_provider),
        std::make_shared<VoteTrackerImpl>(),  // Prevote tracker
        std::make_shared<VoteTrackerImpl>(),  // Precommit tracker
        std::move(vote_graph),
        clock_,
        io_context_,
        std::move(previous_round_state));

    return new_round;
  }

  std::shared_ptr<VotingRound> FinalityImpl::makeNextRound(
      const std::shared_ptr<VotingRound> &round) {
    logger_->debug("Making next round");

    // Obtain finality voters
    auto voters_res = getVoters();
    if (! voters_res.has_value()) {
      logger_->critical("Can't retrieve voters: {}. Stopping finality execution",
                        voters_res.error().message());
      return {};
    }
    const auto &voters = voters_res.value();
    if (voters->empty()) {
      logger_->critical("Voters are empty. Stopping finality execution");
      return {};
    }

    const auto new_round_number = round->roundNumber() + 1;

    BlockInfo best_block = round->state()->finalized.value_or(
        round->state()->last_finalized_block);

    auto vote_graph = std::make_shared<VoteGraphImpl>(best_block, environment_);

    FinalityConfig config{
        /*.voters =*/ voters,
        /*.round_number =*/ new_round_number,
        /*.duration = */std::chrono::milliseconds(
            333),  // Note: Duration value was gotten from substrate:
                   // https://github.com/paritytech/substrate/blob/efbac7be80c6e8988a25339061078d3e300f132d/bin/node-template/node/src/service.rs#L166
        /*.peer_id =*/ keypair_.public_key};

    auto vote_crypto_provider = std::make_shared<VoteCryptoProviderImpl>(
        keypair_, crypto_provider_, new_round_number, voters);

    auto new_round = std::make_shared<VotingRoundImpl>(
        shared_from_this(),
        std::move(config),
        environment_,
        std::move(vote_crypto_provider),
        std::make_shared<VoteTrackerImpl>(),  // Prevote tracker
        std::make_shared<VoteTrackerImpl>(),  // Precommit tracker
        std::move(vote_graph),
        clock_,
        io_context_,
        round);
    return new_round;
  }

  std::shared_ptr<VotingRound> FinalityImpl::selectRound(
      RoundNumber round_number) {
    std::shared_ptr<VotingRound> target_round;
    if (current_round_ && current_round_->roundNumber() == round_number) {
      target_round = current_round_;
    } else if (previous_round_
               && previous_round_->roundNumber() == round_number) {
      target_round = previous_round_;
    }
    return target_round;
  }

  outcome::result<std::shared_ptr<VoterSet>> FinalityImpl::getVoters() const {
    /*
     * TODO(kamilsa): PRE-356 Check if voters were updated:
     * We should check if voters received from runtime (through
     * finality->finality_authorities() runtime entry call) differ from the ones
     * that we obtained from the storage. If so, we should return voter set with
     * incremented voter set and consisting of new voters. Also round number
     * should be reset to 0
     */
    OUTCOME_TRY((auto &&, voters_encoded), storage_->get(base::Buffer().put(storage::GetAuthoritySetKey())));
    OUTCOME_TRY((auto &&, voter_set), scale::decode<VoterSet>(voters_encoded));
    return std::make_shared<VoterSet>(voter_set);
  }

  outcome::result<CompletedRound> FinalityImpl::getLastCompletedRound() const {
    auto last_round_encoded_res = storage_->get(base::Buffer().put(storage::GetSetStateKey()));

    // Saved data exists
    if (last_round_encoded_res.has_value()) {
      return scale::decode<CompletedRound>(last_round_encoded_res.value());
    }

    // Fail at retrieve data
    if (last_round_encoded_res
        != outcome::failure(storage::DatabaseError::NOT_FOUND)) {
      logger_->critical(
          "Can't retrieve last round data: {}. Stopping finality execution",
          last_round_encoded_res.error().message());
      return last_round_encoded_res.as_failure();
    }

    // No saved data - make from genesis
    auto genesis_hash_res = storage_->get(base::Buffer().put(storage::GetGenesisBlockHashLookupKey()));
    if (! genesis_hash_res.has_value()) {
      logger_->critical(
          "Can't retrieve genesis block hash: {}. Stopping finality execution",
          genesis_hash_res.error().message());
      return genesis_hash_res.as_failure();
    }

    primitives::BlockHash genesis_hash;
    std::copy(genesis_hash_res.value().begin(),
              genesis_hash_res.value().end(),
              genesis_hash.begin());

    auto round_state = std::make_shared<const RoundState>(RoundState{
        /*.last_finalized_block =*/ primitives::BlockInfo(1, genesis_hash),
        /*.best_prevote_candidate =*/ Prevote(1, genesis_hash),
        /*.best_final_candidate =*/ primitives::BlockInfo(1, genesis_hash),
        /*.finalized =*/ primitives::BlockInfo(1, genesis_hash)});

    CompletedRound zero_round{/*.round_number = */0, /*.state = */round_state};

    return zero_round;
  }

  void FinalityImpl::executeNextRound() {
    logger_->debug("Execute next round #{} -> #{}",
                   current_round_->roundNumber(),
                   current_round_->roundNumber() + 1);
    previous_round_.swap(current_round_);
    previous_round_->end();
    current_round_ = makeNextRound(previous_round_);
    current_round_->play();
  }

  void FinalityImpl::onVoteMessage(const VoteMessage &msg) {
    std::shared_ptr<VotingRound> target_round = selectRound(msg.round_number);
    if ( !target_round )
    {
        return;
    }

    // get block info
    auto blockInfo = visit_in_place(msg.vote.message, [](const auto &vote) {
      return BlockInfo(vote.block_number, vote.block_hash);
    });

    // get authorities
    /*const auto &weighted_authorities_res =
        finality_api_->authorities(primitives::BlockId(blockInfo.block_number));
    if (!weighted_authorities_res.has_value()) {
      logger_->error("Can't get authorities");
      return;
    };
    auto &weighted_authorities = weighted_authorities_res.value();
    */
    //TODO - Replace with actual authority list
    primitives::AuthorityList weighted_authorities;


    // find signer in authorities
    auto weighted_authority_it =
        std::find_if(weighted_authorities.begin(),
                     weighted_authorities.end(),
                     [&id = msg.vote.id](const auto &weighted_authority) {
                       return weighted_authority.id.id == id;
                     });

    if (weighted_authority_it == weighted_authorities.end()) {
      logger_->warn("Vote signed by unknown validator");
      return;
    };

    visit_in_place(
        msg.vote.message,
        [&target_round, &msg](const PrimaryPropose &primary_propose) {
          target_round->onPrimaryPropose(msg.vote);
        },
        [&target_round, &msg](const Prevote &prevote) {
          target_round->onPrevote(msg.vote);
        },
        [&target_round, &msg](const Precommit &precommit) {
          target_round->onPrecommit(msg.vote);
        });
  }

  void FinalityImpl::onFinalize(const Fin &f) {
    logger_->debug("Received fin message for round: {}", f.round_number);

    std::shared_ptr<VotingRound> target_round = selectRound(f.round_number);
    if (! target_round) return;

    target_round->onFinalize(f);
  }

  void FinalityImpl::onCompletedRound(
      outcome::result<CompletedRound> completed_round_res) {
    round_id++;

    if (! completed_round_res) {
      logger_->debug("Finality round was not finalized: {}",
                     completed_round_res.error().message());
    } else {
      const auto &completed_round = completed_round_res.value();

      logger_->debug("Event OnCompleted for round #{}",
                     completed_round.round_number);

      if (auto put_res = storage_->put(
              base::Buffer().put(storage::GetSetStateKey()),
              base::Buffer(scale::encode(completed_round).value()));
          ! put_res) {
        logger_->error("New round state was not added to the storage");
        return;
      }

      BOOST_ASSERT(storage_->get(base::Buffer().put(storage::GetSetStateKey())));
    }

    boost::asio::post(*io_context_, [wp = weak_from_this()] {
      if (auto finality = wp.lock()) {
        finality->executeNextRound();
      }
    });
  }
}  // namespace sgns::verification::finality
