
#include "application/impl/block_producing_node_application.hpp"

namespace sgns::application {

  BlockProducingNodeApplication::BlockProducingNodeApplication(
      const AppConfigPtr &app_config)
      : injector_{injector::makeBlockProducingNodeInjector(app_config)},
        logger_(base::createLogger("Application")) {
    spdlog::set_level(app_config->verbosity());

    // keep important instances, the must exist when injector destroyed
    // some of them are requested by reference and hence not copied
    app_state_manager_ = injector_.create<std::shared_ptr<AppStateManager>>();

    io_context_ = injector_.create<sptr<boost::asio::io_context>>();
    config_storage_ = injector_.create<sptr<ConfigurationStorage>>();
    key_storage_ = injector_.create<sptr<KeyStorage>>();
    clock_ = injector_.create<sptr<clock::SystemClock>>();
    production_ = injector_.create<sptr<Production>>();
    router_ = injector_.create<sptr<network::Router>>();

    jrpc_api_service_ = injector_.create<sptr<api::ApiService>>();
  }

  void BlockProducingNodeApplication::run() {
    logger_->info("Start as {} with PID {}", __PRETTY_FUNCTION__, getpid());

    production_->setExecutionStrategy(Production::ExecutionStrategy::SYNC_FIRST);

    app_state_manager_->atLaunch([this] {
      // execute listeners
      io_context_->post([this] {
        const auto &current_peer_info =
            injector_.template create<network::OwnPeerInfo>();
        auto &host = injector_.template create<libp2p::Host &>();
        for (const /*auto*/ libp2p::multi::Multiaddress &ma : current_peer_info.addresses) {
          /*auto*/outcome::result<void> listen = host.listen(ma);
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
