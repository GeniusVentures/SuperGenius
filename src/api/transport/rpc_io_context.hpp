
#ifndef SUPERGENIUS_SRC_API_RPC_IO_CONTEXT_HPP
#define SUPERGENIUS_SRC_API_RPC_IO_CONTEXT_HPP

#include <boost/asio/io_context.hpp>
#include "singleton/IComponent.hpp"

namespace sgns::api {

  class RpcContext : public boost::asio::io_context, public IComponent {
   public:
    using boost::asio::io_context::io_context;

    std::string GetName() override
    {
      return "RpcContext";
    }
  };

}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_API_RPC_IO_CONTEXT_HPP
