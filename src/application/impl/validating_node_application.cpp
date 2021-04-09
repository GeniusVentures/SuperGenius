#include "application/impl/validating_node_application.hpp"

#include <boost/filesystem.hpp>

namespace sgns::application {

  ValidatingNodeApplication::ValidatingNodeApplication(
      const AppConfigPtr &app_config)
      : injector_{injector::makeFullNodeInjector(app_config)},
        logger_(base::createLogger("Application")) {
    spdlog::set_level(app_config->verbosity());

    // genesis launch if database does not exist
    production_execution_strategy_ = boost::filesystem::exists(app_config->rocksdb_path())
                      ? Production::ExecutionStrategy::SYNC_FIRST
                      : Production::ExecutionStrategy::GENESIS;

    // keep important instances, the must exist when injector destroyed
    // some of them are requested by reference and hence not copied
    app_state_manager_ = injector_.create<std::shared_ptr<AppStateManager>>();

    io_context_ = injector_.create<sptr<boost::asio::io_context>>();
    config_storage_ = injector_.create<sptr<ConfigurationStorage>>();
    key_storage_ = injector_.create<sptr<KeyStorage>>();
    clock_ = injector_.create<sptr<clock::SystemClock>>();
    production_ = injector_.create<sptr<Production>>();
    finality_ = injector_.create<sptr<Finality>>();
    router_ = injector_.create<sptr<network::Router>>();

    jrpc_api_service_ = injector_.create<sptr<api::ApiService>>();
  }

  void ValidatingNodeApplication::run() {
    logger_->info("Start as {} with PID {}", typeid(*this).name(), getpid());

    production_->setExecutionStrategy(production_execution_strategy_);

    app_state_manager_->atLaunch([this] {
      // execute listeners
      io_context_->post([this] {
        const auto &current_peer_info =
            injector_.template create<network::OwnPeerInfo>();
        auto &host = injector_.template create<libp2p::Host &>();
        for (const auto &ma : current_peer_info.addresses) {
          auto listen = host.listen(ma);
          if (! listen) {
            logger_->error("Cannot listen address {}. Error: {}",
                           ma.getStringAddress(),
                           listen.error().message());
            std::exit(1);
          }
        }
        this->router_->init();
      });
      return true;
    });

    app_state_manager_->atLaunch([ctx{io_context_}] {
      std::thread asio_runner([ctx{ctx}] { ctx->run(); });
      asio_runner.detach();
      return true;
    });

    app_state_manager_->atShutdown([ctx{io_context_}] { ctx->stop(); });

    app_state_manager_->run();
  }

}  // namespace sgns::application
