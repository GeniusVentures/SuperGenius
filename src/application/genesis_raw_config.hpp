#ifndef SUPERGENIUS_SRC_APPLICATION_GENESIS_RAW_CONFIG_HPP
#define SUPERGENIUS_SRC_APPLICATION_GENESIS_RAW_CONFIG_HPP

#include "base/buffer.hpp"

namespace sgns::application {

  // configurations from genesis.json lying under "genesis"->"raw" key
  using GenesisRawConfig = std::vector<std::pair<base::Buffer, base::Buffer>>;

}  // namespace sgns::application

#endif  // SUPERGENIUS_SRC_APPLICATION_GENESIS_RAW_CONFIG_HPP
