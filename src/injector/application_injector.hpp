#ifndef SUPERGENIUS_SRC_INJECTOR_APPLICATION_INJECTOR_HPP
#define SUPERGENIUS_SRC_INJECTOR_APPLICATION_INJECTOR_HPP

#include <boost/di.hpp>
#include <boost/di/extension/scopes/shared.hpp>

// added to fix "fatal error C1189: #error:  WinSock.h has already been included " in windows build
// this error is generated in libp2p/injector/host_injector.hpp
#include <boost/asio.hpp>
//#include <libp2p/host/host.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/peer/peer_info.hpp>
//end

#include <crypto/bip39/impl/bip39_provider_impl.hpp>
#include <crypto/crypto_store/crypto_store_impl.hpp>
#include <crypto/pbkdf2/impl/pbkdf2_provider_impl.hpp>
#include <crypto/secp256k1/secp256k1_provider_impl.hpp>

#include "outcome/outcome.hpp"
#include "api/service/api_service.hpp"
#include "api/service/author/author_jrpc_processor.hpp"
#include "api/service/author/impl/author_api_impl.hpp"
#include "api/service/chain/chain_jrpc_processor.hpp"
#include "api/service/chain/impl/chain_api_impl.hpp"
#include "api/service/state/impl/state_api_impl.hpp"
#include "api/service/state/state_jrpc_processor.hpp"
#include "api/service/system/impl/system_api_impl.hpp"
#include "api/service/system/system_jrpc_processor.hpp"
#include "api/transport/impl/http/http_listener_impl.hpp"
#include "api/transport/impl/http/http_session.hpp"
#include "api/transport/impl/ws/ws_listener_impl.hpp"
#include "api/transport/impl/ws/ws_session.hpp"
#include "api/transport/rpc_thread_pool.hpp"

#include "application/impl/app_state_manager_impl.hpp"
#include "application/impl/configuration_storage_impl.hpp"
#include "authorship/impl/block_builder_factory_impl.hpp"
#include "authorship/impl/block_builder_impl.hpp"
#include "authorship/impl/proposer_impl.hpp"
#include "blockchain/impl/block_tree_impl.hpp"
#include "blockchain/impl/key_value_block_header_repository.hpp"
#include "blockchain/impl/key_value_block_storage.hpp"
#include "blockchain/impl/storage_util.hpp"
#include "clock/impl/basic_waitable_timer.hpp"
#include "clock/impl/clock_impl.hpp"
#include "base/outcome_throw.hpp"
#include "verification/authority/authority_manager.hpp"
#include "verification/authority/authority_update_observer.hpp"
#include "verification/authority/impl/authority_manager_impl.hpp"
#include "verification/production/impl/production_synchronizer_impl.hpp"
#include "verification/production/impl/epoch_storage_impl.hpp"
#include "verification/finality/finalization_observer.hpp"
#include "verification/finality/impl/environment_impl.hpp"
#include "verification/finality/impl/finalization_composite.hpp"
#include "verification/finality/impl/vote_crypto_provider_impl.hpp"
#include "verification/validation/production_block_validator.hpp"
#include "primitives/production_configuration.hpp"
#include "crypto/ed25519/ed25519_provider_impl.hpp"
#include "crypto/hasher/hasher_impl.hpp"
#include "crypto/random_generator/boost_generator.hpp"
#include "crypto/sr25519/sr25519_provider_impl.hpp"
#include "crypto/vrf/vrf_provider_impl.hpp"
#include "extensions/impl/extension_factory_impl.hpp"
#include "network/impl/dummy_sync_protocol_client.hpp"
#include "network/impl/extrinsic_observer_impl.hpp"
#include "network/impl/gossiper_broadcast.hpp"
#include "network/impl/remote_sync_protocol_client.hpp"
#include "network/impl/router_libp2p.hpp"
#include "network/impl/sync_protocol_observer_impl.hpp"
#include "network/sync_protocol_observer.hpp"
#include "network/types/sync_clients_set.hpp"
#include "runtime/binaryen/module/wasm_module_impl.hpp"
#include "runtime/binaryen/module/wasm_module_factory_impl.hpp"

#include "runtime/binaryen/runtime_api/production_api_impl.hpp"
#include "runtime/binaryen/runtime_api/block_builder_impl.hpp"
#include "runtime/binaryen/runtime_api/core_factory_impl.hpp"
#include "runtime/binaryen/runtime_api/core_impl.hpp"
#include "runtime/binaryen/runtime_api/finality_api_impl.hpp"
#include "runtime/binaryen/runtime_api/metadata_impl.hpp"
#include "runtime/binaryen/runtime_api/offchain_worker_impl.hpp"
#include "runtime/binaryen/runtime_api/parachain_host_impl.hpp"
#include "runtime/binaryen/runtime_api/tagged_transaction_queue_impl.hpp"
#include "runtime/common/storage_wasm_provider.hpp"
#include "runtime/common/trie_storage_provider_impl.hpp"

#include "storage/changes_trie/impl/storage_changes_tracker_impl.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "storage/predefined_keys.hpp"
#include "storage/trie/impl/trie_storage_backend_impl.hpp"
#include "storage/trie/impl/trie_storage_impl.hpp"
#include "storage/trie/supergenius_trie/supergenius_trie_factory_impl.hpp"
#include "storage/trie/serialization/supergenius_codec.hpp"
#include "storage/trie/serialization/trie_serializer_impl.hpp"
#include "transaction_pool/impl/pool_moderator_impl.hpp"
#include "transaction_pool/impl/transaction_pool_impl.hpp"

namespace sgns::injector {
  enum class InjectorError {
    INDEX_OUT_OF_BOUND = 1,  // index out of bound
  };
}

OUTCOME_HPP_DECLARE_ERROR_2(sgns::injector, InjectorError);

namespace sgns::injector {
  namespace di = boost::di;

  template <typename C>
  auto useConfig(C c) {
    return boost::di::bind<std::decay_t<C>>()./*template */to(
        std::move(c))[boost::di::override];
  }

  template <class T>
  using sptr = std::shared_ptr<T>;

  template <class T>
  using uptr = std::unique_ptr<T>;

  template <typename Injector>
  sptr<network::PeerList> get_boot_nodes(const Injector &injector) {
    static auto initialized =
        boost::optional<sptr<network::PeerList>>(boost::none);
    if (initialized) {
      return initialized.value();
    }
    auto &cfg = injector.template create<application::ConfigurationStorage &>();

    initialized = std::make_shared<network::PeerList>(cfg.getBootNodes());
    return initialized.value();
  }

  template <typename Injector>
  sptr<api::ApiService> get_jrpc_api_service(const Injector &injector) {
    static auto initialized =
        boost::optional<sptr<api::ApiService>>(boost::none);
    if (initialized) {
      return initialized.value();
    }
    using SubscriptionEnginePtr = std::shared_ptr<
        subscription::SubscriptionEngine<base::Buffer,
                                         std::shared_ptr<api::Session>,
                                         base::Buffer,
                                         primitives::BlockHash>>;
    auto subscription_engine =
        injector.template create<SubscriptionEnginePtr>();
    auto app_state_manager =
        injector
            .template create<std::shared_ptr<application::AppStateManager>>();
    auto rpc_thread_pool =
        injector.template create<std::shared_ptr<api::RpcThreadPool>>();
    std::vector<std::shared_ptr<api::Listener>> listeners{
        injector.template create<std::shared_ptr<api::HttpListenerImpl>>(),
        injector.template create<std::shared_ptr<api::WsListenerImpl>>(),
    };
    auto server = injector.template create<std::shared_ptr<api::JRpcServer>>();
    std::vector<std::shared_ptr<api::JRpcProcessor>> processors{
        injector
            .template create<std::shared_ptr<api::state::StateJRpcProcessor>>(),
        injector.template create<
            std::shared_ptr<api::author::AuthorJRpcProcessor>>(),
        injector
            .template create<std::shared_ptr<api::chain::ChainJRpcProcessor>>(),
        injector.template create<
            std::shared_ptr<api::system::SystemJRpcProcessor>>()};

    initialized =
        std::make_shared<api::ApiService>(std::move(app_state_manager),
                                          std::move(rpc_thread_pool),
                                          std::move(listeners),
                                          std::move(server),
                                          processors,
                                          std::move(subscription_engine));

    auto state_api = injector.template create<std::shared_ptr<api::StateApi>>();
    state_api->setApiService(initialized.value());
    return initialized.value();
  }

    // jrpc api listener (over HTTP) getter
  template <typename Injector>
  sptr<api::HttpListenerImpl> get_jrpc_api_http_listener(
      const Injector &injector,
      const boost::asio::ip::tcp::endpoint &endpoint) {
    static auto initialized =
        boost::optional<sptr<api::HttpListenerImpl>>(boost::none);
    if (initialized) {
      return initialized.value();
    }

    auto app_state_manager =
        injector.template create<sptr<application::AppStateManager>>();

    auto context = injector.template create<sptr<api::RpcContext>>();

    api::HttpListenerImpl::Configuration listener_config;
    listener_config.endpoint = endpoint;

    auto &&http_session_config =
        injector.template create<api::HttpSession::Configuration>();

    initialized = std::make_shared<api::HttpListenerImpl>(
        app_state_manager, context, listener_config, http_session_config);
    return initialized.value();
  }

  // jrpc api listener (over WebSockets) getter
  template <typename Injector>
  sptr<api::WsListenerImpl> get_jrpc_api_ws_listener(
      const Injector &injector,
      const boost::asio::ip::tcp::endpoint &endpoint) {
    static auto initialized =
        boost::optional<sptr<api::WsListenerImpl>>(boost::none);
    if (initialized) {
      return initialized.value();
    }

    auto app_state_manager =
        injector.template create<sptr<application::AppStateManager>>();

    auto context = injector.template create<sptr<api::RpcContext>>();

    api::WsListenerImpl::Configuration listener_config;
    listener_config.endpoint = endpoint;

    auto &&ws_session_config =
        injector.template create<api::WsSession::Configuration>();

    initialized = std::make_shared<api::WsListenerImpl>(
        app_state_manager, context, listener_config, ws_session_config);
    return initialized.value();
  }

  // block storage getter
  template <typename Injector>
  sptr<blockchain::BlockStorage> get_block_storage(const Injector &injector) {
    static auto initialized =
        boost::optional<sptr<blockchain::BlockStorage>>(boost::none);

    if (initialized) {
      return initialized.value();
    }
    auto &&hasher = injector.template create<sptr<crypto::Hasher>>();

    const auto &db = injector.template create<sptr<storage::BufferStorage>>();

    const auto &trie_storage =
        injector.template create<sptr<storage::trie::TrieStorage>>();

    auto storage = blockchain::KeyValueBlockStorage::create(
        trie_storage->getRootHash(),
        db,
        hasher,
        [&db, &injector](const primitives::Block &genesis_block) {
          // handle genesis initialization, which happens when there is not
          // authorities and last completed round in the storage
          if (! db->get(storage::GetAuthoritySetKey())) {
            // insert authorities
            auto finality_api =
                injector.template create<sptr<runtime::FinalityApi>>();
            const auto &weighted_authorities_res = finality_api->authorities(
                primitives::BlockId(primitives::BlockNumber{0}));
            BOOST_ASSERT_MSG(weighted_authorities_res,
                             "finality_api_->authorities failed");
            const auto &weighted_authorities = weighted_authorities_res.value();

            for (const auto authority : weighted_authorities) {
              spdlog::info("Finality authority: {}", authority.id.id.toHex());
            }

            verification::finality::VoterSet voters{0};
            for (const auto &weighted_authority : weighted_authorities) {
              voters.insert(weighted_authority.id.id,
                            weighted_authority.weight);
              spdlog::debug("Added to finality authorities: {}, weight: {}",
                            weighted_authority.id.id.toHex(),
                            weighted_authority.weight);
            }
            BOOST_ASSERT_MSG(voters.size() != 0, "Finality voters are empty");
            auto authorities_put_res =
                db->put(storage::GetAuthoritySetKey(),
                        base::Buffer(scale::encode(voters).value()));
            if (! authorities_put_res) {
              BOOST_ASSERT_MSG(false, "Could not insert authorities");
              std::exit(1);
            }
          }
        });
    if (storage.has_error()) {
      base::raise(storage.error());
    }
    initialized = storage.value();
    return initialized.value();
  }

  // block tree getter
  template <typename Injector>
  sptr<blockchain::BlockTree> get_block_tree(const Injector &injector) {
    static auto initialized =
        boost::optional<sptr<blockchain::BlockTree>>(boost::none);

    if (initialized) {
      return initialized.value();
    }
    auto header_repo =
        injector.template create<sptr<blockchain::BlockHeaderRepository>>();

    auto &&storage = injector.template create<sptr<blockchain::BlockStorage>>();

    auto last_finalized_block_res = storage->getLastFinalizedBlockHash();

    /*const*/ auto block_id =
        last_finalized_block_res.has_value()
            ? primitives::BlockId{last_finalized_block_res.value()}
            : primitives::BlockId{0};

    auto &&extrinsic_observer =
        injector.template create<sptr<network::ExtrinsicObserver>>();

    auto &&hasher = injector.template create<sptr<crypto::Hasher>>();

    auto &&tree =
        blockchain::BlockTreeImpl::create(std::move(header_repo),
                                          storage,
                                          block_id,
                                          std::move(extrinsic_observer),
                                          std::move(hasher));
    if (!tree) {
      base::raise(tree.error());
    }
    initialized = tree.value();
    return initialized.value();
  }

  template <typename Injector>
  sptr<extensions::ExtensionFactoryImpl> get_extension_factory(
      const Injector &injector) {
    static auto initialized =
        boost::optional<sptr<extensions::ExtensionFactoryImpl>>(boost::none);
    if (initialized) {
      return initialized.value();
    }
    auto tracker =
        injector.template create<sptr<storage::changes_trie::ChangesTracker>>();
    auto sr25519_provider =
        injector.template create<sptr<crypto::SR25519Provider>>();
    auto ed25519_provider =
        injector.template create<sptr<crypto::ED25519Provider>>();
    auto secp256k1_provider =
        injector.template create<sptr<crypto::Secp256k1Provider>>();
    auto hasher = injector.template create<sptr<crypto::Hasher>>();
    auto crypto_store = injector.template create<sptr<crypto::CryptoStore>>();
    auto bip39_provider =
        injector.template create<sptr<crypto::Bip39Provider>>();
    auto core_factory_method =
        [&injector](sptr<runtime::WasmProvider> wasm_provider) {
          auto core_factory =
              injector.template create<sptr<runtime::CoreFactory>>();
          return core_factory->createWithCode(wasm_provider);
        };

    initialized =
        std::make_shared<extensions::ExtensionFactoryImpl>(tracker,
                                                           sr25519_provider,
                                                           ed25519_provider,
                                                           secp256k1_provider,
                                                           hasher,
                                                           crypto_store,
                                                           bip39_provider,
                                                           core_factory_method);
    return initialized.value();
  }

  template <typename Injector>
  sptr<storage::trie::TrieStorageBackendImpl> get_trie_storage_backend(
      const Injector &injector) {
    static auto initialized =
        boost::optional<sptr<storage::trie::TrieStorageBackendImpl>>(
            boost::none);

    if (initialized) {
      return initialized.value();
    }
    auto storage = injector.template create<sptr<storage::BufferStorage>>();
    using blockchain::prefix::TRIE_NODE;
    auto backend = std::make_shared<storage::trie::TrieStorageBackendImpl>(
        storage, base::Buffer{TRIE_NODE});
    initialized = backend;
    return backend;
  }

  template <typename Injector>
  sptr<storage::trie::TrieStorageImpl> get_trie_storage_impl(
      const Injector &injector) {
    static auto initialized =
        boost::optional<sptr<storage::trie::TrieStorageImpl>>(boost::none);

    if (initialized) {
      return initialized.value();
    }
    auto factory =
        injector.template create<sptr<storage::trie::SuperGeniusTrieFactory>>();
    auto codec = injector.template create<sptr<storage::trie::Codec>>();
    auto serializer =
        injector.template create<sptr<storage::trie::TrieSerializer>>();
    auto tracker =
        injector.template create<sptr<storage::changes_trie::ChangesTracker>>();
    auto trie_storage_res = storage::trie::TrieStorageImpl::createEmpty(
        factory, codec, serializer, tracker);
    if (!trie_storage_res) {
      base::raise(trie_storage_res.error());
    }

    sptr<storage::trie::TrieStorageImpl> trie_storage =
        std::move(trie_storage_res.value());
    initialized = trie_storage;
    return trie_storage;
  }

  template <typename Injector>
  sptr<storage::trie::TrieStorage> get_trie_storage(const Injector &injector) {
    static auto initialized =
        boost::optional<sptr<storage::trie::TrieStorage>>(boost::none);
    if (initialized) {
      return initialized.value();
    }
    auto configuration_storage =
        injector.template create<sptr<application::ConfigurationStorage>>();
    const auto &genesis_raw_configs = configuration_storage->getGenesis();

    auto trie_storage =
        injector.template create<sptr<storage::trie::TrieStorageImpl>>();
    auto batch = trie_storage->getPersistentBatch();
    if (! batch) {
      base::raise(batch.error());
    }
    for (const auto &[key, val] : genesis_raw_configs) {
      spdlog::info(
          "Key: {}, Val: {}", key.toHex(), val.toHex().substr(0, 200));
      if (auto res = batch.value()->put(key, val); ! res) {
        base::raise(res.error());
      }
    }
    if (auto res = batch.value()->commit(); ! res) {
      base::raise(res.error());
    }

    initialized = trie_storage;
    return trie_storage;
  }

  // rocks db getter
  template <typename Injector>
  sptr<storage::BufferStorage> get_rocks_db(std::string_view rocksdb_path,
                                            const Injector &injector) {
    static auto initialized =
        boost::optional<sptr<storage::BufferStorage>>(boost::none);
    if (initialized) {
      return initialized.value();
    }
    auto options = rocksdb::Options{};
    options.create_if_missing = true;

    auto db = storage::rocksdb::create(rocksdb_path, options);
    if (!db) {
      base::raise(db.error());
    }
    initialized = db.value();
    return initialized.value();
  };

  // configuration storage getter
  template <typename Injector>
  std::shared_ptr<application::ConfigurationStorage> get_configuration_storage(
      std::string_view genesis_path, const Injector &injector) {
    static auto initialized =
        boost::optional<sptr<application::ConfigurationStorage>>(boost::none);
    if (initialized) {
      return initialized.value();
    }
    auto config_storage_res = application::ConfigurationStorageImpl::create(
        std::string(genesis_path));
    if (config_storage_res.has_error()) {
      base::raise(config_storage_res.error());
    }
    initialized = config_storage_res.value();
    return config_storage_res.value();
  };

  template <typename Injector>
  sptr<network::SyncClientsSet> get_sync_clients_set(const Injector &injector) {
    static auto initialized =
        boost::optional<sptr<network::SyncClientsSet>>(boost::none);
    if (initialized) {
      return initialized.value();
    }
    auto configuration_storage =
        injector.template create<sptr<application::ConfigurationStorage>>();
    auto peer_infos = configuration_storage->getBootNodes().peers;

    auto host = injector.template create<sptr<libp2p::Host>>();
    auto block_tree = injector.template create<sptr<blockchain::BlockTree>>();
    auto block_header_repository =
        injector.template create<sptr<blockchain::BlockHeaderRepository>>();

    auto res = std::make_shared<network::SyncClientsSet>();

    auto &current_peer_info =
        injector.template create<network::OwnPeerInfo &>();
    for (auto &peer_info : peer_infos) {
      spdlog::debug("Added peer with id: {}", peer_info.id.toBase58());
      if (peer_info.id != current_peer_info.id) {
        res->clients.emplace_back(
            std::make_shared<network::RemoteSyncProtocolClient>(
                *host, std::move(peer_info)));
      } else {
        res->clients.emplace_back(
            std::make_shared<network::DummySyncProtocolClient>());
      }
    }
    std::reverse(res->clients.begin(), res->clients.end());
    initialized = res;
    return res;
  }

  template <typename Injector>
  sptr<primitives::ProductionConfiguration> get_production_configuration(
      const Injector &injector) {
    static auto initialized =
        boost::optional<sptr<primitives::ProductionConfiguration>>(boost::none);
    if (initialized) {
      return *initialized;
    }

    auto production_api = injector.template create<sptr<runtime::ProductionApi>>();
    auto configuration_res = production_api->configuration();
    if (! configuration_res) {
      base::raise(configuration_res.error());
    }
    auto config = configuration_res.value();
	//added code for debug mode
	primitives::ProductionDuration duration{ 60000000 };  // changed from 30000000 to 40000000
	config.slot_duration = duration;
	//end
    for (const auto &authority : config.genesis_authorities) {
      spdlog::debug("Production authority: {}", authority.id.id.toHex());
    }
    config.leadership_rate.first *= 3;
    initialized = std::make_shared<primitives::ProductionConfiguration>(config);
    return *initialized;
  };

  template <class Injector>
  sptr<crypto::CryptoStore> get_crypto_store(std::string_view keystore_path,
                                             const Injector &injector) {
    static auto initialized =
        boost::optional<sptr<crypto::CryptoStore>>(boost::none);
    if (initialized) {
      return *initialized;
    }

    auto ed25519_provider =
        injector.template create<sptr<crypto::ED25519Provider>>();
    auto sr25519_provider =
        injector.template create<sptr<crypto::SR25519Provider>>();
    auto secp256k1_provider =
        injector.template create<sptr<crypto::Secp256k1Provider>>();
    auto bip39_provider =
        injector.template create<sptr<crypto::Bip39Provider>>();
    auto random_generator = injector.template create<sptr<crypto::CSPRNG>>();

    auto crypto_store =
        std::make_shared<crypto::CryptoStoreImpl>(std::move(ed25519_provider),
                                                  std::move(sr25519_provider),
                                                  std::move(secp256k1_provider),
                                                  std::move(bip39_provider),
                                                  std::move(random_generator));

    boost::filesystem::path path = std::string(keystore_path);
    if (auto &&res = crypto_store->initialize(path); res) {
      base::raise(res.error());
    }
    initialized = crypto_store;

    return *initialized;
  }

  template <class Injector>
  sptr<verification::finality::FinalizationObserver> get_finalization_observer(
      const Injector &injector) {
    static auto instance = boost::optional<
        std::shared_ptr<verification::finality::FinalizationObserver>>(boost::none);
    if (instance) {
      return *instance;
    }

    instance = std::make_shared<verification::finality::FinalizationComposite>(
        injector.template create<
            std::shared_ptr<authority::AuthorityManagerImpl>>());

    return *instance;
  }

  template <typename... Ts>
  auto makeApplicationInjector(
      const std::string &genesis_path,
      const std::string &rocksdb_path,
      const boost::asio::ip::tcp::endpoint &rpc_http_endpoint,
      const boost::asio::ip::tcp::endpoint &rpc_ws_endpoint,
      Ts &&... args) {
    using namespace boost;  // NOLINT;

    // default values for configurations
    api::RpcThreadPool::Configuration rpc_thread_pool_config{};
    api::HttpSession::Configuration http_config{};
    api::WsSession::Configuration ws_config{};
    transaction_pool::PoolModeratorImpl::Params pool_moderator_config{};
    transaction_pool::TransactionPool::Limits tp_pool_limits{};
    return di::make_injector(
        // bind configs
        injector::useConfig<api::RpcThreadPool::Configuration>(rpc_thread_pool_config),
        injector::useConfig<api::HttpSession::Configuration>(http_config),
        injector::useConfig(ws_config),
        injector::useConfig(pool_moderator_config),
        injector::useConfig(tp_pool_limits),

        // inherit host injector
        libp2p::injector::makeHostInjector<BOOST_DI_CFG>(
            libp2p::injector::useSecurityAdaptors<
                libp2p::security::Secio>()[di::override]),

        // bind boot nodes
        di::bind<network::PeerList>.to(
            [](auto const &inj) { return get_boot_nodes(inj); }),
        di::bind<application::AppStateManager>.template to<application::AppStateManagerImpl>(),

        // bind io_context: 1 per injector
        di::bind<::boost::asio::io_context>.in(
            di::extension::shared)[boost::di::override],

        // bind interfaces
        di::bind<api::HttpListenerImpl>.to(
            [rpc_http_endpoint](const auto &injector) {
              return get_jrpc_api_http_listener(injector, rpc_http_endpoint);
            }),
        di::bind<api::WsListenerImpl>.to(
            [rpc_ws_endpoint](const auto &injector) {
              return get_jrpc_api_ws_listener(injector, rpc_ws_endpoint);
            }),
        di::bind<api::AuthorApi>.template to<api::AuthorApiImpl>(),
        di::bind<api::ChainApi>.template to<api::ChainApiImpl>(),
        di::bind<api::StateApi>.template to<api::StateApiImpl>(),
        di::bind<api::SystemApi>.template to<api::SystemApiImpl>(),
        di::bind<api::ApiService>.to([](const auto &injector) {
          return get_jrpc_api_service(injector);
        }),
        di::bind<api::JRpcServer>.template to<api::JRpcServerImpl>(),
        di::bind<authorship::Proposer>.template to<authorship::ProposerImpl>(),
        di::bind<authorship::BlockBuilder>.template to<authorship::BlockBuilderImpl>(),
        di::bind<authorship::BlockBuilderFactory>.template to<authorship::BlockBuilderFactoryImpl>(),
        di::bind<storage::BufferStorage>.to(
            [rocksdb_path](const auto &injector) {
              return get_rocks_db(rocksdb_path, injector);
            }),
        di::bind<blockchain::BlockStorage>.to(
            [](const auto &injector) { return get_block_storage(injector); }),
        di::bind<blockchain::BlockTree>.to(
            [](auto const &inj) { return get_block_tree(inj); }),
        di::bind<blockchain::BlockHeaderRepository>.template to<blockchain::KeyValueBlockHeaderRepository>(),
        di::bind<clock::SystemClock>.template to<clock::SystemClockImpl>(),
        di::bind<clock::SteadyClock>.template to<clock::SteadyClockImpl>(),
        di::bind<clock::Timer>.template to<clock::BasicWaitableTimer>(),
        di::bind<primitives::ProductionConfiguration>.to([](auto const &injector) {
          return get_production_configuration(injector);
        }),
        di::bind<verification::ProductionSynchronizer>.template to<verification::ProductionSynchronizerImpl>(),
        di::bind<verification::finality::Environment>.template to<verification::finality::EnvironmentImpl>(),
        di::bind<verification::finality::VoteCryptoProvider>.template to<verification::finality::VoteCryptoProviderImpl>(),
        di::bind<verification::EpochStorage>.template to<verification::EpochStorageImpl>(),
        di::bind<verification::BlockValidator>.template to<verification::ProductionBlockValidator>(),
        di::bind<crypto::ED25519Provider>.template to<crypto::ED25519ProviderImpl>(),
        di::bind<crypto::Hasher>.template to<crypto::HasherImpl>(),
        di::bind<crypto::SR25519Provider>.template to<crypto::SR25519ProviderImpl>(),
        di::bind<crypto::VRFProvider>.template to<crypto::VRFProviderImpl>(),
        di::bind<crypto::Bip39Provider>.template to<crypto::Bip39ProviderImpl>(),
        di::bind<crypto::Pbkdf2Provider>.template to<crypto::Pbkdf2ProviderImpl>(),
        di::bind<crypto::Secp256k1Provider>.template to<crypto::Secp256k1ProviderImpl>(),

        di::bind<crypto::CryptoStore>.template to<crypto::CryptoStoreImpl>(),
        di::bind<extensions::ExtensionFactory>./*template */to(
            [](auto const &injector) {
              return get_extension_factory(injector);
            }),
        di::bind<network::Router>.template to<network::RouterLibp2p>(),
        di::bind<verification::ProductionGossiper>.template to<network::GossiperBroadcast>(),
        di::bind<verification::finality::Gossiper>.template to<network::GossiperBroadcast>(),
        di::bind<network::Gossiper>.template to<network::GossiperBroadcast>(),
        di::bind<network::SyncClientsSet>.to([](auto const &injector) {
          return get_sync_clients_set(injector);
        }),
        di::bind<network::SyncProtocolObserver>.template to<network::SyncProtocolObserverImpl>(),
        di::bind<runtime::binaryen::WasmModule>.template to<runtime::binaryen::WasmModuleImpl>(),
        di::bind<runtime::binaryen::WasmModuleFactory>.template to<runtime::binaryen::WasmModuleFactoryImpl>(),
        di::bind<runtime::CoreFactory>.template to<runtime::binaryen::CoreFactoryImpl>(),
        di::bind<runtime::TaggedTransactionQueue>.template to<runtime::binaryen::TaggedTransactionQueueImpl>(),
        di::bind<runtime::ParachainHost>.template to<runtime::binaryen::ParachainHostImpl>(),
        di::bind<runtime::OffchainWorker>.template to<runtime::binaryen::OffchainWorkerImpl>(),

        di::bind<runtime::Metadata>.template to<runtime::binaryen::MetadataImpl>(),
        di::bind<runtime::FinalityApi>.template to<runtime::binaryen::FinalityApiImpl>(),
        di::bind<runtime::Core>.template to<runtime::binaryen::CoreImpl>(),
        di::bind<runtime::ProductionApi>.template to<runtime::binaryen::ProductionApiImpl>(),
        di::bind<runtime::BlockBuilder>.template to<runtime::binaryen::BlockBuilderImpl>(),
        di::bind<runtime::TrieStorageProvider>.template to<runtime::TrieStorageProviderImpl>(),
        di::bind<transaction_pool::TransactionPool>.template to<transaction_pool::TransactionPoolImpl>(),
        di::bind<transaction_pool::PoolModerator>.template to<transaction_pool::PoolModeratorImpl>(),
        di::bind<storage::changes_trie::ChangesTracker>.template to<storage::changes_trie::StorageChangesTrackerImpl>(),
        di::bind<storage::trie::TrieStorageBackend>.to(
            [](auto const &inj) { return get_trie_storage_backend(inj); }),
        di::bind<storage::trie::TrieStorageImpl>.to(
            [](auto const &inj) { return get_trie_storage_impl(inj); }),
        di::bind<storage::trie::TrieStorage>.to(
            [](auto const &inj) { return get_trie_storage(inj); }),
        di::bind<storage::trie::SuperGeniusTrieFactory>.template to<storage::trie::SuperGeniusTrieFactoryImpl>(),
        di::bind<storage::trie::Codec>.template to<storage::trie::SuperGeniusCodec>(),
        di::bind<storage::trie::TrieSerializer>.template to<storage::trie::TrieSerializerImpl>(),
        di::bind<runtime::WasmProvider>.template to<runtime::StorageWasmProvider>(),
        di::bind<application::ConfigurationStorage>.to(
            [genesis_path](const auto &injector) {
              return get_configuration_storage(genesis_path, injector);
            }),
        di::bind<network::ExtrinsicObserver>.template to<network::ExtrinsicObserverImpl>(),
        di::bind<network::ExtrinsicGossiper>.template to<network::GossiperBroadcast>(),
        di::bind<authority::AuthorityUpdateObserver>.template to<authority::AuthorityManagerImpl>(),
        di::bind<authority::AuthorityManager>.template to<authority::AuthorityManagerImpl>(),
        di::bind<verification::finality::FinalizationObserver>.to(
            [](auto const &inj) { return get_finalization_observer(inj); }),

        // user-defined overrides...
        std::forward<decltype(args)>(args)...);
  }

}  // namespace sgns::injector

#endif  // SUPERGENIUS_SRC_INJECTOR_APPLICATION_INJECTOR_HPP
