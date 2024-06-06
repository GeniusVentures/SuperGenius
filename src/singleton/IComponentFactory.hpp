/**
 * @file       IComponentFactory.hpp
 * @brief      
 * @date       2024-02-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <memory>
#include <boost/optional.hpp>
#include "singleton/IComponent.hpp"
#include "outcome/outcome.hpp"

class IComponentFactory
{
public:
    virtual ~IComponentFactory() = default;

    virtual void Register( std::shared_ptr<IComponent>               component,
                           const std::string                        &type,
                           const boost::optional<std::string> &variant ) = 0;

    virtual outcome::result<std::shared_ptr<IComponent>> GetComponent(
        const std::string &type, const boost::optional<std::string> &variant ) = 0;
};
