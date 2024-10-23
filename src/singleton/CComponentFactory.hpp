/**
 * @file       CComponentFactory.hpp
 * @brief      
 * @date       2024-02-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _CCOMPONENT_FACTORY_HPP_
#define _CCOMPONENT_FACTORY_HPP_

#include <unordered_map>
#include "singleton/IComponentFactory.hpp"
#include "singleton/Singleton.hpp"

class CComponentFactory : public IComponentFactory
{
    SINGLETON( CComponentFactory );

private:
    std::unordered_map<std::string, std::shared_ptr<IComponent>> ComponentTable;

public:
    void Register( std::shared_ptr<IComponent>         component,
                   const std::string                  &type,
                   const boost::optional<std::string> &variant = boost::none ) override;

    outcome::result<std::shared_ptr<IComponent>> GetComponent(
        const std::string &type, const boost::optional<std::string> &variant = boost::none ) override;
};

#endif
