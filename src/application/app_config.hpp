
#ifndef SUPERGENIUS_APP_CONFIG_HPP
#define SUPERGENIUS_APP_CONFIG_HPP

#include <spdlog/spdlog.h>
#include <boost/asio/ip/tcp.hpp>
#include <memory>
#include <string>

namespace sgns::application
{

    /**
   * Parse and store application config.
   */
    class AppConfiguration
    {
    public:
        enum struct LoadScheme
        {
            kBlockProducing,
            kValidating,
            kFullSyncing,
        };

        virtual ~AppConfiguration() = default;

        /**
        * @return file path with genesis configuration.
        */
        [[nodiscard]] virtual const std::string &genesis_path() const = 0;

        /**
        * @return keystore directory path.
        */
        [[nodiscard]] virtual const std::string &keystore_path() const = 0;

        /**
        * @return rocksdb directory path.
        */
        [[nodiscard]] virtual const std::string &rocksdb_path() const = 0;

        /**
        * @return port for peer to peer interactions.
        */
        [[nodiscard]] virtual uint16_t p2p_port() const = 0;

        /**
        * @return endpoint for RPC over HTTP.
        */
        [[nodiscard]] virtual const boost::asio::ip::tcp::endpoint &rpc_http_endpoint() const = 0;

        /**
        * @return endpoint for RPC over Websocket protocol.
        */
        [[nodiscard]] virtual const boost::asio::ip::tcp::endpoint &rpc_ws_endpoint() const = 0;

        /**
        * @return log level (0-trace, 5-only critical, 6-no logs).
        */
        [[nodiscard]] virtual spdlog::level::level_enum verbosity() const = 0;

        /**
        * @return true if node in only finalizing mode, otherwise false.
        */
        [[nodiscard]] virtual bool is_only_finalizing() const = 0;

        virtual bool initialize_from_args( LoadScheme scheme, int argc, char **argv ) = 0;
    };

    using AppConfigPtr = std::shared_ptr<AppConfiguration>;

} // namespace sgns::application

#endif // SUPERGENIUS_APP_CONFIG_HPP
