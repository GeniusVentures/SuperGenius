#include "outcome/outcome.hpp"
#include <string>
#include <string_view>
#include <utility>

namespace sgns::password
{
    outcome::result<void> StoreCredentials( std::string_view userName, std::string_view password );

    outcome::result<std::pair<std::string, std::string>> RetrieveCredential();
}
