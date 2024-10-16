#ifndef SUPERGENIUS_API_SYSTEMAPI
#define SUPERGENIUS_API_SYSTEMAPI

#include "application/configuration_storage.hpp"
#include "integration/IComponent.hpp"

namespace sgns::api {

  /// Auxiliary class that providing access for some app's parts over RPC
  class SystemApi : public IComponent {
   public:
    virtual ~SystemApi() = default;

    virtual std::shared_ptr<application::ConfigurationStorage> getConfig()
        const = 0;
  };

}  // namespace sgns::api

#endif  // SUPERGENIUS_API_SYSTEMAPI
