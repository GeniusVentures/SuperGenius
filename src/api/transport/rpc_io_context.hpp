
#ifndef SUPERGENIUS_SRC_API_RPC_IO_CONTEXT_HPP
#define SUPERGENIUS_SRC_API_RPC_IO_CONTEXT_HPP

#include <boost/asio/io_context.hpp>

namespace sgns::api {

  class RpcContext : public boost::asio::io_context {
   public:
    using boost::asio::io_context::io_context;
  };

}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_API_RPC_IO_CONTEXT_HPP
