#include "../ISecureStorage.hpp"

namespace sgns
{
    class WindowsSecureStorage : public ISecureStorage
    {
    public:
        std::string GetName() override
        {
            return "WindowsSecureStorage";
        }

        outcome::result<SecureBufferType> Load( const std::string &key ) override;

        outcome::result<void> Save( const std::string &key, const SecureBufferType &buffer ) override;

        outcome::result<bool> DeleteKey( const std::string &key ) override;
    };
}
