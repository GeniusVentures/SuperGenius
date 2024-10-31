#ifndef SUPERGENIUS_CRDT_OPTIONS_HPP
#define SUPERGENIUS_CRDT_OPTIONS_HPP

#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "crdt/hierarchical_key.hpp"
#include <functional>

namespace sgns::crdt
{
  /** @brief Options holds configurable values for CrdtDatastore
  */
  struct CrdtOptions
  {
    using Buffer = base::Buffer;
    using Logger = base::Logger;

    using PutHookPtr = std::function<void(const std::string& k, const Buffer& v)>;
    using DeleteHookPtr = std::function<void(const std::string& k)>;

    Logger logger = nullptr;

    /** RebroadcastInterval specifies interval to rebroadcast data */
    long long rebroadcastIntervalMilliseconds = 0;

    /** DAGSyncerTimeout specifies how long to wait for a DAGSyncer.
    * Set to 0 to disable.
    */
    long long dagSyncerTimeoutSec = 0;

    /** NumWorkers specifies the number of workers ready to walk DAGs */
    int numWorkers = 0;

    /** The PutHook function is triggered whenever an element
    * is successfully added to the datastore (either by a local
    * or remote update), and only when that addition is considered the
    * prevalent value.
    */
    PutHookPtr putHookFunc = nullptr;

    /** The DeleteHook function is triggered whenever a version of an
    * element is successfully removed from the datastore (either by a
    * local or remote update). Unordered and concurrent updates may
    * result in the DeleteHook being triggered even though the element is
    * still present in the datastore because it was re-added. If that is
    * relevant, use Has() to check if the removed element is still part
    * of the datastore.
    */
    DeleteHookPtr deleteHookFunc = nullptr;

    enum class VerifyErrorCode
    {
      Success = 0, // 0 should not represent an error
      InvalidRebroadcastInterval, // invalid RebroadcastInterval
      LoggerUndefinied, // the Logger is undefined
      BadNumberOfNumWorkers, // bad number of NumWorkers
      InvalidDAGSyncerTimeout, // invalid DAGSyncerTimeout
    };

    static std::shared_ptr<CrdtOptions> DefaultOptions()
    {
      auto options = std::make_shared<CrdtOptions>();
      options->logger = base::createLogger("CrdtDatastore");
      options->rebroadcastIntervalMilliseconds = 10000; // 10s
      options->dagSyncerTimeoutSec = 300; // 5 mins
      options->numWorkers = 5;
      return options;
    }

    /** Verifies CrdtOptions */
    outcome::result<VerifyErrorCode> Verify() const
    {
      if (rebroadcastIntervalMilliseconds <= 0)
      {
        return VerifyErrorCode::InvalidRebroadcastInterval;
      }
      if (numWorkers <= 0)
      {
        return VerifyErrorCode::BadNumberOfNumWorkers;
      }
      if (dagSyncerTimeoutSec < 0)
      {
        return VerifyErrorCode::InvalidDAGSyncerTimeout;
      }
      return VerifyErrorCode::Success;
    }

    bool operator==( const CrdtOptions &rhs ) const
    {
      return logger == rhs.logger && rebroadcastIntervalMilliseconds == rhs.rebroadcastIntervalMilliseconds;
    }

    bool operator!=( const CrdtOptions &rhs ) const
    {
      return !operator==( rhs );
    }

  };
} // namespace sgns::crdt

#endif //SUPERGENIUS_CRDT_OPTIONS_HPP
