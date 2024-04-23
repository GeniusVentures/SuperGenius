/**
 * @file       AccountManager.hpp
 * @brief      
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _ACCOUNT_MANAGER_HPP_
#define _ACCOUNT_MANAGER_HPP_
#include <memory>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include "ipfs_pubsub/gossip_pubsub.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crypto/hasher/hasher_impl.hpp"
#include "blockchain/impl/common.hpp"
#include "blockchain/impl/key_value_block_header_repository.hpp"
#include "blockchain/impl/key_value_block_storage.hpp"
#include "integration/IComponent.hpp"
#include "account/GeniusAccount.hpp"
#include "account/TransactionManager.hpp"

using namespace boost::multiprecision;
namespace sgns
{
    class AccountManager : public IComponent
    {

    public:
        AccountManager( const std::string &priv_key_data );
        AccountManager();
        ~AccountManager();
        boost::optional<GeniusAccount> CreateAccount( const std::string &priv_key_data, const uint64_t &initial_amount );
        //void ImportAccount(const std::string &priv_key_data);

        void        ProcessImage( const std::string &image_path, uint16_t funds );
        std::string GetName() override
        {
            return "AccountManager";
        }
        void MintTokens( uint64_t amount );
        void AddPeer(const std::string &peer);

    private:
        std::shared_ptr<GeniusAccount>                             account_;
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                 pubsub_;
        std::shared_ptr<boost::asio::io_context>                   io_;
        std::shared_ptr<crdt::GlobalDB>                            globaldb_;
        std::shared_ptr<crypto::HasherImpl>                        hasher_;
        std::shared_ptr<blockchain::KeyValueBlockHeaderRepository> header_repo_;
        std::shared_ptr<blockchain::KeyValueBlockStorage>          block_storage_;
        std::shared_ptr<TransactionManager>                        transaction_manager_;

        std::thread                              io_thread;
        std::shared_ptr<boost::asio::signal_set> signals_;

        static constexpr std::string_view db_path_ = "bc-%d/";
        static constexpr std::uint16_t    MAIN_NET = 369;
        static constexpr std::uint16_t    TEST_NET = 963;
    };
};

#endif
