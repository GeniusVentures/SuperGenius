

#include "network/helpers/scale_message_read_writer.hpp"

namespace sgns::network {
  ScaleMessageReadWriter::ScaleMessageReadWriter(
      std::shared_ptr<libp2p::basic::MessageReadWriter> read_writer)
      : read_writer_{std::move(read_writer)} {}

  ScaleMessageReadWriter::ScaleMessageReadWriter(
      std::shared_ptr<libp2p::basic::ReadWriter> read_writer)
      : read_writer_{std::make_shared<libp2p::basic::MessageReadWriterUvarint>(
          std::move(read_writer))} {}
}  // namespace sgns::network
