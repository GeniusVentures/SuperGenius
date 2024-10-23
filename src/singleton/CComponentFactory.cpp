/**
 * @file       CComponentFactory.cpp
 * @brief      
 * @date       2024-02-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include "CComponentFactory.hpp"

void CComponentFactory::Register( std::shared_ptr<IComponent>         component,
                                  const std::string                  &type,
                                  const boost::optional<std::string> &variant )
{
    std::string key = type;

    if ( variant )
    {
        key.append( variant.value() );
    }
    if ( ComponentTable.count( key ) == 0 )
    {
        ComponentTable[key] = component;
    }
    else
    {
        ComponentTable[key] = component; // Overwrites existing value
    }
}

outcome::result<std::shared_ptr<IComponent>> CComponentFactory::GetComponent(
    const std::string &type, const boost::optional<std::string> &variant )
{
    std::string key = type;

    if ( variant )
    {
        key.append( variant.value() );
    }

    if ( ComponentTable.count( key ) == 0 )
    {
        return outcome::failure( boost::system::error_code{} );
    }

    return ComponentTable[key];
}
