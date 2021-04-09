
#ifndef SUPERGENIUS_SRC_INJECTOR_BLOCK_PRODUCING_NODE_INJECTOR_HPP
#define SUPERGENIUS_SRC_INJECTOR_BLOCK_PRODUCING_NODE_INJECTOR_HPP

#include "application/app_config.hpp"
#include "application/impl/local_key_storage.hpp"
#include "verification/production/impl/production_impl.hpp"
#include "verification/production/impl/syncing_production_observer.hpp"
#include "verification/finality/impl/syncing_round_observer.hpp"
#include "injector/application_injector.hpp"
#include "injector/validating_node_injector.hpp"
#include "runtime/dummy/finality_api_dummy.hpp"
#include "storage/in_memory/in_memory_storage.hpp"

#include "platform/platform.hpp"
namespace sgns::injector {
  namespace di = boost::di;

  template <typename... Ts>
  auto makeBlockProducingNodeInjector(
      const application::AppConfigPtr &app_config, Ts &&... args) {
    using namespace boost;  // NOLINT;
    assert(app_config);

    return di::make_injector<BOOST_DI_CFG>(

        // inherit application injector
        makeApplicationInjector(app_config->genesis_path(),
                                app_config->rocksdb_path(),
                                app_config->rpc_http_endpoint(),
                                app_config->rpc_ws_endpoint()),
        // bind sr25519 keypair
        di::bind<crypto::SR25519Keypair>.to(
            [](auto const &inj) { return get_sr25519_keypair(inj); }),
        // bind ed25519 keypair
        di::bind<crypto::ED25519Keypair>.to(
            [](auto const &inj) { return get_ed25519_keypair(inj); }),
        // compose peer keypair
        di::bind<libp2p::crypto::KeyPair>.to([](auto const &inj) {
          return get_peer_keypair(inj);
        })[boost::di::override],
        // peer info
        di::bind<network::OwnPeerInfo>.to(
            [p2p_port{app_config->p2p_port()}](const auto &injector) {
              return get_peer_info(injector, p2p_port);
            }),

        di::bind<verification::Production>.to(
            [](auto const &inj) { return get_production(inj); }),
        di::bind<verification::ProductionLottery>.template to<verification::ProductionLotteryImpl>(),
        di::bind<network::ProductionObserver>.to(
            [](auto const &inj) { return get_production(inj); }),

        di::bind<verification::finality::RoundObserver>.template to<verification::finality::SyncingRoundObserver>(),
        di::bind<application::KeyStorage>.to(
            [app_config](const auto &injector) {
              return get_key_storage(app_config->keystore_path(), injector);
            }),
        di::bind<runtime::FinalityApi>.TEMPLATE_TO/*template to*/<runtime::dummy::FinalityApiDummy>()
            [boost::di::override],
        di::bind<crypto::CryptoStore>.TEMPLATE_TO/*template to*/(
            [app_config](const auto &injector) {
              return get_crypto_store(app_config->keystore_path(), injector);
            })[boost::di::override],
        // user-defined overrides...
        std::forward<decltype(args)>(args)...);
  }
}  // namespace sgns::injector

#endif  // SUPERGENIUS_SRC_INJECTOR_BLOCK_PRODUCING_NODE_INJECTOR_HPP
