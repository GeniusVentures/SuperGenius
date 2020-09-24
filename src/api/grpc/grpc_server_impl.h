#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "SuperGenius_OpenAPI.grpc.pb.h"


using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

// Model messages

using sgns::BalanceInfo;
using sgns::BlockCountInfo;
using sgns::Account;
using sgns::History;
using sgns::AccountHistory;
using sgns::AccountInfo;
using sgns::Key;
using sgns::Accounts;
using sgns::Status;
using sgns::Representative;
using sgns::BlockHash;
using sgns::Weight;
using sgns::AccountBalanceInfoPairs;
using sgns::AccountBalancePairs;
using sgns::AccountFrontierPairs;
using sgns::Count;
using sgns::AccountHashPairs;
// Logic and data behind the server's behavior.
class SuperGenius_OpenAPIImpl final : public sgns::SuperGenius_OpenAPI {
  Status GETAccountBalance (ServerContext* context, GETAccountBalanceParameters * request, BalanceInfo * response) {
    
  }
  Status GETAccountBlockCount(ServerContext* context, GETAccountBlockCountParameters *request, BlockCountInfo* response){

  }
  Status GETAccountCreate (ServerContext* context, GETAccountCreateParameters* request, Account* response ) {
  }

  Status GETTotalSupply (ServerContext* context, AvailableSupply *response) {
  }
  Status GETBlock (ServerContext* context, GETBlockParameters *reuest, Block *response) {
  }
  Status GETBlockCount (ServerContext* context, BlockCountInfo *response) {
  }
  Status GETPeers (ServerContext* context,  PeerPortPairs *response ) {
  }
  

};
