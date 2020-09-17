
#ifndef SUPERGENIUS_API_SYSTEMAPIIMPL
#define SUPERGENIUS_API_SYSTEMAPIIMPL

#include "api/service/system/system_api.hpp"

#include "application/configuration_storage.hpp"

namespace sgns::api {

  class SystemApiImpl final : public SystemApi {
   public:
    SystemApiImpl(std::shared_ptr<application::ConfigurationStorage> config);

    std::shared_ptr<application::ConfigurationStorage> getConfig()
        const override;

   private:
    std::shared_ptr<application::ConfigurationStorage> config_;
  };

}  // namespace sgns::api

#endif  // SUPERGENIUS_API_SYSTEMAPIIMPL
