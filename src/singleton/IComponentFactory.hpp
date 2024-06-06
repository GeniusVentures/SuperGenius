/**
 * @file       IComponentFactory.hpp
 * @brief      
 * @date       2024-02-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "IComponent.hpp"
#include <memory>

class IComponentFactory
{
public:
    virtual ~IComponentFactory() = default;

    virtual void Register( std::shared_ptr<IComponent> component, const std::string &type, const boost::optional<std::string> &variant ) = 0;

    virtual boost::optional<std::shared_ptr<IComponent>> GetComponent( const std::string &type, const boost::optional<std::string> &variant ) = 0;
};