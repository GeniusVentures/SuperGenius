#ifndef SUPERGENIUS_NODE_CONFIG_HPP
#define SUPERGENIUS_NODE_CONFIG_HPP
#include <lib/config.hpp>
#include <secure/common.hpp>
namespace sgns
{
class node_config
{
    public:
    node_config();

};
class node_flags final
{
public:

    sgns::generate_cache generate_cache;
    bool read_only { false };
};
}
#endif