/**
 * @file       IComponent.hpp
 * @brief      Component interface class
 * @date       2024-02-23
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _ICOMPONENT_HPP_
#define _ICOMPONENT_HPP_
#include <boost/optional.hpp>
class IComponent
{
public:
    virtual ~IComponent()         = default;
    virtual std::string GetName() = 0;

};

#endif