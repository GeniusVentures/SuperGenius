#ifndef SUPERGENIUS_NODE_CONFIG_HPP
#define SUPERGENIUS_NODE_CONFIG_HPP
#include <lib/config.hpp>
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
	bool read_only{ false };
    sgns::generate_cache generate_cache;
};
}
#endif