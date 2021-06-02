#include "crdt/graphsync_dagsyncer.hpp"

namespace sgns::crdt
{

  GraphsyncDAGSyncer::GraphsyncDAGSyncer(const std::shared_ptr<IpfsDatastore>& service, 
    const std::shared_ptr<Graphsync>& graphsync, const std::shared_ptr<libp2p::Host>& host)
    : DAGSyncer(service)
    , graphsync_(graphsync)
    , host_(host)
  {
  }

  outcome::result<void> GraphsyncDAGSyncer::Listen(const Multiaddress& listen_to)
  {
    if (this->host_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    auto listen_res = host_->listen(listen_to);
    if (listen_res.has_failure())
    {
      /*logger->trace("Cannot listen to multiaddress {}, {}",
        listen_to.getStringAddress(),
        listen_res.error().message());*/
      return listen_res.error();
    }
    auto startResult = this->StartSync();
    if (startResult.has_failure())
    {
      return startResult.error();
    }

    return outcome::success();
  }

  outcome::result<void> GraphsyncDAGSyncer::RequestNode(const PeerId& peer,  boost::optional<Multiaddress> address,
    const CID& root_cid)
  {
    auto startResult = this->StartSync();
    if (startResult.has_failure())
    {
      return startResult.error();
    }

    if (this->graphsync_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }
    std::vector<Extension> extensions;
    ResponseMetadata response_metadata{};
    Extension response_metadata_extension = ipfs_lite::ipfs::graphsync::encodeResponseMetadata(response_metadata);
    extensions.push_back(response_metadata_extension);
    
    std::vector<CID> cids;
    Extension do_not_send_cids_extension = ipfs_lite::ipfs::graphsync::encodeDontSendCids(cids);
    extensions.push_back(do_not_send_cids_extension);
    auto subscription = this->graphsync_->makeRequest(peer, std::move(address),  root_cid, {}, extensions, this->RequestNodeCallback());

    // keeping subscriptions alive, otherwise they cancel themselves
    this->requests_.push_back(std::shared_ptr<Subscription>(new Subscription(std::move(subscription))));

    return outcome::success();
  }

  outcome::result<bool> GraphsyncDAGSyncer::HasBlock(const CID& cid) const
  {
    auto getNodeResult = this->getNode(cid);
    return getNodeResult.has_value();
  }

  outcome::result<bool> GraphsyncDAGSyncer::StartSync()
  {
    if (!started_)
    {
      if (this->graphsync_ == nullptr)
      {
        return outcome::failure(boost::system::error_code{});
      }

      auto dagService = std::make_shared<MerkleDagBridgeImpl>(shared_from_this());
      if (dagService == nullptr)
      {
        return outcome::failure(boost::system::error_code{});
      }

      BlockCallback blockCallback = std::bind(&BlockReceivedCallback, std::placeholders::_1, std::placeholders::_2, this);
      this->graphsync_->start(dagService, blockCallback);

      if (this->host_ == nullptr)
      {
        return outcome::failure(boost::system::error_code{});
      }
      this->host_->start();

      this->started_ = true;
    }
    return this->started_;
  }

  void GraphsyncDAGSyncer::StopSync()
  {
    if (this->graphsync_ != nullptr)
    {
      this->graphsync_->stop();
    }
    if (this->host_ != nullptr)
    {
      this->host_->stop();
    }
    this->started_ = false;
  }

  outcome::result<GraphsyncDAGSyncer::PeerId> GraphsyncDAGSyncer::GetId() const
  {
    if (this->host_ != nullptr)
    {
      return this->host_->getId();
    }
    return outcome::failure(boost::system::error_code{});
  }

  GraphsyncDAGSyncer::RequestProgressCallback GraphsyncDAGSyncer::RequestNodeCallback()
  {
    static auto formatExtensions =
      [](const std::vector<Extension>& extensions) -> std::string 
    {
      std::string s;
      for (const auto& item : extensions) {
        s += fmt::format(
          "({}: 0x{}) ", item.name, common::Buffer(item.data).toHex());
      }
      return s;
    };
    
    return [this](ResponseStatusCode code, const std::vector<Extension>& extensions) 
    {
        //++responses_received;
      logger_->trace("request progress: code={}, extensions={}", statusCodeToString(code), formatExtensions(extensions));
        
        /*if (++n_responses == n_responses_expected_) 
        {
          io_->stop();
        }*/
    };
  }


  void GraphsyncDAGSyncer::BlockReceivedCallback(CID cid, sgns::common::Buffer buffer, GraphsyncDAGSyncer* dagSyncer)
  {
    if (dagSyncer != nullptr)
    {
      dagSyncer->logger_->trace("Block received: cid={}, extensions={}", cid.toString(), buffer.toHex());
    }
  }



}
